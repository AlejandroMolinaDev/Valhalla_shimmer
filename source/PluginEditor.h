#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/DecayVisualizer.h"
#include "ui/Knob.h"
#include "ui/LookAndFeel.h"
#include "ui/ModeSelector.h"
#include "ui/PresetBar.h"

/** Fixed 860 x 360 design canvas; the editor scales it (75 % - 200 %) with a fixed aspect ratio,
    so every element stays vector-sharp at any size.

    Layout: header (logo + preset bar), a slim tail visualizer, one row of nine knobs with small
    group captions (output / shimmer / space / tone / motion) and the three mode selectors as
    "title: value" text menus along the bottom. */
class AuroraBloomCanvas final : public juce::Component
{
public:
    explicit AuroraBloomCanvas (AuroraBloomProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Group
    {
        juce::String caption;
        int firstKnob;
        int numKnobs;
    };

    void paintLogo (juce::Graphics&, juce::Rectangle<float> area);
    void paintCaptions (juce::Graphics&);

    ab::ui::PresetBar presetBar;
    ab::ui::DecayVisualizer visualizer;

    ab::ui::Knob mix, pitch, feedback, diffusion, size, lowCut, highCut, modRate, modDepth;
    std::array<ab::ui::Knob*, 9> knobs;
    std::vector<Group> groups;
    juce::Rectangle<int> knobRow;

    ab::ui::ModeSelector reverbMode, pitchMode, colorMode;

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
