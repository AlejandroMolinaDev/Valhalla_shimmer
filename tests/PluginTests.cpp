#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <thread>

#include "BinaryData.h"
#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

using Catch::Matchers::WithinAbs;

namespace
{
    float plainValue (AuroraBloomProcessor& p, const char* id)
    {
        return p.getApvts().getRawParameterValue (id)->load();
    }

    void setPlain (AuroraBloomProcessor& p, const char* id, float value)
    {
        auto* param = p.getApvts().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void prepare (AuroraBloomProcessor& p, double sr, int block, int inChannels = 2)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (inChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        REQUIRE (p.setBusesLayout (layout));
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
    }

    bool bufferIsFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    juce::Component* findById (juce::Component& root, const juce::String& id)
    {
        if (root.getComponentID() == id)
            return &root;
        for (auto* child : root.getChildren())
            if (auto* found = findById (*child, id))
                return found;
        return nullptr;
    }

    void fillNoise (juce::AudioBuffer<float>& b, juce::Random& rng, float amp = 0.3f)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, amp * (rng.nextFloat() * 2.0f - 1.0f));
    }
} // namespace

// =============================================================================================
TEST_CASE ("Plugin instantiates and supports mono/stereo -> stereo", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;

    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (proc.checkBusesLayoutSupported (stereo));

    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (proc.checkBusesLayoutSupported (mono));

    juce::AudioProcessor::BusesLayout monoOut;
    monoOut.inputBuses.add (juce::AudioChannelSet::mono());
    monoOut.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK_FALSE (proc.checkBusesLayoutSupported (monoOut));

    CHECK (proc.getLatencySamples() == 0);
    CHECK (proc.getTailLengthSeconds() >= 1.0);
}

TEST_CASE ("Parameters: IDs, defaults and text round trips", "[plugin][parameters]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    namespace id = ab::params::id;

    CHECK_THAT (plainValue (proc, id::mix), WithinAbs (50.0, 1.0e-4));
    CHECK_THAT (plainValue (proc, id::pitch), WithinAbs (12.0, 1.0e-4));
    CHECK_THAT (plainValue (proc, id::feedback), WithinAbs (60.0, 1.0e-4));
    CHECK_THAT (plainValue (proc, id::diffusion), WithinAbs (0.8, 1.0e-4));
    CHECK_THAT (plainValue (proc, id::size), WithinAbs (60.0, 1.0e-4));
    CHECK_THAT (plainValue (proc, id::lowCut), WithinAbs (20.0, 1.0e-2));
    CHECK_THAT (plainValue (proc, id::highCut), WithinAbs (8000.0, 1.0));
    CHECK_THAT (plainValue (proc, id::modRate), WithinAbs (0.4, 1.0e-4));
    CHECK_THAT (plainValue (proc, id::modDepth), WithinAbs (15.0, 1.0e-4));
    CHECK (plainValue (proc, id::reverbMode) == 2.0f);
    CHECK (plainValue (proc, id::pitchMode) == 0.0f);
    CHECK (plainValue (proc, id::colorMode) == 0.0f);

    // Every parameter is automatable and its text converts back to (almost) the same value.
    for (auto* p : proc.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        REQUIRE (ranged != nullptr);
        CHECK (ranged->isAutomatable());
        if (dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr)
            continue; // choices snap to their steps; checked through their names below
        for (const float norm : { 0.0f, 0.13f, 0.5f, 0.77f, 1.0f })
        {
            const auto text = ranged->getText (norm, 64);
            const auto back = ranged->getValueForText (text);
            CAPTURE (ranged->getName (64), text, norm, back);
            CHECK_THAT (back, WithinAbs (norm, 0.02));
        }
    }

    auto* reverbMode = proc.getApvts().getParameter (id::reverbMode);
    CHECK (reverbMode->getText (reverbMode->convertTo0to1 (1.0f), 64) == "Big Stereo");
    CHECK (reverbMode->getValueForText ("Small Stereo") == reverbMode->convertTo0to1 (3.0f));

    CHECK (ab::params::frequencyToText (8000.0f) == "8.00 kHz");
    CHECK (ab::params::frequencyToText (20.0f) == "20.0 Hz");
    CHECK (ab::params::semitonesToText (12.0f) == "+12 st");
    CHECK (ab::params::semitonesToText (-7.25f) == "-7.25 st");
    CHECK_THAT (ab::params::textToFrequency ("12.5 kHz"), WithinAbs (12500.0, 0.1));
    CHECK_THAT (ab::params::textToNumber ("-12 st"), WithinAbs (-12.0, 1.0e-6));
}

