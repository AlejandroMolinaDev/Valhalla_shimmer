#include "ModeSelector.h"

#include "LookAndFeel.h"

namespace ab::ui
{

namespace
{
    constexpr float kFontHeight = 15.0f;
    constexpr int kArrowSpace = 18;

    juce::Font selectorFont (const juce::Component& c)
    {
        if (auto* lnf = dynamic_cast<AuroraLookAndFeel*> (&c.getLookAndFeel()))
            return lnf->font (kFontHeight);
        return juce::Font (juce::FontOptions (kFontHeight));
    }
} // namespace

ModeSelector::ModeSelector (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                            const juce::String& title, const juce::StringArray& labels, const juce::String& tooltip)
    : options (labels)
{
    titleLabel.setText (title.toLowerCase() + ":", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (kFontHeight)));
    titleLabel.setColour (juce::Label::textColourId, palette::text);
    titleLabel.setJustificationType (juce::Justification::centredRight);
    titleLabel.setBorderSize ({ 0, 0, 0, 6 });
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    box.addItemList (labels, 1);
    box.setTooltip (tooltip);
    box.setTitle (title);
    box.setDescription (tooltip);
    box.setJustificationType (juce::Justification::centredLeft);
    box.setScrollWheelEnabled (true);
    box.setWantsKeyboardFocus (true);
    box.setMouseClickGrabsKeyboardFocus (false); // never steal FL Studio's keyboard shortcuts
    box.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (box);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
    box.onChange = [this]
    {
        if (onWidthChanged != nullptr)
            onWidthChanged();
    };
    setTitle (title);
}

int ModeSelector::getIdealWidth() const
{
    const auto f = selectorFont (*this);
    const auto valueWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, box.getText()));
    const auto titleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, titleLabel.getText()));
    return titleWidth + 8 + valueWidth + kArrowSpace + 4;
}

void ModeSelector::resized()
{
    auto bounds = getLocalBounds();
    const auto f = selectorFont (*this);
    const auto titleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, titleLabel.getText())) + 8;
    titleLabel.setBounds (bounds.removeFromLeft (titleWidth));
    box.setBounds (bounds);
}

} // namespace ab::ui
