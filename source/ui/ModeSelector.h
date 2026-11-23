#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ab::ui
{

/** "title: value" text selector bound to an AudioParameterChoice. Clicking opens a menu with
    every option; the mouse wheel and the arrow keys step through them. Built on juce::ComboBox,
    so it is keyboard- and screen-reader-accessible. */
class ModeSelector final : public juce::Component
{
public:
    ModeSelector (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
                  const juce::StringArray& labels, const juce::String& tooltip);

    void resized() override;

    /** Width needed to show the title and the currently selected option. */
    int getIdealWidth() const;

    /** Called when the selected option (and therefore the ideal width) changes. */
    std::function<void()> onWidthChanged;

private:
    juce::Label titleLabel;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    juce::StringArray options;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
};

} // namespace ab::ui
