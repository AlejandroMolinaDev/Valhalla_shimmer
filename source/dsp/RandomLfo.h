#pragma once

#include <cstdint>

namespace ab::dsp
{

/** Tiny deterministic PRNG (xorshift32). Each engine instance owns its generators and seeds
    them explicitly, so output is reproducible and there is no shared global state between
    plugin instances running on different host threads. */
class FastRandom
{
public:
    explicit FastRandom (std::uint32_t seed = 0x9E3779B9u) noexcept { setSeed (seed); }

    void setSeed (std::uint32_t seed) noexcept { state = seed != 0 ? seed : 0x6D2B79F5u; }

    std::uint32_t nextU32() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return static_cast<float> (nextU32() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

private:
    std::uint32_t state = 1;
};

/** Smoothed random LFO: a new random breakpoint per period, interpolated with a Catmull-Rom
    (cubic Hermite) spline. Unlike a sine it never repeats, so dozens of these with slightly
    different rates break up standing waves and metallic ringing in the diffuser network.
    The phase accumulator is double precision (tiny increments at 0.01 Hz / 192 kHz). */
class RandomLfo
{
public:
    void prepare (double sampleRate, std::uint32_t seed, float rateFactor) noexcept
    {
        invSampleRate = 1.0 / sampleRate;
        factor = rateFactor;
        rng.setSeed (seed);
        p0 = rng.nextBipolar();
        p1 = rng.nextBipolar();
        p2 = rng.nextBipolar();
        p3 = rng.nextBipolar();
        phase = static_cast<double> (rng.nextFloat()); // random start phase
        setRate (1.0f);
    }

    void setRate (float rateHz) noexcept
    {
        increment = static_cast<double> (rateHz * factor) * invSampleRate;
    }

    float next() noexcept
    {
        phase += increment;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            p0 = p1;
            p1 = p2;
            p2 = p3;
            p3 = rng.nextBipolar();
        }

        const auto t = static_cast<float> (phase);
        // Catmull-Rom between p1 and p2.
        const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
        const float b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        const float c = -0.5f * p0 + 0.5f * p2;
        return ((a * t + b) * t + c) * t + p1;
    }

private:
    FastRandom rng;
    double phase = 0.0, increment = 0.0, invSampleRate = 1.0 / 48000.0;
    float factor = 1.0f;
    float p0 = 0.0f, p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
};

} // namespace ab::dsp
