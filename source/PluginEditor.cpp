#include "PluginEditor.h"

AuroraBloomEditor::AuroraBloomEditor (AuroraBloomProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    juce::ignoreUnused (processorRef);
    setSize (760, 420);
}

void AuroraBloomEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b1020));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f));
    g.drawFittedText ("Aurora Bloom", getLocalBounds(), juce::Justification::centred, 1);
}

void AuroraBloomEditor::resized() {}
