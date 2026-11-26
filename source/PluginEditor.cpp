#include "PluginEditor.h"

namespace
{
    namespace id = ab::params::id;
    using ab::ui::palette::text;
    using ab::ui::palette::textDim;

    constexpr int kMargin = 24;
    constexpr int kHeaderY = 14;
    constexpr int kHeaderHeight = 36;
    constexpr int kVisualizerY = 62;
    constexpr int kVisualizerHeight = 60;
    constexpr int kCaptionY = 134;
    constexpr int kCaptionHeight = 14;
    constexpr int kKnobRowY = 152;
    constexpr int kKnobRowHeight = 140;
    constexpr int kModeRowY = 304;
    constexpr int kModeRowHeight = 30;
} // namespace

// =============================================================================================
AuroraBloomCanvas::AuroraBloomCanvas (AuroraBloomProcessor& p)
    : presetBar (p.getPresetManager()),
      visualizer (p),
      mix (p.getApvts(), id::mix, "Mix", "Dry/wet balance with an equal-power law (constant loudness)."),
      pitch (p.getApvts(), id::pitch, "Pitch",
             "Pitch shift applied inside the feedback loop, -12 to +12 semitones. Every recirculation is shifted "
             "again, so +12 stacks rising octaves. Drag snaps to semitones; hold Shift for 0.01 st steps.",
             true, true),
      feedback (p.getApvts(), id::feedback, "Feedback",
                "How much of the output returns into the loop (and through the pitch shifter). At 0 the pitch "
                "shift is not heard; the last 10 % gives near-infinite tails."),
      diffusion (p.getApvts(), id::diffusion, "Diffusion",
                 "Allpass coefficient. 0 = plain (ping-pong) delay, ~0.62 = slow bloom, 0.8-0.9 = dense and long, "
                 "above 0.91 = metallic and quieter."),
      size (p.getApvts(), id::size, "Size",
            "Scales every delay of the network. Changing it while the tail rings bends its pitch, like tape."),
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
      knobs { &mix, &pitch, &feedback, &diffusion, &size, &lowCut, &highCut, &modRate, &modDepth },
      groups { { "output", 0, 1 }, { "shimmer", 1, 1 }, { "space", 2, 3 }, { "tone", 5, 2 }, { "motion", 7, 2 } },
      reverbMode (p.getApvts(), id::reverbMode, "Reverb Mode",
                  juce::StringArray { "mono", "big stereo", "medium stereo", "small stereo" },
                  "Mono: mono in / stereo out, the largest space with the slowest fade-in and the most pronounced "
                  "pitch shifting. Big Stereo: cathedral-like, slow fades, smooth shimmer. Medium Stereo: classic "
                  "hall, harmonics build up faster. Small Stereo: smaller and less dense, more coloured (springs, "
                  "chorus, short ambiences)."),
      pitchMode (p.getApvts(), id::pitchMode, "Pitch Mode",
                 juce::StringArray { "single", "dual", "single reverse", "dual reverse", "bypass" },
                 "Single: one shifter at the Pitch setting. Dual: +Pitch and -Pitch at -3 dB each. Reverse modes "
                 "play each grain backwards before shifting (fewer transients, organ-like). Bypass: no pitch "
                 "shifting, a dense conventional reverb."),
      colorMode (p.getApvts(), id::colorMode, "Color Mode", juce::StringArray { "bright", "dark" },
                 "Bright: full bandwidth, only High Cut limits the top end. Dark: steep ~10 kHz low-pass inside "
                 "the loop, like late-70s digital reverbs; also tames pitch-shift aliasing.")
{
    presetBar.setComponentID ("presetBar");
    setComponentID ("canvas");
    addAndMakeVisible (presetBar);
    addAndMakeVisible (visualizer);

    // The row of knobs runs through the aurora gradient, green-cyan to violet.
    const auto gradient = ab::ui::AuroraLookAndFeel::aurora ({ 0.0f, 0.0f }, { 1.0f, 0.0f });
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->setAccent (gradient.getColourAtPosition (static_cast<double> (i) / static_cast<double> (knobs.size() - 1)));
        addAndMakeVisible (knobs[i]);
    }

    for (auto* selector : { &reverbMode, &pitchMode, &colorMode })
    {
        selector->onWidthChanged = [this] { resized(); };
        addAndMakeVisible (selector);
    }

    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
}

