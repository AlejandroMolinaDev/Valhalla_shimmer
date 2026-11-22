#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ab::ui
{

/** Rotary slider with the interaction model of the plugin:
      - vertical (or horizontal) drag, Ctrl/Cmd + drag = fine adjustment
      - double click = default value, mouse wheel, click on the value to type it
      - keyboard: arrows (Ctrl/Cmd = fine), Home/End, Delete = default
      - optional semitone snapping (Pitch): whole steps unless Shift is held (0.01 st). */
class KnobSlider final : public juce::Slider
{
public:
    KnobSlider();

    void setSnapToIntegers (bool shouldSnap) noexcept { snapToIntegers = shouldSnap; }
    void setDefaultValue (double v) { defaultValue = v; setDoubleClickReturnValue (true, v); }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    double snap (double value, const juce::ModifierKeys& mods) const;
    void nudge (double proportionDelta, const juce::ModifierKeys& mods);

    juce::Point<float> lastDragPos;
    double dragProportion = 0.0;
    double defaultValue = 0.0;
    bool snapToIntegers = false;
    bool dragging = false;
};

/** A labelled knob bound to one APVTS parameter. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
          const juce::String& tooltip, bool bipolar = false, bool snapToIntegers = false);

    void resized() override;
    KnobSlider& getSlider() noexcept { return slider; }

private:
    juce::Label titleLabel;
    KnobSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace ab::ui
