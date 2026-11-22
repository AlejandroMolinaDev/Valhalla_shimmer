#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/ShimmerEngine.h"

namespace ab::params
{

// Parameter IDs. Never rename or reorder: hosts (FL Studio projects, .fst presets, automation
// clips) store parameters by ID/index. New parameters must be appended with a new version.
namespace id
{
    inline constexpr const char* mix = "mix";
    inline constexpr const char* pitch = "pitch";
    inline constexpr const char* feedback = "feedback";
    inline constexpr const char* diffusion = "diffusion";
    inline constexpr const char* size = "size";
    inline constexpr const char* lowCut = "lowCut";
    inline constexpr const char* highCut = "highCut";
    inline constexpr const char* modRate = "modRate";
    inline constexpr const char* modDepth = "modDepth";
    inline constexpr const char* reverbMode = "reverbMode";
    inline constexpr const char* pitchMode = "pitchMode";
    inline constexpr const char* colorMode = "colorMode";
} // namespace id

inline constexpr int kParameterVersion = 1;

/** All parameter IDs in their canonical order. */
const juce::StringArray& allIds();

const juce::StringArray& reverbModeNames();
const juce::StringArray& pitchModeNames();
const juce::StringArray& colorModeNames();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Cached raw (atomic) parameter pointers for lock-free reads on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    dsp::EngineParameters load() const noexcept;

    std::atomic<float>* mix;
    std::atomic<float>* pitch;
    std::atomic<float>* feedback;
    std::atomic<float>* diffusion;
    std::atomic<float>* size;
    std::atomic<float>* lowCut;
    std::atomic<float>* highCut;
    std::atomic<float>* modRate;
    std::atomic<float>* modDepth;
    std::atomic<float>* reverbMode;
    std::atomic<float>* pitchMode;
    std::atomic<float>* colorMode;
};

// Value <-> text helpers (shared by the parameters and the UI).
juce::String percentToText (float value);
juce::String semitonesToText (float value);
juce::String frequencyToText (float hz);
juce::String rateToText (float hz);
juce::String diffusionToText (float value);
float textToNumber (const juce::String& text);
float textToFrequency (const juce::String& text);

} // namespace ab::params
