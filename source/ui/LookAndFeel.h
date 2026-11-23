#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ab::ui
{

/** Aurora Bloom palette: night-blue background, aurora accent running from green-cyan to violet.
    Text colours keep a contrast ratio above 4.5:1 against the background. */
namespace palette
{
    inline const juce::Colour backgroundTop { 0xff0a0f1f };
    inline const juce::Colour backgroundBottom { 0xff121b36 };
    inline const juce::Colour panel { 0x0fffffff };
    inline const juce::Colour panelBorder { 0x1cffffff };
    inline const juce::Colour text { 0xffe9edf9 };
    inline const juce::Colour textDim { 0xff95a0c6 };
    inline const juce::Colour auroraGreen { 0xff3cf2c0 };
    inline const juce::Colour auroraCyan { 0xff4cc9f0 };
    inline const juce::Colour auroraViolet { 0xff8b6cff };
    inline const juce::Colour knobBody { 0xff18213f };
    inline const juce::Colour knobBodyHighlight { 0xff26325c };
    inline const juce::Colour track { 0x24ffffff };
    inline const juce::Colour focus { 0xff4cc9f0 };
} // namespace palette

class AuroraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AuroraLookAndFeel();

    juce::Font font (float height, bool bold = false) const;

    /** Horizontal aurora gradient between two points. */
    static juce::ColourGradient aurora (juce::Point<float> from, juce::Point<float> to, float alpha = 1.0f);

    // Typeface
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    // Rotary knobs
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    // Mode selectors ("title: value" combo boxes)
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

    // Buttons (preset bar)
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Misc
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

private:
    juce::Typeface::Ptr medium, bold;
};

} // namespace ab::ui
