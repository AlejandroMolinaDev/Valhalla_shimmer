#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/ShimmerEngine.h"

/** Level snapshot sent from the audio thread to the visualizer (lock-free FIFO). */
struct MeterFrame
{
    float wetL = 0.0f;
    float wetR = 0.0f;
    float input = 0.0f;
};

class AuroraBloomProcessor final : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kDefaultEditorWidth = 760;
    static constexpr int kDefaultEditorHeight = 420;

    AuroraBloomProcessor();
    ~AuroraBloomProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return presetManager; }

    /** Pops up to maxFrames meter frames (message thread). Returns the number read. */
    int popMeterFrames (MeterFrame* dest, int maxFrames) noexcept;

    int getEditorWidth() const noexcept { return editorWidth.load(); }
    void setEditorWidth (int width) noexcept { editorWidth = width; }

private:
    void processInternal (juce::AudioBuffer<float>& buffer, bool bypassed);
    void pushMeters (int numSamples) noexcept;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::AudioProcessorValueTreeState apvts;
    ab::params::ParameterRefs paramRefs;
    ab::dsp::ShimmerEngine engine;
    PresetManager presetManager;

    // Meter FIFO (single producer: audio thread, single consumer: UI timer).
    static constexpr int kMeterFifoSize = 512;
    juce::AbstractFifo meterFifo { kMeterFifoSize };
    std::array<MeterFrame, kMeterFifoSize> meterBuffer {};
    double meterSumWetL = 0.0, meterSumWetR = 0.0, meterSumIn = 0.0;
    int meterCount = 0, meterInterval = 480;

    std::atomic<int> editorWidth { kDefaultEditorWidth };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraBloomProcessor)
};
