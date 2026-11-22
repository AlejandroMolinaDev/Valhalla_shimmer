#include "PluginProcessor.h"
#include "PluginEditor.h"

AuroraBloomProcessor::AuroraBloomProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AuroraBloomProcessor::prepareToPlay (double, int)
{
    setLatencySamples (0);
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
    juce::ScopedNoDenormals noDenormals;

    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    // Mono input: duplicate to the right channel. Passthrough for now (Phase 0).
    if (numIn == 1 && numOut > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    for (auto ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

juce::AudioProcessorEditor* AuroraBloomProcessor::createEditor()
{
    return new AuroraBloomEditor (*this);
}

void AuroraBloomProcessor::getStateInformation (juce::MemoryBlock&) {}

void AuroraBloomProcessor::setStateInformation (const void*, int) {}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuroraBloomProcessor();
}
