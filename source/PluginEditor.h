#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/DecayVisualizer.h"
#include "ui/Knob.h"
#include "ui/LookAndFeel.h"
#include "ui/ModeSelector.h"
#include "ui/PresetBar.h"

/** Fixed 760 x 420 design canvas; the editor scales it (75 % - 200 %) with a fixed aspect ratio,
    so every element stays vector-sharp at any size. */
class AuroraBloomCanvas final : public juce::Component
{
public:
    explicit AuroraBloomCanvas (AuroraBloomProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Group
    {
        juce::String title;
        std::vector<ab::ui::Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    void paintLogo (juce::Graphics&, juce::Rectangle<float> area);

    ab::ui::PresetBar presetBar;
    ab::ui::DecayVisualizer visualizer;

    ab::ui::Knob pitch, size, diffusion, feedback, lowCut, highCut, modRate, modDepth, mix;
    std::vector<Group> groups;

    std::unique_ptr<ab::ui::ModeSelector> reverbMode, pitchMode, colorMode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraBloomCanvas)
};

class AuroraBloomEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int kBaseWidth = AuroraBloomProcessor::kDefaultEditorWidth;
    static constexpr int kBaseHeight = AuroraBloomProcessor::kDefaultEditorHeight;

    explicit AuroraBloomEditor (AuroraBloomProcessor&);
    ~AuroraBloomEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuroraBloomProcessor& processorRef;
    ab::ui::AuroraLookAndFeel lookAndFeel;
    AuroraBloomCanvas canvas;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraBloomEditor)
};
