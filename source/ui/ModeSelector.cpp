#include "ModeSelector.h"

#include "LookAndFeel.h"

namespace ab::ui
{

ModeSelector::ModeSelector (juce::RangedAudioParameter& parameter, const juce::String& title,
                            const juce::StringArray& labels, const juce::StringArray& tooltips)
    : attachment (parameter, [this] (float value) { select (static_cast<int> (std::lround (value))); })
{
    titleLabel.setText (title.toUpperCase(), juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (11.5f)));
    titleLabel.setColour (juce::Label::textColourId, palette::textDim);
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    setTitle (title);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);

    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (std::make_unique<juce::TextButton> (labels[i]));
        b->setClickingTogglesState (false);
        b->setTooltip (tooltips[i]);
        b->setTitle (title + ": " + labels[i]);
        b->setWantsKeyboardFocus (true);

        int edges = 0;
        if (i > 0)
            edges |= juce::Button::ConnectedOnLeft;
        if (i < labels.size() - 1)
            edges |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges (edges);

        b->onClick = [this, i] { attachment.setValueAsCompleteGesture (static_cast<float> (i)); };
        addAndMakeVisible (b);
    }

    attachment.sendInitialUpdate();
}

void ModeSelector::select (int index)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);
}

void ModeSelector::resized()
{
    auto bounds = getLocalBounds();
    titleLabel.setBounds (bounds.removeFromTop (16));
    bounds.removeFromTop (2);

    const int n = buttons.size();
    if (n == 0)
        return;

    const float w = static_cast<float> (bounds.getWidth()) / static_cast<float> (n);
    for (int i = 0; i < n; ++i)
    {
        const auto x0 = bounds.getX() + juce::roundToInt (w * static_cast<float> (i));
        const auto x1 = bounds.getX() + juce::roundToInt (w * static_cast<float> (i + 1));
        buttons[i]->setBounds (x0, bounds.getY(), x1 - x0 + (i < n - 1 ? 1 : 0), bounds.getHeight());
    }
}

} // namespace ab::ui