// =============================================================================================
TEST_CASE ("Test 9 - APVTS state save and restore", "[plugin][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    namespace id = ab::params::id;

    juce::MemoryBlock state;
    {
        AuroraBloomProcessor a;
        setPlain (a, id::mix, 33.0f);
        setPlain (a, id::pitch, -7.0f);
        setPlain (a, id::feedback, 91.5f);
        setPlain (a, id::diffusion, 0.618f);
        setPlain (a, id::size, 12.0f);
        setPlain (a, id::lowCut, 250.0f);
        setPlain (a, id::highCut, 3300.0f);
        setPlain (a, id::modRate, 2.2f);
        setPlain (a, id::modDepth, 64.0f);
        setPlain (a, id::reverbMode, 0.0f);
        setPlain (a, id::pitchMode, 3.0f);
        setPlain (a, id::colorMode, 1.0f);
        a.setEditorWidth (1290);
        a.getStateInformation (state);
    }

    AuroraBloomProcessor b;
    b.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    CHECK_THAT (plainValue (b, id::mix), WithinAbs (33.0, 0.02));
    CHECK_THAT (plainValue (b, id::pitch), WithinAbs (-7.0, 0.01));
    CHECK_THAT (plainValue (b, id::feedback), WithinAbs (91.5, 0.02));
    CHECK_THAT (plainValue (b, id::diffusion), WithinAbs (0.618, 0.002));
    CHECK_THAT (plainValue (b, id::size), WithinAbs (12.0, 0.02));
    CHECK_THAT (plainValue (b, id::lowCut), WithinAbs (250.0, 0.5));
    CHECK_THAT (plainValue (b, id::highCut), WithinAbs (3300.0, 2.0));
    CHECK_THAT (plainValue (b, id::modRate), WithinAbs (2.2, 0.005));
    CHECK_THAT (plainValue (b, id::modDepth), WithinAbs (64.0, 0.02));
    CHECK (plainValue (b, id::reverbMode) == 0.0f);
    CHECK (plainValue (b, id::pitchMode) == 3.0f);
    CHECK (plainValue (b, id::colorMode) == 1.0f);
    CHECK (b.getEditorWidth() == 1290);

    // Garbage or foreign state must be ignored without crashing.
    const char junk[] = "not a valid state";
    b.setStateInformation (junk, static_cast<int> (sizeof (junk)));
    CHECK_THAT (plainValue (b, id::mix), WithinAbs (33.0, 0.02));
}

TEST_CASE ("Factory presets load and round trip through XML", "[plugin][presets]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    auto& presets = proc.getPresetManager();

    int factory = 0;
    for (const auto& p : presets.getPresets())
        factory += p.isFactory ? 1 : 0;
    CHECK (factory >= 12);

    for (int i = 0; i < factory; ++i)
    {
        REQUIRE (presets.loadPreset (i));
        CHECK (presets.getCurrentName() == presets.getPresets()[static_cast<size_t> (i)].name);
        CHECK_FALSE (presets.isModified());
    }

    // "Endless Pad" must be the 100 % feedback preset.
    for (int i = 0; i < factory; ++i)
        if (presets.getPresets()[static_cast<size_t> (i)].name == "Endless Pad")
        {
            presets.loadPreset (i);
            CHECK_THAT (plainValue (proc, ab::params::id::feedback), WithinAbs (100.0, 0.01));
        }

    const auto xml = PresetManager::createPresetXml (proc.getApvts(), "Test", "User");
    AuroraBloomProcessor other;
    REQUIRE (PresetManager::applyPresetXml (other.getApvts(), *xml));
    for (const auto& id : ab::params::allIds())
        CHECK_THAT (plainValue (other, id.toRawUTF8()), WithinAbs (plainValue (proc, id.toRawUTF8()), 1.0e-3));
}

