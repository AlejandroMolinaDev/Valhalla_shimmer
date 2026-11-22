#pragma once

#include "PluginProcessor.h"

class AuroraBloomEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AuroraBloomEditor (AuroraBloomProcessor&);
    ~AuroraBloomEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuroraBloomProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraBloomEditor)
};