void AuroraBloomCanvas::resized()
{
    const int width = getWidth();
    const int inner = width - 2 * kMargin;

    // Header: logo on the left (painted), preset bar on the right.
    presetBar.setBounds (width - kMargin - 420, kHeaderY + 3, 420, kHeaderHeight - 6);

    visualizer.setBounds (kMargin, kVisualizerY, inner, kVisualizerHeight);

    // One row of nine equal knob cells.
    knobRow = { kMargin, kKnobRowY, inner, kKnobRowHeight };
    const float cell = static_cast<float> (inner) / static_cast<float> (knobs.size());
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto x0 = kMargin + juce::roundToInt (cell * static_cast<float> (i));
        const auto x1 = kMargin + juce::roundToInt (cell * static_cast<float> (i + 1));
        knobs[i]->setBounds (x0, kKnobRowY, x1 - x0, kKnobRowHeight);
    }

    // Mode selectors: left, centre and right aligned along the bottom.
    const int rw = reverbMode.getIdealWidth();
    const int pw = pitchMode.getIdealWidth();
    const int cw = colorMode.getIdealWidth();
    reverbMode.setBounds (kMargin, kModeRowY, rw, kModeRowHeight);
    pitchMode.setBounds ((width - pw) / 2, kModeRowY, pw, kModeRowHeight);
    colorMode.setBounds (width - kMargin - cw, kModeRowY, cw, kModeRowHeight);
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
        juce::ColourGradient glow (palette::auroraGreen.withAlpha (0.09f), bounds.getWidth() * 0.10f, -40.0f,
                                   palette::auroraGreen.withAlpha (0.0f), bounds.getWidth() * 0.10f, 240.0f, true);
        g.setGradientFill (glow);
        g.fillRect (bounds);
        juce::ColourGradient glow2 (palette::auroraViolet.withAlpha (0.09f), bounds.getWidth() * 0.92f, 40.0f,
                                    palette::auroraViolet.withAlpha (0.0f), bounds.getWidth() * 0.92f, 360.0f, true);
        g.setGradientFill (glow2);
        g.fillRect (bounds);
    }

    paintLogo (g, { static_cast<float> (kMargin), static_cast<float> (kHeaderY), 320.0f,
                    static_cast<float> (kHeaderHeight) });
    paintCaptions (g);

    // Hairline above the mode row
    g.setColour (palette::panelBorder);
    g.fillRect (juce::Rectangle<float> (static_cast<float> (kMargin), static_cast<float> (kModeRowY - 8),
                                        bounds.getWidth() - 2.0f * kMargin, 1.0f));
}

void AuroraBloomCanvas::paintCaptions (juce::Graphics& g)
{
    using namespace ab::ui;
    auto* lnf = dynamic_cast<AuroraLookAndFeel*> (&getLookAndFeel());
    if (lnf == nullptr)
        return;

    const auto f = lnf->font (11.0f);
    g.setFont (f);
    const float cell = static_cast<float> (knobRow.getWidth()) / static_cast<float> (knobs.size());
    const float y = static_cast<float> (kCaptionY) + kCaptionHeight * 0.5f;

    for (const auto& group : groups)
    {
        const float x0 = static_cast<float> (knobRow.getX()) + cell * static_cast<float> (group.firstKnob) + 10.0f;
        const float x1 = static_cast<float> (knobRow.getX()) + cell * static_cast<float> (group.firstKnob + group.numKnobs) - 10.0f;
        const float textW = juce::GlyphArrangement::getStringWidth (f, group.caption) + 12.0f;
        const float cx = (x0 + x1) * 0.5f;

        g.setColour (textDim);
        g.drawText (group.caption, juce::Rectangle<float> (textW, static_cast<float> (kCaptionHeight)).withCentre ({ cx, y }),
                    juce::Justification::centred);

        // Hairlines either side of the caption, tinted with the group's colours.
        const auto colourA = knobs[static_cast<size_t> (group.firstKnob)]->getSlider().getProperties()["accent"];
        const auto accent = juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (colourA)));
        g.setColour (accent.withAlpha (0.45f));
        if (cx - textW * 0.5f > x0 + 4.0f)
        {
            g.fillRect (juce::Rectangle<float> (x0, y, cx - textW * 0.5f - x0, 1.0f));
            g.fillRect (juce::Rectangle<float> (cx + textW * 0.5f, y, x1 - (cx + textW * 0.5f), 1.0f));
        }
    }
}

void AuroraBloomCanvas::paintLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    using namespace ab::ui;
    // Glyph: three concentric arcs opening upwards, like light blooming over a horizon.
    const float s = area.getHeight();
    const auto glyph = area.removeFromLeft (s).reduced (3.0f);
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
        const auto boldFont = lnf->font (21.0f, true).withExtraKerningFactor (0.12f);
        const auto lightFont = lnf->font (21.0f).withExtraKerningFactor (0.12f);
        g.setFont (boldFont);
        g.setColour (palette::text);
        const auto auroraWidth = juce::GlyphArrangement::getStringWidth (boldFont, "AURORA");
        g.drawText ("AURORA", area, juce::Justification::centredLeft);
        g.setFont (lightFont);
        g.setGradientFill (AuroraLookAndFeel::aurora (area.getTopLeft().translated (auroraWidth, 0.0f),
                                                      area.getTopLeft().translated (auroraWidth + 90.0f, 0.0f)));
        g.drawText ("BLOOM", area.withTrimmedLeft (auroraWidth + 7.0f), juce::Justification::centredLeft);
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