TEST_CASE ("Factory presets are exposed as host programs (FL Studio preset menu)", "[plugin][presets]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    namespace id = ab::params::id;

    REQUIRE (proc.getNumPrograms() == 12);
    CHECK (proc.getProgramName (0) == "Octave Halo");
    CHECK (proc.getProgramName (9) == "Endless Pad");

    proc.setCurrentProgram (9);
    CHECK (proc.getCurrentProgram() == 9);
    CHECK (proc.getPresetManager().getCurrentName() == "Endless Pad");
    CHECK_THAT (plainValue (proc, id::feedback), WithinAbs (100.0, 0.01));

    // Out-of-range program changes are ignored.
    proc.setCurrentProgram (99);
    CHECK (proc.getCurrentProgram() == 9);

    // The program survives a state round trip through its preset name.
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    AuroraBloomProcessor other;
    other.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (other.getCurrentProgram() == 9);
}

TEST_CASE ("Init resets every parameter to its default", "[plugin][presets]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    namespace id = ab::params::id;

    proc.setCurrentProgram (5);
    proc.getPresetManager().loadInit();
    CHECK (proc.getPresetManager().getCurrentName() == "Init");
    CHECK_FALSE (proc.getPresetManager().isModified());
    for (auto* p : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            CHECK_THAT (ranged->getValue(), WithinAbs (ranged->getDefaultValue(), 1.0e-6));
    CHECK_THAT (plainValue (proc, id::pitch), WithinAbs (12.0, 1.0e-4));
}

TEST_CASE ("A/B comparison swaps between two parameter sets", "[plugin][presets]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    auto& presets = proc.getPresetManager();
    namespace id = ab::params::id;

    setPlain (proc, id::mix, 20.0f);
    presets.toggleSlot(); // -> B (starts as a copy of A)
    CHECK (presets.getActiveSlot() == 1);
    setPlain (proc, id::mix, 80.0f);
    presets.toggleSlot(); // -> A
    CHECK_THAT (plainValue (proc, id::mix), WithinAbs (20.0, 0.01));
    presets.toggleSlot(); // -> B
    CHECK_THAT (plainValue (proc, id::mix), WithinAbs (80.0, 0.01));
}

// =============================================================================================
// Host simulation: call patterns seen in FL Studio and other hosts
// =============================================================================================
TEST_CASE ("Host: processBlock before prepareToPlay and setState before prepare are safe", "[plugin][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;

    juce::MemoryBlock state;
    proc.getStateInformation (state);
    proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    juce::AudioBuffer<float> buffer (2, 256);
    juce::Random rng (1);
    fillNoise (buffer, rng);
    const juce::AudioBuffer<float> copy (buffer);
    juce::MidiBuffer midi;
    proc.processBlock (buffer, midi);
    for (int i = 0; i < 256; ++i)
        CHECK (buffer.getSample (0, i) == copy.getSample (0, i)); // passthrough
}

TEST_CASE ("Host: irregular, zero-length and oversized blocks", "[plugin][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    prepare (proc, 44100.0, 512);
    setPlain (proc, ab::params::id::feedback, 100.0f);

    juce::Random rng (2);
    juce::MidiBuffer midi;
    for (const int n : { 0, 1, 3, 512, 0, 17, 4096, 8192, 1, 255, 511, 0, 2048 })
    {
        juce::AudioBuffer<float> buffer (2, n);
        fillNoise (buffer, rng);
        proc.processBlock (buffer, midi);
        REQUIRE (bufferIsFinite (buffer));
        if (n > 0)
            CHECK (buffer.getMagnitude (0, n) < 2.0f);
    }
}

TEST_CASE ("Host: re-prepare at another sample rate, mono input and bypass", "[plugin][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    juce::Random rng (3);
    juce::MidiBuffer midi;

    for (const double sr : { 48000.0, 96000.0, 44100.0, 192000.0, 48000.0 })
    {
        prepare (proc, sr, 256, sr > 100000.0 ? 1 : 2);
        juce::AudioBuffer<float> buffer (2, 256);
        for (int k = 0; k < 20; ++k)
        {
            fillNoise (buffer, rng);
            proc.processBlock (buffer, midi);
            REQUIRE (bufferIsFinite (buffer));
        }
    }

    // Host bypass: after the ramp, the output equals the input.
    for (int k = 0; k < 40; ++k)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        fillNoise (buffer, rng);
        proc.processBlockBypassed (buffer, midi);
    }
    juce::AudioBuffer<float> buffer (2, 256);
    fillNoise (buffer, rng);
    const juce::AudioBuffer<float> copy (buffer);
    proc.processBlockBypassed (buffer, midi);
    for (int i = 0; i < 256; ++i)
        REQUIRE_THAT (buffer.getSample (1, i), WithinAbs (copy.getSample (1, i), 1.0e-6));
}

