#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Identifier kStateType { "AuroraBloomState" };
}

AuroraBloomProcessor::AuroraBloomProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kStateType, ab::params::createLayout()),
      paramRefs (apvts)
{
}

void AuroraBloomProcessor::prepareToPlay (double sampleRate, int)
{
    // May be called repeatedly (FL Studio does so when switching audio devices or rendering);
    // the engine only reallocates when the sample rate actually changes.
    setLatencySamples (0);
    engine.setParameters (paramRefs.load());
    engine.prepare (sampleRate);
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
        return; // e.g. FL Studio's zero-length parameter flush calls

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
}

double AuroraBloomProcessor::getTailLengthSeconds() const
{
    // Generous estimate so FL Studio's render and Smart Disable never cut the tail.
    return ab::dsp::ShimmerEngine::estimateTailSeconds (paramRefs.load());
}

juce::AudioProcessorEditor* AuroraBloomProcessor::createEditor()
{
    return new AuroraBloomEditor (*this);
}

void AuroraBloomProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AuroraBloomProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuroraBloomProcessor();
}
