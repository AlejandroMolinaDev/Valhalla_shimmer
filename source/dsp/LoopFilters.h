#pragma once

#include <array>
#include <cmath>

#include "Smoothers.h"

namespace ab::dsp
{

/** 2nd-order topology-preserving-transform state-variable filter (Zavalishin / Simper).
    Unlike direct-form biquads it stays well behaved when its cutoff is modulated, which
    matters because Low Cut and High Cut are swept by the user while the loop rings. */
class SvfTpt
{
public:
    void setCoefficients (double cutoffHz, double sampleRate, double q) noexcept;
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    float lowpass (float x) noexcept
    {
        float low = 0.0f, band = 0.0f;
        tick (x, low, band);
        return low;
    }

    float highpass (float x) noexcept
    {
        float low = 0.0f, band = 0.0f;
        tick (x, low, band);
        return x - k * band - low;
    }

private:
    void tick (float x, float& low, float& band) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
    }

    float k = 1.41421356f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

/** In-loop filtering for one channel pair:

        LowCut (12 dB/oct high-pass) -> HighCut (12 dB/oct low-pass) -> Color

    Color = Bright leaves the full bandwidth (only High Cut limits it); Color = Dark adds a steep
    8th-order Butterworth low-pass (~9-11 kHz depending on the reverb mode), emulating late-70s /
    early-80s digital reverbs and taming the aliasing that pitch shifting builds up over many
    recirculations. Bright <-> Dark is a 20 ms crossfade. Cutoffs glide in the log-frequency
    domain (50 ms) and coefficients are recomputed per sample only while gliding. */
class LoopFilters
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setLowCut (float hz) noexcept { lowCutLog.setTarget (std::log (hz)); }
    void setHighCut (float hz) noexcept { highCutLog.setTarget (std::log (hz)); }
    void setDark (bool shouldBeDark) noexcept { darkMix.setTarget (shouldBeDark ? 1.0f : 0.0f); }
    void setDarkCutoff (float hz) noexcept; // changes immediately (called while the wet is ducked)

    /** Jump all smoothers to their targets (after reset / prepare). */
    void snap() noexcept;

    void process (float& left, float& right) noexcept
    {
        if (lowCutLog.isSmoothing())
            updateLowCut (lowCutLog.next());
        if (highCutLog.isSmoothing())
            updateHighCut (highCutLog.next());

        float l = highCutL.lowpass (lowCutL.highpass (left));
        float r = highCutR.lowpass (lowCutR.highpass (right));

        const float dark = darkMix.next();
        if (dark > 0.0f || darkMix.getTarget() > 0.0f)
        {
            if (! darkActive)
            {
                for (auto& f : darkL) f.reset();
                for (auto& f : darkR) f.reset();
                darkActive = true;
            }
            float dl = l, dr = r;
            for (std::size_t i = 0; i < kDarkSections; ++i)
            {
                dl = darkL[i].lowpass (dl);
                dr = darkR[i].lowpass (dr);
            }
            l += dark * (dl - l);
            r += dark * (dr - r);
        }
        else
        {
            darkActive = false;
        }

        left = l;
        right = r;
    }

private:
    static constexpr std::size_t kDarkSections = 4; // 4 x 2nd order = 8th-order Butterworth

    void updateLowCut (float logHz) noexcept;
    void updateHighCut (float logHz) noexcept;

    double sampleRate = 48000.0;
    SvfTpt lowCutL, lowCutR, highCutL, highCutR;
    std::array<SvfTpt, kDarkSections> darkL, darkR;
    LinearSmoother lowCutLog, highCutLog, darkMix;
    float darkCutoffHz = 10000.0f;
    bool darkActive = false;
};

} // namespace ab::dsp
