#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ab::ui
{

/** Segmented button bar bound to an AudioParameterChoice. */
class ModeSelector final : public juce::Component
{
public:
    ModeSelector (juce::RangedAudioParameter& parameter, const juce::String& title, const juce::StringArray& labels,
                  const juce::StringArray& tooltips);

    void resized() override;

private:
    void select (int index);

    juce::Label titleLabel;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
};

} // namespace ab::ui
