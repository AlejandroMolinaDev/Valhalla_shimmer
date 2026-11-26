#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AuroraBloomProcessor;

namespace ab::ui
{

/** Animated view of the tail: scrolling wet-level envelope on the left and, on the right, the
    stacked pitch layers the loop is building (rising for positive Pitch, falling for negative,
    both for Dual, softened for the Reverse modes, flat when the shifter is bypassed).

    Reads levels from the processor through its lock-free FIFO; runs on the UI thread at most at
    60 fps and stops its timer whenever it is not on screen. */
class DecayVisualizer final : public juce::Component, private juce::Timer
{
public:
    explicit DecayVisualizer (AuroraBloomProcessor& processor);
    ~DecayVisualizer() override;

    void paint (juce::Graphics&) override;
    void visibilityChanged() override { updateTimer(); }

    /** Pulls new meter frames and advances the animation by one frame (called by the timer;
        public so tests can drive it without a message loop). */
    void advanceFrame();
    void parentHierarchyChanged() override { updateTimer(); }

    /** Smoothed wet level shown on the right edge of the envelope, 0..1 (-60..0 dBFS). */
    float getDisplayLevel() const noexcept { return level; }
    bool isAnimating() const noexcept { return isTimerRunning(); }

private:
    void timerCallback() override;
    void updateTimer();
    void paintEnvelope (juce::Graphics&, juce::Rectangle<float> area);
    void paintLayers (juce::Graphics&, juce::Rectangle<float> area);

    AuroraBloomProcessor& processor;
    std::atomic<float>* pitch;
    std::atomic<float>* feedback;
    std::atomic<float>* pitchMode;

    static constexpr int kHistory = 300;
    std::array<float, kHistory> history {};
    std::array<float, kHistory> inputHistory {};
    int head = 0;
    float level = 0.0f;
    double phase = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecayVisualizer)
};

} // namespace ab::ui
