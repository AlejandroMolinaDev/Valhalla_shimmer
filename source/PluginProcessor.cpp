#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Identifier kStateType { "AuroraBloomState" };
    const juce::Identifier kEditorWidth { "editorWidth" };
    const juce::Identifier kPresetName { "presetName" };
} // namespace

AuroraBloomProcessor::AuroraBloomProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kStateType, ab::params::createLayout()),
      paramRefs (apvts),
      presetManager (apvts)
{
    for (const auto& id : ab::params::allIds())
        apvts.addParameterListener (id, this);
}

AuroraBloomProcessor::~AuroraBloomProcessor()
{
    for (const auto& id : ab::params::allIds())
        apvts.removeParameterListener (id, this);
}

void AuroraBloomProcessor::parameterChanged (const juce::String&, float)
{
    // May be called on any thread (host automation): only touches an atomic.
    presetManager.markModified();
}

void AuroraBloomProcessor::prepareToPlay (double sampleRate, int)
{
    // May be called repeatedly (FL Studio does so when switching audio devices or rendering);
    // the engine only reallocates when the sample rate actually changes.
    setLatencySamples (0);
    engine.setParameters (paramRefs.load());
    engine.prepare (sampleRate);

    meterInterval = juce::jmax (1, static_cast<int> (sampleRate / 100.0)); // 10 ms meter frames
    meterSumWetL = meterSumWetR = meterSumIn = 0.0;
    meterCount = 0;
}

void AuroraBloomProcessor::releaseResources() {}

bool AuroraBloomProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Output is always stereo; input may be mono or stereo (mono->stereo, stereo->stereo).
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AuroraBloomProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, false);
}

void AuroraBloomProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, true);
}

void AuroraBloomProcessor::processInternal (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();
    const auto numIn = getTotalNumInputChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return; // zero-length calls (parameter-only updates from some hosts)

    // Mono input bus: the second channel holds garbage, duplicate the first one.
    if (numIn == 1 && numChannels > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    for (auto ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (! engine.isPrepared())
        return; // processBlock before prepareToPlay: safe passthrough

    engine.setParameters (paramRefs.load());
    engine.setBypassed (bypassed);

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : left;
    engine.process (left, right, numSamples);

    pushMeters (numSamples);
}

void AuroraBloomProcessor::pushMeters (int numSamples) noexcept
{
    const auto n = static_cast<double> (numSamples);
    const auto wl = static_cast<double> (engine.getLastWetRms (0));
    const auto wr = static_cast<double> (engine.getLastWetRms (1));
    const auto in = static_cast<double> (engine.getLastInputRms());
    meterSumWetL += wl * wl * n;
    meterSumWetR += wr * wr * n;
    meterSumIn += in * in * n;
    meterCount += numSamples;

    if (meterCount < meterInterval)
        return;

    const auto inv = 1.0 / static_cast<double> (meterCount);
    const MeterFrame frame { static_cast<float> (std::sqrt (meterSumWetL * inv)),
                             static_cast<float> (std::sqrt (meterSumWetR * inv)),
                             static_cast<float> (std::sqrt (meterSumIn * inv)) };
    meterSumWetL = meterSumWetR = meterSumIn = 0.0;
    meterCount = 0;

    // Lock-free write; if the UI is not reading (editor closed) the frame is simply dropped.
    const auto scope = meterFifo.write (1);
    if (scope.blockSize1 > 0)
        meterBuffer[static_cast<size_t> (scope.startIndex1)] = frame;
    else if (scope.blockSize2 > 0)
        meterBuffer[static_cast<size_t> (scope.startIndex2)] = frame;
}

int AuroraBloomProcessor::popMeterFrames (MeterFrame* dest, int maxFrames) noexcept
{
    const auto scope = meterFifo.read (juce::jmin (maxFrames, meterFifo.getNumReady()));
    int count = 0;
    for (int i = 0; i < scope.blockSize1; ++i)
        dest[count++] = meterBuffer[static_cast<size_t> (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        dest[count++] = meterBuffer[static_cast<size_t> (scope.startIndex2 + i)];
    return count;
}

double AuroraBloomProcessor::getTailLengthSeconds() const
{
    // Generous estimate so FL Studio's render and Smart Disable never cut the tail.
    return ab::dsp::ShimmerEngine::estimateTailSeconds (paramRefs.load());
}

int AuroraBloomProcessor::getNumPrograms()
{
    return juce::jmax (1, presetManager.getNumFactoryPresets());
}

int AuroraBloomProcessor::getCurrentProgram()
{
    return juce::jmax (0, presetManager.getCurrentFactoryIndex());
}

const juce::String AuroraBloomProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, presetManager.getNumFactoryPresets()) ? presetManager.getFactoryName (index)
                                                                                 : juce::String ("Init");
}

void AuroraBloomProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, presetManager.getNumFactoryPresets()))
        return;

    // Factory presets come first in the preset list, so the program index is the preset index.
    presetManager.setCurrentFactoryIndex (index);
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        presetManager.loadPreset (index);
        return;
    }

    // Some hosts change programs from another thread: apply the preset on the message thread.
    juce::MessageManager::callAsync ([safe = juce::WeakReference<AuroraBloomProcessor> (this), index]
    {
        if (auto* p = safe.get())
            p->presetManager.loadPreset (index);
    });
}

juce::AudioProcessorEditor* AuroraBloomProcessor::createEditor()
{
    return new AuroraBloomEditor (*this);
}

void AuroraBloomProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kEditorWidth, editorWidth.load(), nullptr);
    state.setProperty (kPresetName, presetManager.getCurrentName(), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AuroraBloomProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    editorWidth = juce::jlimit (kDefaultEditorWidth * 3 / 4, kDefaultEditorWidth * 2,
                                static_cast<int> (state.getProperty (kEditorWidth, kDefaultEditorWidth)));
    const auto presetName = state.getProperty (kPresetName, "Init").toString();
    state.removeProperty (kEditorWidth, nullptr);
    state.removeProperty (kPresetName, nullptr);

    apvts.replaceState (state);
    presetManager.restoreCurrentName (presetName); // restoring is not a user edit of the preset
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuroraBloomProcessor();
}
