#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

/** Temporary editor (Phase 1-3): JUCE's generic parameter editor. */
class AuroraBloomEditor final : public juce::GenericAudioProcessorEditor
{
public:
    explicit AuroraBloomEditor (AuroraBloomProcessor& p) : GenericAudioProcessorEditor (p)
    {
        setSize (520, 460);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraBloomEditor)
};
