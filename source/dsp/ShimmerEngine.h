#pragma once

#include "DiffuserNetwork.h"
#include "ReverbModes.h"
#include "Smoothers.h"

namespace ab::dsp
{

enum class PitchMode : int
{
    Single = 0,
    Dual,
    SingleReverse,
    DualReverse,
    Bypass
};

enum class ColorMode : int
{
    Bright = 0,
    Dark
};

inline constexpr int kNumPitchModes = 5;
inline constexpr int kNumColorModes = 2;

/** Plain-value parameter snapshot handed to the engine once per block. */
struct EngineParameters
{
    float mix = 0.5f;              // 0..1
    float pitchSemitones = 12.0f;  // -12..+12
    float feedback = 0.6f;         // 0..1
    float diffusion = 0.8f;        // 0..1
    float size = 0.6f;             // 0..1
    float lowCutHz = 20.0f;        // 10..2000
    float highCutHz = 8000.0f;     // 500..20000
    float modRateHz = 0.4f;        // 0.01..10
    float modDepth = 0.15f;        // 0..1
    ReverbMode reverbMode = ReverbMode::MediumStereo;
    PitchMode pitchMode = PitchMode::Single;
    ColorMode colorMode = ColorMode::Bright;
};

/** One-pole DC blocker (~5 Hz) used inside the feedback loop. */
class DcBlocker
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

/** The whole wet path of Aurora Bloom:

        in ─► u = in + g·Rot(fb[n-1]) ─► DiffuserNetwork ─► softClip ─► wet ─► equal-power mix
                       ▲                                       │
                       └─ DC block ◄─ (pitch, filters) ◄───────┘

    Processing is strictly per sample with every smoother and modulator advanced once per
    sample, so the output does not depend on how the host splits the audio into blocks. */
class ShimmerEngine
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    bool isPrepared() const noexcept { return prepared; }
    double getSampleRate() const noexcept { return sampleRate; }

    void setParameters (const EngineParameters& newParams) noexcept;

    /** Host bypass: the dry signal passes at unity, the wet fades out and the loop is flushed
        (input and feedback ramp to zero over 50 ms) so no stale tail bursts out later. */
    void setBypassed (bool shouldBeBypassed) noexcept;

    /** In-place stereo processing. Any numSamples >= 0 is valid. */
    void process (float* left, float* right, int numSamples) noexcept;

    // --- Parameter laws (public for tests and UI) ------------------------------------------
    static float feedbackToGain (float feedback01) noexcept;
    static float sizeToFactor (float size01) noexcept;
    static void mixGains (float mix01, float& dryGain, float& wetGain) noexcept;
    static float softClip (float x) noexcept;
    static float diffusionToCoefficient (float diffusion01) noexcept;
    static double estimateTailSeconds (const EngineParameters& p) noexcept;

    // --- Meters (valid after each process() call) -------------------------------------------
    float getLastWetRms (int channel) const noexcept { return channel == 0 ? wetRmsL : wetRmsR; }
    float getLastInputRms() const noexcept { return inputRms; }
    int getNumResets() const noexcept { return numSafetyResets; }

private:
    void applyMode (ReverbMode newMode) noexcept;
    void clearLoopState() noexcept;
    void updateModulation() noexcept;

    double sampleRate = 48000.0;
    bool prepared = false;

    EngineParameters params;
    DiffuserNetwork diffuser;

    LinearSmoother dryGain, wetGain, feedbackGain, diffusionG, bypassAmount;
    OnePoleSmoother sizeFactor;

    // Reverb-mode change: duck the wet path, reconfigure, fade back in.
    ReverbMode activeMode = ReverbMode::MediumStereo;
    float duckGain = 1.0f;
    float duckStep = 0.0f;
    bool monoInput = false;
    float crossCos = 1.0f, crossSin = 0.0f;

    float fbL = 0.0f, fbR = 0.0f;
    DcBlocker dcL, dcR;

    float wetRmsL = 0.0f, wetRmsR = 0.0f, inputRms = 0.0f;
    int numSafetyResets = 0;
};

} // namespace ab::dsp
