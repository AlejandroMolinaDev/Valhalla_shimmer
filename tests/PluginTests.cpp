#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginProcessor.h"

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
}

TEST_CASE ("Plugin processes a block without crashing", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    AuroraBloomProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    juce::MidiBuffer midi;
    proc.processBlock (buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));

    proc.releaseResources();
}
