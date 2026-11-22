#pragma once

#include <array>
#include <cstdint>

#include "ModulatedDelay.h"
#include "RandomLfo.h"

namespace ab::dsp
{

/** Granular "rotating tape head" pitch shifter designed to live inside the reverb loop.

    One input delay line is shared by two independent grain voices:
        voice A reads at  ratio = 2^( semitones / 12)
        voice B reads at  ratio = 2^(-semitones / 12)   (used by the Dual modes)

    Each voice overlaps up to four Hann-windowed grains. Grain length (base 60 ms) and spawn
    time are randomised by ±25 % and the read offset by up to 10 ms, which breaks the
    periodicity of a classic two-head shifter: instead of comb filtering it produces the noisy,
    ensemble-like sidebands that make the shimmer sound like a string section. No
    autocorrelation de-glitching is done on purpose.

    Reverse grains read their segment backwards in time before it is pitch shifted, which
    removes transients and gives an organ-like texture.

    A grain keeps the ratio and direction it was born with; pitch and direction changes thus
    take effect grain by grain and are inherently crossfaded by the windows (no clicks). */
class GrainPitchShifter
{
public:
    static constexpr int kMaxGrains = 4;
    static constexpr double kBaseGrainMs = 60.0;
    static constexpr double kGrainJitter = 0.25;
    static constexpr double kOffsetJitterMs = 10.0;
    static constexpr double kMinDelayMs = 1.0;
    static constexpr float kMaxRatio = 2.0f;

    void prepare (double sampleRate, std::uint32_t seed);
    void reset() noexcept;

    /** Sets the ratio used by grains spawned from now on. */
    void setSemitones (float semitones) noexcept;
    void setReverse (bool shouldReverse) noexcept { reverse = shouldReverse; }

    /** Writes one input sample and returns the output of the requested voices.
        Disabled voices are not computed (and are restarted cleanly when re-enabled). */
    void process (float input, bool enableA, bool enableB, float& outA, float& outB) noexcept
    {
        outA = enableA ? voices[0].render (*this) : (voices[0].disable(), 0.0f);
        outB = enableB ? voices[1].render (*this) : (voices[1].disable(), 0.0f);
        line.push (input);
    }

private:
    struct Grain
    {
        double delay = 0.0;      // read position, in samples behind the write head
        double delayRate = 0.0;  // change of delay per sample: (1 - r) forward, (1 + r) reverse
        float phase = 0.0f;      // window phase 0..1
        float phaseInc = 0.0f;
        bool active = false;
    };

    struct Voice
    {
        std::array<Grain, kMaxGrains> grains {};
        double samplesToNext = 0.0;
        float ratio = 1.0f;
        bool enabled = false;

        void disable() noexcept { enabled = false; }
        float render (GrainPitchShifter& owner) noexcept;
        void spawn (GrainPitchShifter& owner) noexcept;
        void clear() noexcept;
    };

    float window (float phase) const noexcept
    {
        const float x = phase * static_cast<float> (kWindowSize);
        const auto i = static_cast<std::size_t> (x);
        const float f = x - static_cast<float> (i);
        return windowTable[i] + f * (windowTable[i + 1] - windowTable[i]);
    }

    static constexpr std::size_t kWindowSize = 1024;
    std::array<float, kWindowSize + 2> windowTable {};

    ModulatedDelay line;
    std::array<Voice, 2> voices;
    FastRandom rng;
    std::uint32_t seed = 1;
    double sampleRate = 48000.0;
    double baseGrainSamples = 2880.0;
    double offsetJitterSamples = 480.0;
    double minDelaySamples = 52.0;
    float semitones = 0.0f;
    bool reverse = false;
};

} // namespace ab::dsp
