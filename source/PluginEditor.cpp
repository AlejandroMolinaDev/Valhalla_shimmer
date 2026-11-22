#include "PluginEditor.h"

namespace
{
    namespace id = ab::params::id;
    using ab::ui::palette::text;
    using ab::ui::palette::textDim;

    juce::RangedAudioParameter& param (AuroraBloomProcessor& p, const char* paramId)
    {
        auto* result = p.getApvts().getParameter (paramId);
        jassert (result != nullptr);
        return *result;
    }

    constexpr int kMargin = 16;
} // namespace

// =============================================================================================
AuroraBloomCanvas::AuroraBloomCanvas (AuroraBloomProcessor& p)
    : presetBar (p.getPresetManager()),
      visualizer (p),
      pitch (p.getApvts(), id::pitch, "Pitch",
             "Pitch shift applied inside the feedback loop, -12 to +12 semitones. Every recirculation is shifted "
             "again, so +12 stacks rising octaves. Drag snaps to semitones; hold Shift for 0.01 st steps.",
             true, true),
      size (p.getApvts(), id::size, "Size",
            "Scales every delay of the network. Changing it while the tail rings bends its pitch, like tape."),
      diffusion (p.getApvts(), id::diffusion, "Diffusion",
                 "Allpass coefficient. 0 = plain (ping-pong) delay, ~0.62 = slow bloom, 0.8-0.9 = dense and long, "
                 "above 0.91 = metallic and quieter."),
      feedback (p.getApvts(), id::feedback, "Feedback",
                "How much of the output returns into the loop (and through the pitch shifter). At 0 the pitch "
                "shift is not heard; the last 10 % gives near-infinite tails."),
      lowCut (p.getApvts(), id::lowCut, "Low Cut",
              "High-pass filter inside the loop (10 Hz - 2 kHz). Its effect accumulates: the tail gets thinner "
              "and brighter on every pass."),
      highCut (p.getApvts(), id::highCut, "High Cut",
               "Low-pass filter inside the loop (500 Hz - 20 kHz). Low values make the tail darker on every pass, "
               "like air absorption."),
      modRate (p.getApvts(), id::modRate, "Mod Rate",
               "Average speed of the random modulators (0.01 - 10 Hz), one per delay line."),
      modDepth (p.getApvts(), id::modDepth, "Mod Depth",
                "Modulation depth in samples. Adds chorus and detune, more so at high Diffusion; breaks up "
                "metallic resonances."),
      mix (p.getApvts(), id::mix, "Mix", "Dry/wet balance with an equal-power law (constant loudness).")
{
    addAndMakeVisible (presetBar);
    addAndMakeVisible (visualizer);

    groups = { { "Shimmer", { &pitch }, {} },
               { "Space", { &size, &diffusion, &feedback }, {} },
               { "Tone", { &lowCut, &highCut }, {} },
               { "Motion", { &modRate, &modDepth }, {} },
               { "Output", { &mix }, {} } };

    for (auto& group : groups)
        for (auto* knob : group.knobs)
            addAndMakeVisible (knob);

    reverbMode = std::make_unique<ab::ui::ModeSelector> (
        param (p, id::reverbMode), "Reverb Mode", juce::StringArray { "Mono", "Big", "Medium", "Small" },
        juce::StringArray { "Mono in, stereo out. The largest space with the slowest fade-in; pitch shifting is most "
                            "pronounced.",
                            "Very large stereo space: cathedral-like, slow fades, smooth shimmer.",
                            "Classic stereo hall. Harmonics build up faster.",
                            "Small and less dense: more coloured, great for spring-like, chorus and short ambiences." });

    pitchMode = std::make_unique<ab::ui::ModeSelector> (
        param (p, id::pitchMode), "Pitch Mode", juce::StringArray { "Single", "Dual", "Single Rev", "Dual Rev", "Bypass" },
        juce::StringArray { "One shifter at the Pitch setting.",
                            "Two shifters, +Pitch and -Pitch, at -3 dB each.",
                            "Grains are played backwards before shifting: fewer transients, organ-like texture.",
                            "Dual with reversed grains: the smoothest option.",
                            "No pitch shifting: a dense conventional reverb where Feedback only lengthens the tail." });

    colorMode = std::make_unique<ab::ui::ModeSelector> (
        param (p, id::colorMode), "Color", juce::StringArray { "Bright", "Dark" },
        juce::StringArray { "Full bandwidth: only High Cut limits the top end.",
                            "Steep ~10 kHz low-pass inside the loop, like late-70s digital reverbs; also tames "
                            "pitch-shift aliasing." });

    addAndMakeVisible (*reverbMode);
    addAndMakeVisible (*pitchMode);
    addAndMakeVisible (*colorMode);

    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
}

