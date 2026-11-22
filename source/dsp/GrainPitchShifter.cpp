#include "GrainPitchShifter.h"

#include <cmath>

namespace ab::dsp
{

void GrainPitchShifter::prepare (double newSampleRate, std::uint32_t newSeed)
{
    sampleRate = newSampleRate;
    seed = newSeed;
    baseGrainSamples = kBaseGrainMs * 0.001 * sampleRate;
    offsetJitterSamples = kOffsetJitterMs * 0.001 * sampleRate;
    minDelaySamples = kMinDelayMs * 0.001 * sampleRate + 4.0;

    // Worst case is a reverse grain: it starts at most minDelay + offsetJitter behind the write
    // head and recedes by (1 + ratio) samples per sample over the longest possible grain.
    const double longestGrain = baseGrainSamples * (1.0 + kGrainJitter);
    const double maxDelay = minDelaySamples + offsetJitterSamples
                          + (1.0 + static_cast<double> (kMaxRatio)) * longestGrain + 16.0;
    line.allocate (static_cast<std::size_t> (std::ceil (maxDelay)));

    for (std::size_t i = 0; i < windowTable.size(); ++i)
    {
        const double phase = static_cast<double> (std::min (i, kWindowSize)) / static_cast<double> (kWindowSize);
        windowTable[i] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * phase));
    }

    reset();
}

void GrainPitchShifter::reset() noexcept
{
    line.clear();
    rng.setSeed (seed);
    for (auto& v : voices)
        v.clear();
    setSemitones (semitones);
}

void GrainPitchShifter::setSemitones (float newSemitones) noexcept
{
    semitones = newSemitones;
    const float ratio = std::exp2 (semitones / 12.0f);
    voices[0].ratio = ratio;
    voices[1].ratio = 1.0f / ratio;
}

// =============================================================================================
void GrainPitchShifter::Voice::clear() noexcept
{
    for (auto& g : grains)
        g.active = false;
    samplesToNext = 0.0;
    enabled = false;
}

void GrainPitchShifter::Voice::spawn (GrainPitchShifter& owner) noexcept
{
    // Pick a free slot; if all are busy (only possible with extreme jitter) steal the grain
    // closest to its end, where the window is near zero.
    Grain* slot = nullptr;
    float latestPhase = -1.0f;
    for (auto& g : grains)
    {
        if (! g.active)
        {
            slot = &g;
            break;
        }
        if (g.phase > latestPhase)
        {
            latestPhase = g.phase;
            slot = &g;
        }
    }

    auto& rng = owner.rng;
    const double length = owner.baseGrainSamples * (1.0 + kGrainJitter * static_cast<double> (rng.nextBipolar()));
    const double offset = owner.minDelaySamples + owner.offsetJitterSamples * static_cast<double> (rng.nextFloat());
    const auto r = static_cast<double> (ratio);

    Grain& g = *slot;
    if (owner.reverse)
    {
        // Read backwards: the read head recedes from the write head at (1 + r) samples/sample.
        g.delay = offset;
        g.delayRate = 1.0 + r;
    }
    else
    {
        // Forward: delay shrinks by (r - 1) per sample when shifting up, grows when shifting down.
        g.delay = offset + std::max (0.0, (r - 1.0) * length);
        g.delayRate = 1.0 - r;
    }
    g.phase = 0.0f;
    g.phaseInc = static_cast<float> (1.0 / length);
    g.active = true;

    // Next grain after ~half this grain's length (±20 %): the average window overlap stays at
    // unity gain while the exact spawn times remain irregular.
    samplesToNext += 0.5 * length * (1.0 + 0.2 * static_cast<double> (rng.nextBipolar()));
}

float GrainPitchShifter::Voice::render (GrainPitchShifter& owner) noexcept
{
    if (! enabled)
    {
        for (auto& g : grains)
            g.active = false;
        samplesToNext = 0.0;
        enabled = true;
    }

    samplesToNext -= 1.0;
    if (samplesToNext <= 0.0)
        spawn (owner);

    float out = 0.0f;
    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        out += owner.window (g.phase) * owner.line.tap (static_cast<float> (g.delay));

        g.delay += g.delayRate;
        g.phase += g.phaseInc;
        if (g.phase >= 1.0f)
            g.active = false;
    }
    return out;
}

} // namespace ab::dsp
