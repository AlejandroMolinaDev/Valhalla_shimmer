#include "Knob.h"

#include "LookAndFeel.h"

namespace ab::ui
{

namespace
{
    constexpr double kPixelsForFullRange = 220.0;
    constexpr double kFineFactor = 6.0;
} // namespace

KnobSlider::KnobSlider()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    setTextBoxIsEditable (true);
    setWantsKeyboardFocus (true);           // reachable with Tab...
    setMouseClickGrabsKeyboardFocus (false); // ...but a click never steals FL Studio's shortcuts
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setScrollWheelEnabled (true);
}

double KnobSlider::snap (double value, const juce::ModifierKeys& mods) const
{
    if (snapToIntegers && ! mods.isShiftDown())
        return std::round (value);
    return value;
}

void KnobSlider::mouseDown (const juce::MouseEvent& e)
{
    // Let juce::Slider start the host gesture (beginChangeGesture) and handle popups/double clicks.
    Slider::mouseDown (e);
    lastDragPos = e.position;
    dragProportion = valueToProportionOfLength (getValue());
    dragging = true;
}

void KnobSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! dragging || e.mods.isPopupMenu())
        return;

    // Relative drag (up/right increases), re-anchored every event, so switching to fine mode
    // mid-drag never makes the value jump.
    const auto delta = (lastDragPos.y - e.position.y) + (e.position.x - lastDragPos.x);
    lastDragPos = e.position;

    // Ctrl/Cmd = fine. On a semitone-snapping knob (Pitch) Shift selects 0.01 st steps, so it
    // also slows the drag down to make those steps reachable.
    const bool fine = e.mods.isCommandDown() || (snapToIntegers && e.mods.isShiftDown());
    const double pixels = kPixelsForFullRange * (fine ? kFineFactor : 1.0);
    dragProportion = juce::jlimit (0.0, 1.0, dragProportion + static_cast<double> (delta) / pixels);
    setValue (snap (proportionOfLengthToValue (dragProportion), e.mods), juce::sendNotificationSync);
}

void KnobSlider::mouseUp (const juce::MouseEvent& e)
{
    dragging = false;
    Slider::mouseUp (e); // ends the host gesture
}

void KnobSlider::nudge (double proportionDelta, const juce::ModifierKeys& mods)
{
    if (snapToIntegers && ! mods.isShiftDown())
    {
        const auto step = proportionDelta > 0.0 ? 1.0 : -1.0;
        setValue (std::round (getValue()) + step, juce::sendNotificationSync);
        return;
    }

    const auto p = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + proportionDelta);
    setValue (snap (proportionOfLengthToValue (p), mods), juce::sendNotificationSync);
}

void KnobSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! isEnabled() || ! isScrollWheelEnabled())
        return;

    const float raw = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
    const float amount = wheel.isReversed ? -raw : raw;
    if (std::abs (amount) < 1.0e-4f)
        return;

    const double perUnit = e.mods.isCommandDown() ? 0.04 : 0.25;
    nudge (static_cast<double> (amount) * perUnit, e.mods);
}

bool KnobSlider::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const double step = mods.isCommandDown() ? 0.002 : 0.01;
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
        nudge (step, mods);
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
        nudge (-step, mods);
    else if (code == juce::KeyPress::pageUpKey)
        nudge (0.1, mods);
    else if (code == juce::KeyPress::pageDownKey)
        nudge (-0.1, mods);
    else if (code == juce::KeyPress::homeKey)
        setValue (getMinimum(), juce::sendNotificationSync);
    else if (code == juce::KeyPress::endKey)
        setValue (getMaximum(), juce::sendNotificationSync);
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        setValue (defaultValue, juce::sendNotificationSync);
    else
        return Slider::keyPressed (key);

    return true;
}

// =============================================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
            const juce::String& tooltip, bool bipolar, bool snapToIntegers)
{
    titleLabel.setText (title.toLowerCase(), juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::Font (juce::FontOptions (15.0f)));
    titleLabel.setColour (juce::Label::textColourId, palette::text);
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    slider.getProperties().set ("bipolar", bipolar);
    slider.setSnapToIntegers (snapToIntegers);
    slider.setTooltip (tooltip);
    slider.setTitle (title);
    slider.setDescription (tooltip);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* param = state.getParameter (paramId))
        slider.setDefaultValue (static_cast<double> (param->convertFrom0to1 (param->getDefaultValue())));
}

void Knob::setAccent (juce::Colour colour)
{
    slider.getProperties().set ("accent", static_cast<juce::int64> (colour.getARGB()));
    slider.repaint();
}

void Knob::resized()
{
    auto bounds = getLocalBounds();
    titleLabel.setBounds (bounds.removeFromTop (20));
    slider.setBounds (bounds);
}

} // namespace ab::ui