void AuroraBloomCanvas::resized()
{
    auto bounds = getLocalBounds().reduced (kMargin, 0);

    // Top bar
    auto top = bounds.removeFromTop (56).withTrimmedTop (12).withTrimmedBottom (12);
    presetBar.setBounds (top.removeFromRight (400));

    // Visualizer
    visualizer.setBounds (bounds.removeFromTop (100));
    bounds.removeFromTop (10);

    // Knob groups: width proportional to the number of knobs
    auto knobRow = bounds.removeFromTop (172);
    const int gap = 8;
    const int totalKnobs = 9;
    const int knobWidth = (knobRow.getWidth() - gap * (static_cast<int> (groups.size()) - 1)) / totalKnobs;
    for (auto& group : groups)
    {
        const int w = knobWidth * static_cast<int> (group.knobs.size());
        group.bounds = knobRow.removeFromLeft (w);
        knobRow.removeFromLeft (gap);

        auto inner = group.bounds.withTrimmedTop (24).withTrimmedBottom (10);
        for (auto* knob : group.knobs)
            knob->setBounds (inner.removeFromLeft (knobWidth).reduced (3, 0));
    }

    // Mode selectors
    bounds.removeFromTop (14);
    auto modes = bounds.removeFromTop (54);
    reverbMode->setBounds (modes.removeFromLeft (280).withTrimmedBottom (8));
    modes.removeFromLeft (16);
    colorMode->setBounds (modes.removeFromRight (140).withTrimmedBottom (8));
    modes.removeFromRight (16);
    pitchMode->setBounds (modes.withTrimmedBottom (8));
}

