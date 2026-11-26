#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ab::dsp
{

/** Power-of-two circular buffer with fractional reads using 3rd-order Lagrange interpolation.

    Convention: tap (d) is called *before* push() and returns x[n - d], i.e. d = 1 is the sample
    pushed last. Lagrange needs one sample on the "newer" side of the read point, so the
    smallest legal delay is 2 samples (enforced by clamping).

    Lagrange (rather than linear) interpolation is used because these lines sit inside long
    feedback loops: linear interpolation acts as a modulated low-pass filter that would darken
    and dull the tail on every recirculation. */
class ModulatedDelay
{
public:
    static constexpr float kMinDelay = 2.0f;

    /** Allocates room for at least maxDelaySamples. Not real-time safe. */
    void allocate (std::size_t maxDelaySamples)
    {
        std::size_t size = 16;
        while (size < maxDelaySamples + 8)
            size <<= 1;

        buffer.assign (size, 0.0f);
        mask = size - 1;
        writePos = 0;
        maxDelay = static_cast<float> (size - 4);
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    float getMaxDelay() const noexcept { return maxDelay; }
    bool isAllocated() const noexcept { return ! buffer.empty(); }

    void push (float x) noexcept
    {
        buffer[writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Integer-delay read (d >= 1). */
    float tapInt (std::size_t d) const noexcept
    {
        return buffer[(writePos - d) & mask];
    }

    /** Fractional read with 3rd-order Lagrange interpolation. */
    float tap (float delaySamples) const noexcept
    {
        const float d = std::clamp (delaySamples, kMinDelay, maxDelay);
        const auto i = static_cast<std::size_t> (d);
        const float f = d - static_cast<float> (i);

        // Samples at delays i-1, i, i+1, i+2 (node positions -1, 0, 1, 2), evaluated at f.
        const float ym1 = buffer[(writePos - (i - 1)) & mask];
        const float y0  = buffer[(writePos - i) & mask];
        const float y1  = buffer[(writePos - (i + 1)) & mask];
        const float y2  = buffer[(writePos - (i + 2)) & mask];

        const float fp1 = f + 1.0f;
        const float fm1 = f - 1.0f;
        const float fm2 = f - 2.0f;

        const float cm1 = -f * fm1 * fm2 * (1.0f / 6.0f);
        const float c0  = fp1 * fm1 * fm2 * 0.5f;
        const float c1  = -fp1 * f * fm2 * 0.5f;
        const float c2  = fp1 * f * fm1 * (1.0f / 6.0f);

        return cm1 * ym1 + c0 * y0 + c1 * y1 + c2 * y2;
    }

private:
    std::vector<float> buffer;
    std::size_t mask = 0;
    std::size_t writePos = 0;
    float maxDelay = 0.0f;
};

} // namespace ab::dsp
