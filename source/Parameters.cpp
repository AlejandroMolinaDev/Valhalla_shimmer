#include "Parameters.h"

namespace ab::params
{

namespace
{
    using Range = juce::NormalisableRange<float>;

    /** Exact logarithmic mapping between normalised 0..1 and [start, end]. */
    Range logRange (float start, float end)
    {
        return Range (
            start, end,
            [] (float s, float e, float norm) { return s * std::pow (e / s, norm); },
            [] (float s, float e, float value) { return std::log (value / s) / std::log (e / s); },
            [] (float s, float e, float value) { return juce::jlimit (s, e, value); });
    }

    juce::ParameterID pid (const char* name) { return { name, kParameterVersion }; }

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* paramId, const juce::String& name, Range range,
                                                          float defaultValue, const juce::String& label,
                                                          std::function<juce::String (float)> toText,
                                                          std::function<float (const juce::String&)> fromText)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel (label)
                              .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                              .withValueFromStringFunction ([fromText] (const juce::String& t) { return fromText (t); });

        return std::make_unique<juce::AudioParameterFloat> (pid (paramId), name, range, defaultValue, attributes);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const char* paramId, const juce::String& name,
                                                            const juce::StringArray& choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (pid (paramId), name, choices, defaultIndex);
    }
} // namespace

// =============================================================================================
const juce::StringArray& allIds()
{
    static const juce::StringArray ids { id::mix, id::pitch, id::feedback, id::diffusion, id::size, id::lowCut,
                                         id::highCut, id::modRate, id::modDepth, id::reverbMode, id::pitchMode,
                                         id::colorMode };
    return ids;
}

const juce::StringArray& reverbModeNames()
{
    static const juce::StringArray names { "Mono", "Big Stereo", "Medium Stereo", "Small Stereo" };
    return names;
}

const juce::StringArray& pitchModeNames()
{
    static const juce::StringArray names { "Single", "Dual", "Single Reverse", "Dual Reverse", "Bypass" };
    return names;
}

const juce::StringArray& colorModeNames()
{
    static const juce::StringArray names { "Bright", "Dark" };
    return names;
}

// =============================================================================================
juce::String percentToText (float value)
{
    return juce::String (value, std::abs (value) < 10.0f ? 1 : 0) + " %";
}

juce::String semitonesToText (float value)
{
    const auto rounded = std::round (value);
    const bool isWhole = std::abs (value - rounded) < 0.005f;
    const auto magnitude = isWhole ? juce::String (static_cast<int> (std::abs (rounded)))
                                   : juce::String (std::abs (value), 2);
    const auto sign = value > 0.0049f ? "+" : (value < -0.0049f ? "-" : "");
    return sign + magnitude + " st";
}

juce::String frequencyToText (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz";
}

juce::String rateToText (float hz)
{
    return juce::String (hz, hz < 1.0f ? 2 : (hz < 10.0f ? 2 : 1)) + " Hz";
}

juce::String diffusionToText (float value)
{
    return juce::String (value, 2);
}

float textToNumber (const juce::String& text)
{
    // Keep only the leading numeric part ("+12 st", "60 %", "0.80").
    const auto numeric = text.trim().retainCharacters ("0123456789.,-+").replaceCharacter (',', '.');
    const bool negative = numeric.startsWithChar ('-');
    const auto value = numeric.removeCharacters ("+-").getFloatValue();
    return negative ? -value : value;
}

float textToFrequency (const juce::String& text)
{
    const auto value = textToNumber (text);
    return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
}

// =============================================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeFloat (id::mix, "Mix", Range (0.0f, 100.0f, 0.01f), 50.0f, "%", percentToText, textToNumber));
    layout.add (makeFloat (id::pitch, "Pitch", Range (-12.0f, 12.0f, 0.01f), 12.0f, "st", semitonesToText, textToNumber));
    layout.add (makeFloat (id::feedback, "Feedback", Range (0.0f, 100.0f, 0.01f), 60.0f, "%", percentToText, textToNumber));
    layout.add (makeFloat (id::diffusion, "Diffusion", Range (0.0f, 1.0f, 0.001f), 0.80f, "", diffusionToText, textToNumber));
    layout.add (makeFloat (id::size, "Size", Range (0.0f, 100.0f, 0.01f), 60.0f, "%", percentToText, textToNumber));
    layout.add (makeFloat (id::lowCut, "Low Cut", logRange (10.0f, 2000.0f), 20.0f, "Hz", frequencyToText, textToFrequency));
    layout.add (makeFloat (id::highCut, "High Cut", logRange (500.0f, 20000.0f), 8000.0f, "Hz", frequencyToText, textToFrequency));
    layout.add (makeFloat (id::modRate, "Mod Rate", logRange (0.01f, 10.0f), 0.40f, "Hz", rateToText, textToNumber));
    layout.add (makeFloat (id::modDepth, "Mod Depth", Range (0.0f, 100.0f, 0.01f), 15.0f, "%", percentToText, textToNumber));
    layout.add (makeChoice (id::reverbMode, "Reverb Mode", reverbModeNames(), 2));
    layout.add (makeChoice (id::pitchMode, "Pitch Mode", pitchModeNames(), 0));
    layout.add (makeChoice (id::colorMode, "Color", colorModeNames(), 0));

    return layout;
}

// =============================================================================================
ParameterRefs::ParameterRefs (juce::AudioProcessorValueTreeState& apvts)
    : mix (apvts.getRawParameterValue (id::mix)),
      pitch (apvts.getRawParameterValue (id::pitch)),
      feedback (apvts.getRawParameterValue (id::feedback)),
      diffusion (apvts.getRawParameterValue (id::diffusion)),
      size (apvts.getRawParameterValue (id::size)),
      lowCut (apvts.getRawParameterValue (id::lowCut)),
      highCut (apvts.getRawParameterValue (id::highCut)),
      modRate (apvts.getRawParameterValue (id::modRate)),
      modDepth (apvts.getRawParameterValue (id::modDepth)),
      reverbMode (apvts.getRawParameterValue (id::reverbMode)),
      pitchMode (apvts.getRawParameterValue (id::pitchMode)),
      colorMode (apvts.getRawParameterValue (id::colorMode))
{
    jassert (mix && pitch && feedback && diffusion && size && lowCut && highCut && modRate && modDepth
             && reverbMode && pitchMode && colorMode);
}

dsp::EngineParameters ParameterRefs::load() const noexcept
{
    const auto choice = [] (const std::atomic<float>* p, int count)
    { return juce::jlimit (0, count - 1, static_cast<int> (std::lround (p->load (std::memory_order_relaxed)))); };
    const auto get = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };

    dsp::EngineParameters e;
    e.mix = get (mix) * 0.01f;
    e.pitchSemitones = get (pitch);
    e.feedback = get (feedback) * 0.01f;
    e.diffusion = get (diffusion);
    e.size = get (size) * 0.01f;
    e.lowCutHz = get (lowCut);
    e.highCutHz = get (highCut);
    e.modRateHz = get (modRate);
    e.modDepth = get (modDepth) * 0.01f;
    e.reverbMode = static_cast<dsp::ReverbMode> (choice (reverbMode, dsp::kNumReverbModes));
    e.pitchMode = static_cast<dsp::PitchMode> (choice (pitchMode, dsp::kNumPitchModes));
    e.colorMode = static_cast<dsp::ColorMode> (choice (colorMode, dsp::kNumColorModes));
    return e;
}

} // namespace ab::params
