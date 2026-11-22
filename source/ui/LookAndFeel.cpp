#include "LookAndFeel.h"

#include "BinaryData.h"

namespace ab::ui
{

AuroraLookAndFeel::AuroraLookAndFeel()
{
    medium = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskMedium_ttf,
                                                      BinaryData::SpaceGroteskMedium_ttfSize);
    bold = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskBold_ttf,
                                                    BinaryData::SpaceGroteskBold_ttfSize);

    using namespace palette;
    setColour (juce::ResizableWindow::backgroundColourId, backgroundTop);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, auroraCyan.withAlpha (0.35f));
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, auroraCyan);
    setColour (juce::TextButton::textColourOffId, textDim);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::TextEditor::backgroundColourId, knobBody);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, auroraCyan.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, text);
    setColour (juce::TextEditor::outlineColourId, panelBorder);
    setColour (juce::TextEditor::focusedOutlineColourId, focus);
    setColour (juce::CaretComponent::caretColourId, auroraGreen);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf2111933));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, auroraGreen);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, auroraViolet.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff111933));
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, panelBorder);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf20d1429));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, auroraCyan.withAlpha (0.4f));
}

juce::Font AuroraLookAndFeel::font (float height, bool isBold) const
{
    return juce::Font (juce::FontOptions (isBold ? bold : medium).withHeight (height));
}

juce::ColourGradient AuroraLookAndFeel::aurora (juce::Point<float> from, juce::Point<float> to, float alpha)
{
    juce::ColourGradient g (palette::auroraGreen.withMultipliedAlpha (alpha), from,
                            palette::auroraViolet.withMultipliedAlpha (alpha), to, false);
    g.addColour (0.5, palette::auroraCyan.withMultipliedAlpha (alpha));
    return g;
}

juce::Typeface::Ptr AuroraLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return f.isBold() ? bold : medium;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

// =============================================================================================
void AuroraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace palette;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area = bounds.withSizeKeepingCentre (size, size).reduced (5.0f);
    const auto centre = area.getCentre();
    const float radius = area.getWidth() * 0.5f;
    const float arcRadius = radius - 3.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool bipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolar", false));
    const bool enabled = slider.isEnabled();

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (track.isEmpty() ? juce::Colours::transparentBlack : palette::track);
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (from the centre for bipolar parameters such as Pitch)
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle),
                             juce::jmax (from, angle), true);
        auto grad = aurora (area.getBottomLeft(), area.getTopRight(), enabled ? 1.0f : 0.35f);
        g.setGradientFill (grad);
        g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Soft glow at the tip
        const auto tip = centre.getPointOnCircumference (arcRadius, angle);
        g.setColour (auroraCyan.withAlpha (enabled ? 0.25f : 0.08f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (tip));
    }

    // Body
    const float bodyRadius = radius * 0.70f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (body.translated (0.0f, 2.0f).expanded (1.0f));
    g.setGradientFill (juce::ColourGradient (knobBodyHighlight, body.getX(), body.getY(), knobBody,
                                             body.getX(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Pointer
    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (bodyRadius * 0.35f, angle));
    pointer.lineTo (centre.getPointOnCircumference (bodyRadius * 0.82f, angle));
    g.setColour (enabled ? text : textDim);
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Keyboard focus ring
    if (slider.hasKeyboardFocus (true))
    {
        g.setColour (focus.withAlpha (0.9f));
        g.drawEllipse (area.expanded (2.5f), 1.5f);
    }
}

juce::Slider::SliderLayout AuroraLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    // Square knob with the value right underneath, the pair centred vertically.
    juce::Slider::SliderLayout layout;
    const auto bounds = slider.getLocalBounds();
    constexpr int textHeight = 18;
    const int knob = juce::jmin (bounds.getWidth(), bounds.getHeight() - textHeight);
    const int top = bounds.getY() + (bounds.getHeight() - (knob + textHeight + 2)) / 2;
    layout.sliderBounds = { bounds.getX(), top, bounds.getWidth(), knob };
    layout.textBoxBounds = { bounds.getX(), top + knob + 2, bounds.getWidth(), textHeight };
    return layout;
}

juce::Label* AuroraLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (font (13.0f));
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, palette::text);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundWhenEditingColourId, palette::knobBody);
    label->setColour (juce::Label::outlineWhenEditingColourId, palette::focus);
    label->setColour (juce::Label::textWhenEditingColourId, palette::text);
    label->setMouseCursor (juce::MouseCursor::IBeamCursor);
    return label;
}

// =============================================================================================
void AuroraLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                              bool isMouseOver, bool isDown)
{
    using namespace palette;
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 6.0f;
    const bool on = button.getToggleState();

    const bool flatLeft = button.isConnectedOnLeft();
    const bool flatRight = button.isConnectedOnRight();

    juce::Path shape;
    shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), corner, corner,
                               ! flatLeft, ! flatRight, ! flatLeft, ! flatRight);

    if (on)
    {
        g.setGradientFill (aurora (bounds.getTopLeft(), bounds.getBottomRight(), 0.28f));
        g.fillPath (shape);
        g.setGradientFill (aurora (bounds.getTopLeft(), bounds.getBottomRight(), 0.9f));
        g.strokePath (shape, juce::PathStrokeType (1.2f));
    }
    else
    {
        g.setColour (panel.withMultipliedAlpha (isDown ? 2.2f : (isMouseOver ? 1.7f : 1.0f)));
        g.fillPath (shape);
        g.setColour (panelBorder);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    if (button.hasKeyboardFocus (true))
    {
        g.setColour (focus);
        g.strokePath (shape, juce::PathStrokeType (1.6f));
    }
}

void AuroraLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOver, bool)
{
    const bool on = button.getToggleState();
    auto colour = on ? palette::text : (isMouseOver ? palette::text.withAlpha (0.9f) : palette::textDim);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1,
                      0.85f);
}

juce::Font AuroraLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (13.0f, static_cast<float> (buttonHeight) * 0.52f));
}

juce::Font AuroraLookAndFeel::getLabelFont (juce::Label& label)
{
    return font (label.getFont().getHeight(), label.getFont().isBold());
}

juce::Font AuroraLookAndFeel::getPopupMenuFont() { return font (14.0f); }
juce::Font AuroraLookAndFeel::getAlertWindowTitleFont() { return font (17.0f, true); }
juce::Font AuroraLookAndFeel::getAlertWindowMessageFont() { return font (14.0f); }
juce::Font AuroraLookAndFeel::getAlertWindowFont() { return font (13.0f); }

juce::Rectangle<int> AuroraLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    juce::TextLayout layout;
    juce::AttributedString s;
    s.append (tipText, font (13.0f), palette::text);
    layout.createLayoutWithBalancedLineLengths (s, 260.0f);
    const auto w = static_cast<int> (layout.getWidth() + 18.0f);
    const auto h = static_cast<int> (layout.getHeight() + 12.0f);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void AuroraLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    juce::AttributedString s;
    s.append (text, font (13.0f), palette::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 260.0f);
    layout.draw (g, bounds.reduced (9.0f, 6.0f));
}

void AuroraLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (palette::knobBody);
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), 4.0f);
}

void AuroraLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? palette::focus : palette::panelBorder);
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), 4.0f, 1.0f);
}

} // namespace ab::ui
