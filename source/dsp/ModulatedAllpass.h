#pragma once

#include "ModulatedDelay.h"

namespace ab::dsp
{

/** Schroeder allpass with a modulated, fractional delay, in its single-delay-line form:

        v[n] = x[n] + g * v[n - D]
        y[n] = -g * v[n] + v[n - D]

    which has H(z) = (z^-D - g) / (1 - g z^-D), i.e. |H| = 1 for |g| < 1. With g = 0 the stage
    degenerates into a pure delay of D samples, which is what turns the whole diffuser network
    into a plain (ping-pong) delay line at Diffusion = 0. */
class ModulatedAllpass
{
public:
    void allocate (std::size_t maxDelaySamples) { line.allocate (maxDelaySamples); }
    void clear() noexcept { line.clear(); }
    float getMaxDelay() const noexcept { return line.getMaxDelay(); }

    float process (float x, float delaySamples, float g) noexcept
    {
        const float delayed = line.tap (delaySamples);
        const float v = x + g * delayed;
        line.push (v);
        return delayed - g * v;
    }

private:
    ModulatedDelay line;
};

} // namespace ab::dsp