void AuroraBloomCanvas::paint (juce::Graphics& g)
{
    using namespace ab::ui;
    const auto bounds = getLocalBounds().toFloat();

    // Night-blue background with two faint aurora glows
    g.setGradientFill (juce::ColourGradient (palette::backgroundTop, 0.0f, 0.0f, palette::backgroundBottom, 0.0f,
                                             bounds.getBottom(), false));
    g.fillAll();
    {
        juce::ColourGradient glow (palette::auroraGreen.withAlpha (0.10f), bounds.getWidth() * 0.12f, -40.0f,
                                   palette::auroraGreen.withAlpha (0.0f), bounds.getWidth() * 0.12f, 260.0f, true);
        g.setGradientFill (glow);
        g.fillRect (bounds);
        juce::ColourGradient glow2 (palette::auroraViolet.withAlpha (0.10f), bounds.getWidth() * 0.9f, -20.0f,
                                    palette::auroraViolet.withAlpha (0.0f), bounds.getWidth() * 0.9f, 300.0f, true);
        g.setGradientFill (glow2);
        g.fillRect (bounds);
    }

    paintLogo (g, { static_cast<float> (kMargin), 10.0f, 300.0f, 40.0f });

    // Group panels
    auto* lnf = dynamic_cast<AuroraLookAndFeel*> (&getLookAndFeel());
    for (const auto& group : groups)
    {
        const auto r = group.bounds.toFloat();
        g.setColour (palette::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (palette::panelBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        // Title with a short aurora underline
        if (lnf != nullptr)
            g.setFont (lnf->font (11.0f, true));
        const auto titleArea = r.withHeight (22.0f).withTrimmedTop (6.0f);
        g.setColour (text.withAlpha (0.85f));
        g.drawText (group.title.toUpperCase(), titleArea, juce::Justification::centred);
        const float lineW = 18.0f;
        g.setGradientFill (AuroraLookAndFeel::aurora ({ r.getCentreX() - lineW, 0.0f }, { r.getCentreX() + lineW, 0.0f }));
        g.fillRoundedRectangle (juce::Rectangle<float> (lineW * 2.0f, 2.0f).withCentre ({ r.getCentreX(), titleArea.getBottom() + 1.0f }),
                                1.0f);
    }
}

void AuroraBloomCanvas::paintLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    using namespace ab::ui;
    // Glyph: three concentric arcs opening upwards, like light blooming over a horizon.
    const float s = area.getHeight();
    const auto glyph = area.removeFromLeft (s).reduced (4.0f);
    const auto c = juce::Point<float> (glyph.getCentreX(), glyph.getBottom() - 4.0f);
    for (int i = 0; i < 3; ++i)
    {
        const float r = glyph.getWidth() * (0.2f + 0.16f * static_cast<float> (i));
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0.0f, -juce::MathConstants<float>::halfPi * 0.95f,
                           juce::MathConstants<float>::halfPi * 0.95f, true);
        g.setGradientFill (AuroraLookAndFeel::aurora (glyph.getBottomLeft(), glyph.getTopRight(),
                                                      1.0f - 0.22f * static_cast<float> (i)));
        g.strokePath (arc, juce::PathStrokeType (2.6f - 0.5f * static_cast<float> (i), juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }
    g.setColour (palette::text);
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c));

    area.removeFromLeft (8.0f);
    if (auto* lnf = dynamic_cast<AuroraLookAndFeel*> (&getLookAndFeel()))
    {
        const auto boldFont = lnf->font (19.0f, true).withExtraKerningFactor (0.12f);
        const auto lightFont = lnf->font (19.0f).withExtraKerningFactor (0.12f);
        g.setFont (boldFont);
        g.setColour (palette::text);
        const auto auroraWidth = juce::GlyphArrangement::getStringWidth (boldFont, "AURORA");
        g.drawText ("AURORA", area, juce::Justification::centredLeft);
        g.setFont (lightFont);
        g.setGradientFill (AuroraLookAndFeel::aurora (area.getTopLeft().translated (auroraWidth, 0.0f),
                                                      area.getTopLeft().translated (auroraWidth + 80.0f, 0.0f)));
        g.drawText ("BLOOM", area.withTrimmedLeft (auroraWidth + 6.0f), juce::Justification::centredLeft);
    }
}

// =============================================================================================
AuroraBloomEditor::AuroraBloomEditor (AuroraBloomProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), canvas (p)
{
    // Read the saved size first: setResizeLimits() below resizes (and so re-saves) immediately.
    const int savedWidth = processorRef.getEditorWidth();

    // Add the canvas first so that setLookAndFeel() propagates to every child (sliders rebuild
    // their value boxes with our look and feel).
    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, kBaseWidth, kBaseHeight);
    setLookAndFeel (&lookAndFeel);
    tooltips.setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (static_cast<double> (kBaseWidth) / static_cast<double> (kBaseHeight));

    const int width = juce::jlimit (kBaseWidth * 3 / 4, kBaseWidth * 2, savedWidth);
    setSize (width, juce::roundToInt (static_cast<double> (width) * kBaseHeight / kBaseWidth));
}

AuroraBloomEditor::~AuroraBloomEditor()
{
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AuroraBloomEditor::paint (juce::Graphics& g)
{
    g.fillAll (ab::ui::palette::backgroundTop);
}

void AuroraBloomEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (kBaseWidth);
    canvas.setTransform (juce::AffineTransform::scale (scale));
    processorRef.setEditorWidth (getWidth());
}