TEST_CASE ("Host: several instances processing on parallel threads", "[plugin][host]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr int kInstances = 4;
    std::vector<std::unique_ptr<AuroraBloomProcessor>> instances;
    for (int i = 0; i < kInstances; ++i)
    {
        instances.push_back (std::make_unique<AuroraBloomProcessor>());
        prepare (*instances.back(), 48000.0, 128);
    }

    std::atomic<bool> allFinite { true };
    std::vector<std::thread> threads;
    for (int i = 0; i < kInstances; ++i)
        threads.emplace_back ([&, i]
        {
            juce::Random rng (100 + i);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buffer (2, 128);
            for (int k = 0; k < 400; ++k)
            {
                fillNoise (buffer, rng);
                instances[static_cast<size_t> (i)]->processBlock (buffer, midi);
                if (! bufferIsFinite (buffer))
                    allFinite = false;
            }
        });

    // Meanwhile, the "message thread" automates parameters and reads state.
    for (int k = 0; k < 50; ++k)
    {
        for (auto& inst : instances)
        {
            setPlain (*inst, ab::params::id::pitch, static_cast<float> (k % 25 - 12));
            setPlain (*inst, ab::params::id::reverbMode, static_cast<float> (k % 4));
            juce::MemoryBlock mb;
            inst->getStateInformation (mb);
        }
    }

    for (auto& t : threads)
        t.join();
    CHECK (allFinite.load());
}

// =============================================================================================
// Editor
// =============================================================================================
TEST_CASE ("Editor opens, scales with a fixed aspect ratio and remembers its size", "[plugin][editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    proc.setEditorWidth (1290);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() == 1290);
    CHECK (editor->getHeight() == 540);

    editor->setSize (645, 270);
    CHECK (proc.getEditorWidth() == 645);
    editor.reset();
}

TEST_CASE ("Editor snapshot (writes PNG to $AB_SNAPSHOT_DIR)", "[.snapshot]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = juce::SystemStats::getEnvironmentVariable ("AB_SNAPSHOT_DIR", {});
    REQUIRE (dir.isNotEmpty());

    AuroraBloomProcessor proc;
    prepare (proc, 48000.0, 512);

    // Feed some audio so the visualizer has a tail to draw.
    juce::Random rng (5);
    juce::MidiBuffer midi;
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setVisible (true);
    for (int frame = 0; frame < 120; ++frame)
    {
        for (int k = 0; k < 2; ++k)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            if (frame < 30)
                fillNoise (buffer, rng, 0.4f);
            else
                buffer.clear();
            proc.processBlock (buffer, midi);
        }
        if (auto* vis = dynamic_cast<ab::ui::DecayVisualizer*> (findById (*editor, "visualizer")))
            vis->advanceFrame();
    }

    for (const int width : { 860, 1720 })
    {
        editor->setSize (width, width * 360 / 860);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File file (juce::File (dir).getChildFile ("editor_" + juce::String (width) + ".png"));
        file.deleteFile();
        juce::FileOutputStream out (file);
        REQUIRE (out.openedOk());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, out));
    }
}
