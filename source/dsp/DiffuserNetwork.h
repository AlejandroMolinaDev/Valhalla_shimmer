#pragma once

#include <array>

#include "ModulatedAllpass.h"
#include "RandomLfo.h"
#include "ReverbModes.h"

namespace ab::dsp
{

/** Two parallel cascades (L and R) of modulated Schroeder allpass diffusers.

    - Diffusion sets the allpass coefficient g (clamped to 0.98 for numerical safety).
    - Size scales every stage length; changing it in real time glides the delay lengths and
      therefore produces the characteristic pitch sweep (smoothed, so no clicks).
    - Each stage has its own random LFO whose rate is spread around Mod Rate; depth is a
      constant number of samples, so its audible detune grows with Diffusion.

    All memory is allocated in prepare(); process() is allocation- and lock-free. */
class DiffuserNetwork
{
public:
    static constexpr float kMaxDiffusion = 0.98f;
    static constexpr float kMaxModDepthSamples48k = 64.0f;

    /** Allocates every stage for the largest mode at Size = 100 % plus modulation headroom. */
    void prepare (double sampleRate);
    void reset() noexcept;

    void setMode (ReverbMode mode) noexcept;
    ReverbMode getMode() const noexcept { return mode; }
    int getNumStages() const noexcept { return numStages; }

    /** rateHz: average LFO rate. depthSamples: peak modulation in samples (already scaled to
        the current sample rate). */
    void setModulation (float rateHz, float depthSamples) noexcept;

    /** Base length of a stage in samples at the current sample rate (Size = 100 %). */
    float getBaseLength (int stage, bool right) const noexcept;

    /** Processes one stereo sample. sizeFactor in (0, 1], g in [0, kMaxDiffusion]. */
    void process (float inL, float inR, float& outL, float& outR, float sizeFactor, float g) noexcept
    {
        float l = inL, r = inR;
        for (int s = 0; s < numStages; ++s)
        {
            const auto i = static_cast<std::size_t> (s);
            const float dL = baseL[i] * sizeFactor + modDepth * lfoL[i].next();
            const float dR = baseR[i] * sizeFactor + modDepth * lfoR[i].next();
            l = stagesL[i].process (l, dL, g);
            r = stagesR[i].process (r, dR, g);
        }
        outL = l;
        outR = r;
    }

private:
    std::array<ModulatedAllpass, kMaxStages> stagesL, stagesR;
    std::array<RandomLfo, kMaxStages> lfoL, lfoR;
    std::array<float, kMaxStages> baseL {}, baseR {};
    double sampleRate = 48000.0;
    ReverbMode mode = ReverbMode::MediumStereo;
    int numStages = 0;
    float modDepth = 0.0f;
};

} // namespace ab::dsp
