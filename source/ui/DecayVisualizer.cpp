#include "DecayVisualizer.h"

#include "LookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace ab::ui
{

namespace
{
    constexpr float kFloorDb = -60.0f;

    float toNorm (float linear)
    {
        const float db = juce::Decibels::gainToDecibels (linear, kFloorDb);
        return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
    }
} // namespace

DecayVisualizer::DecayVisualizer (AuroraBloomProcessor& p)
    : processor (p),
      pitch (p.getApvts().getRawParameterValue (params::id::pitch)),
      feedback (p.getApvts().getRawParameterValue (params::id::feedback)),
      pitchMode (p.getApvts().getRawParameterValue (params::id::pitchMode))
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
    setTitle ("Decay visualizer");
    setComponentID ("visualizer");
}

DecayVisualizer::~DecayVisualizer()
{
    stopTimer();
}

void DecayVisualizer::updateTimer()
{
    if (isShowing())
        startTimerHz (60);
    else
        stopTimer();
}

void DecayVisualizer::timerCallback()
{
    advanceFrame();
}

void DecayVisualizer::advanceFrame()
{
    std::array<MeterFrame, 64> frames;
    const int n = processor.popMeterFrames (frames.data(), static_cast<int> (frames.size()));

    float wet = 0.0f, in = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        wet = juce::jmax (wet, frames[static_cast<size_t> (i)].wetL, frames[static_cast<size_t> (i)].wetR);
        in = juce::jmax (in, frames[static_cast<size_t> (i)].input);
    }

    // No new data (transport stopped / plugin bypassed by the host): let the display fall.
    const float target = toNorm (wet);
    level = target > level ? target : level + (target - level) * 0.12f;

    head = (head + 1) % kHistory;
    history[static_cast<size_t> (head)] = level;
    inputHistory[static_cast<size_t> (head)] = toNorm (in);

    phase += 0.012;
    if (phase > 1000.0)
        phase -= 1000.0;

    repaint();
}

void DecayVisualizer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (palette::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (palette::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    auto inner = bounds.reduced (12.0f, 10.0f);
    auto layers = inner.removeFromRight (inner.getWidth() * 0.32f);
    inner.removeFromRight (12.0f);

    paintEnvelope (g, inner);
    paintLayers (g, layers);
}

void DecayVisualizer::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto* lnf = dynamic_cast<AuroraLookAndFeel*> (&getLookAndFeel());

    // Grid every 12 dB
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (int db = -12; db > static_cast<int> (kFloorDb); db -= 12)
    {
        const float y = area.getY() + area.getHeight() * (static_cast<float> (db) / kFloorDb);
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }

    if (lnf != nullptr)
    {
        g.setFont (lnf->font (10.5f));
        g.setColour (palette::textDim);
        g.drawText ("TAIL", area.removeFromTop (0.0f).withHeight (14.0f), juce::Justification::topLeft);
    }

    juce::Path wet, input;
    const float step = area.getWidth() / static_cast<float> (kHistory - 1);
    for (int i = 0; i < kHistory; ++i)
    {
        const auto idx = static_cast<size_t> ((head + 1 + i) % kHistory);
        const float x = area.getX() + step * static_cast<float> (i);
        const float yw = area.getBottom() - area.getHeight() * history[idx];
        const float yi = area.getBottom() - area.getHeight() * inputHistory[idx];
        if (i == 0)
        {
            wet.startNewSubPath (x, yw);
            input.startNewSubPath (x, yi);
        }
        else
        {
            wet.lineTo (x, yw);
            input.lineTo (x, yi);
        }
    }

    juce::Path fill (wet);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();

    juce::ColourGradient fillGrad (palette::auroraCyan.withAlpha (0.30f), area.getX(), area.getY(),
                                   palette::auroraViolet.withAlpha (0.0f), area.getX(), area.getBottom(), false);
    g.setGradientFill (fillGrad);
    g.fillPath (fill);

    g.setGradientFill (AuroraLookAndFeel::aurora (area.getTopLeft(), area.getTopRight()));
    g.strokePath (wet, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (palette::textDim.withAlpha (0.45f));
    g.strokePath (input, juce::PathStrokeType (1.0f));
}

void DecayVisualizer::paintLayers (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float semitones = pitch->load();
    const auto mode = static_cast<dsp::PitchMode> (juce::jlimit (0, 4, static_cast<int> (std::lround (pitchMode->load()))));
    const float fbGain = dsp::ShimmerEngine::feedbackToGain (feedback->load() * 0.01f);
    const bool dual = mode == dsp::PitchMode::Dual || mode == dsp::PitchMode::DualReverse;
    const bool reverse = mode == dsp::PitchMode::SingleReverse || mode == dsp::PitchMode::DualReverse;
    const bool bypass = mode == dsp::PitchMode::Bypass || std::abs (semitones) < 0.01f;

    if (auto* lnf = dynamic_cast<AuroraLookAndFeel*> (&getLookAndFeel()))
    {
        g.setFont (lnf->font (10.5f));
        g.setColour (palette::textDim);
        const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  "); // middle dot
        const auto label = bypass ? "LAYERS" + dot + "OFF"
                                  : "LAYERS" + dot + params::semitonesToText (semitones)
                                        + (dual ? "  /  " + params::semitonesToText (-semitones) : juce::String());
        g.drawText (label, area.withHeight (14.0f), juce::Justification::topRight);
    }

    const float energy = 0.15f + 0.85f * level;
    const float centreY = dual ? area.getCentreY() + 6.0f : (semitones >= 0.0f ? area.getBottom() - 8.0f : area.getY() + 20.0f);
    const float direction = semitones >= 0.0f ? -1.0f : 1.0f;
    const float spacing = juce::jmap (std::abs (semitones) / 12.0f, 0.0f, 1.0f, 5.0f, 11.0f);
    const int numLayers = bypass ? 1 : 1 + juce::roundToInt (fbGain * 7.0f);

    auto ribbon = [&] (float y, float alpha, float k, float colourPos)
    {
        juce::Path p;
        const float amp = reverse ? 1.2f : 2.4f;
        const float freq = reverse ? 0.035f : 0.06f + 0.01f * k;
        const auto t = static_cast<float> (phase) * (1.0f + 0.3f * k);
        for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
        {
            const float yy = y + amp * std::sin (x * freq + t * 6.2831853f + k);
            if (x <= area.getX())
                p.startNewSubPath (x, yy);
            else
                p.lineTo (x, yy);
        }
        const auto colour = palette::auroraGreen.interpolatedWith (palette::auroraViolet, colourPos);
        g.setColour (colour.withAlpha (alpha * (reverse ? 0.55f : 1.0f)));
        g.strokePath (p, juce::PathStrokeType (reverse ? 3.0f : 1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    };

    ribbon (centreY, 0.9f * energy, 0.0f, 0.0f);
    if (bypass)
        return;

    for (int k = 1; k < numLayers; ++k)
    {
        const auto kf = static_cast<float> (k);
        const float alpha = energy * std::pow (fbGain, kf) * 0.9f;
        const float colourPos = juce::jmin (1.0f, kf / 6.0f);
        const float upY = centreY + direction * spacing * kf;
        if (area.contains (area.getCentreX(), upY))
            ribbon (upY, alpha, kf, colourPos);
        if (dual)
        {
            const float downY = centreY - direction * spacing * kf;
            if (area.contains (area.getCentreX(), downY))
                ribbon (downY, alpha, kf + 0.5f, colourPos);
        }
    }
}

} // namespace ab::ui
