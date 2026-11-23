#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestHelpers.h"

using namespace ab;
using namespace ab::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    constexpr double kSr = 48000.0;

    const char* modeName (dsp::ReverbMode m)
    {
        switch (m)
        {
            case dsp::ReverbMode::Mono: return "Mono";
            case dsp::ReverbMode::BigStereo: return "Big Stereo";
            case dsp::ReverbMode::MediumStereo: return "Medium Stereo";
            case dsp::ReverbMode::SmallStereo: return "Small Stereo";
        }
        return "?";
    }
} // namespace

// =============================================================================================
// Parameter laws
// =============================================================================================
TEST_CASE ("Feedback law: 0..0.995, monotonic, last 10 % for near-infinite tails", "[laws]")
{
    CHECK (dsp::ShimmerEngine::feedbackToGain (0.0f) == 0.0f);
    CHECK_THAT (dsp::ShimmerEngine::feedbackToGain (1.0f), WithinAbs (0.995, 1.0e-6));
    CHECK_THAT (dsp::ShimmerEngine::feedbackToGain (0.9f), WithinAbs (0.95, 1.0e-6));

    float previous = -1.0f;
    for (int i = 0; i <= 1000; ++i)
    {
        const float g = dsp::ShimmerEngine::feedbackToGain (static_cast<float> (i) / 1000.0f);
        REQUIRE (g >= previous);
        REQUIRE (g < 1.0f);
        previous = g;
    }
}

TEST_CASE ("Test 6 - Mix is equal-power: dry^2 + wet^2 == 1", "[laws][mix]")
{
    for (const float mix : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        float dry = 0.0f, wet = 0.0f;
        dsp::ShimmerEngine::mixGains (mix, dry, wet);
        CHECK_THAT (dry * dry + wet * wet, WithinAbs (1.0, 1.0e-6));
    }

    float dry = 0.0f, wet = 0.0f;
    dsp::ShimmerEngine::mixGains (0.0f, dry, wet);
    CHECK_THAT (dry, WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (wet, WithinAbs (0.0, 1.0e-6));
    dsp::ShimmerEngine::mixGains (1.0f, dry, wet);
    CHECK_THAT (dry, WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (wet, WithinAbs (1.0, 1.0e-6));
}

TEST_CASE ("Test 6 - Mix measured end to end: dry^2 + wet^2 == 1 at 0/50/100 %", "[mix]")
{
    for (const float mix : { 0.0f, 0.5f, 1.0f })
    {
        CAPTURE (mix);
        dsp::EngineParameters p;
        p.mix = mix;
        p.feedback = 0.0f;
        p.diffusion = 0.0f;
        p.size = 1.0f;        // size factor 1.0 -> integer delays at 48 kHz (no interpolation loss)
        p.modDepth = 0.0f;
        p.pitchMode = dsp::PitchMode::Bypass;
        p.lowCutHz = 10.0f;
        p.highCutHz = 20000.0f;
        auto engine = makeEngine (p);

        // Impulse kept below the soft-clip knee so the measurement is purely linear.
        auto s = impulse (static_cast<std::size_t> (kSr * 1.5), 0.5f);
        render (engine, s);

        const double dry = static_cast<double> (s.l[0]) / 0.5;
        const double wetEnergy = energy (s.l, 1) / 0.25;
        CHECK_THAT (dry * dry + wetEnergy, WithinAbs (1.0, 0.02));
    }
}

TEST_CASE ("Soft clipper is transparent below the knee and bounded by 1", "[laws]")
{
    for (float x = -0.75f; x <= 0.75f; x += 0.01f)
        REQUIRE (dsp::ShimmerEngine::softClip (x) == x);

    for (const float x : { 0.8f, 1.0f, 2.0f, 10.0f, 1.0e6f })
    {
        REQUIRE (dsp::ShimmerEngine::softClip (x) <= 1.0f);
        REQUIRE (dsp::ShimmerEngine::softClip (-x) >= -1.0f);
        REQUIRE (dsp::ShimmerEngine::softClip (x) > 0.75f);
    }
}

TEST_CASE ("Tail estimate is generous and grows with feedback", "[laws]")
{
    dsp::EngineParameters p;
    p.feedback = 0.3f;
    const auto low = dsp::ShimmerEngine::estimateTailSeconds (p);
    p.feedback = 0.85f;
    const auto high = dsp::ShimmerEngine::estimateTailSeconds (p);
    p.feedback = 1.0f;
    const auto infinite = dsp::ShimmerEngine::estimateTailSeconds (p);

    CHECK (low >= 1.0);
    CHECK (high > low);
    CHECK (infinite >= 100.0);
}

// =============================================================================================
// Test 1 - Stability
// =============================================================================================
TEST_CASE ("Test 1 - 10 s white noise at Feedback 100 %: finite and peak < +6 dBFS", "[stability]")
{
    const auto reverbMode = GENERATE (dsp::ReverbMode::Mono, dsp::ReverbMode::BigStereo,
                                      dsp::ReverbMode::MediumStereo, dsp::ReverbMode::SmallStereo);
    const auto pitchMode = GENERATE (dsp::PitchMode::Single, dsp::PitchMode::Dual, dsp::PitchMode::SingleReverse,
                                     dsp::PitchMode::DualReverse, dsp::PitchMode::Bypass);
    CAPTURE (modeName (reverbMode), static_cast<int> (pitchMode));

    dsp::EngineParameters p;
    p.feedback = 1.0f;
    p.reverbMode = reverbMode;
    p.pitchMode = pitchMode;
    p.diffusion = 0.85f;
    p.lowCutHz = 10.0f;
    p.highCutHz = 20000.0f;
    auto engine = makeEngine (p);

    auto s = whiteNoise (static_cast<std::size_t> (kSr * 10.0), 1.0f);
    render (engine, s);

    REQUIRE (allFinite (s.l));
    REQUIRE (allFinite (s.r));
    CHECK (peakAbs (s.l) < 2.0f); // +6 dBFS
    CHECK (peakAbs (s.r) < 2.0f);
    CHECK (engine.getNumResets() == 0);
}

TEST_CASE ("Stability at extreme settings (Diffusion 1, Size 100 %, Mod 100 %, Dark)", "[stability]")
{
    const auto reverbMode = GENERATE (dsp::ReverbMode::Mono, dsp::ReverbMode::SmallStereo);
    CAPTURE (modeName (reverbMode));

    dsp::EngineParameters p;
    p.feedback = 1.0f;
    p.diffusion = 1.0f;
    p.size = 1.0f;
    p.modDepth = 1.0f;
    p.modRateHz = 10.0f;
    p.pitchSemitones = 12.0f;
    p.reverbMode = reverbMode;
    p.colorMode = dsp::ColorMode::Dark;
    auto engine = makeEngine (p);

    auto s = whiteNoise (static_cast<std::size_t> (kSr * 5.0), 1.0f, 99);
    render (engine, s, { 1, 7, 64, 513, 4096 });

    REQUIRE (allFinite (s.l));
    REQUIRE (allFinite (s.r));
    CHECK (peakAbs (s.l) < 2.0f);
    CHECK (peakAbs (s.r) < 2.0f);
}

// =============================================================================================
// Test 2 - Silence
// =============================================================================================
TEST_CASE ("Test 2 - After the input stops, output decays below -90 dBFS (Feedback < 90 %)", "[decay]")
{
    const auto reverbMode = GENERATE (dsp::ReverbMode::Mono, dsp::ReverbMode::BigStereo,
                                      dsp::ReverbMode::MediumStereo, dsp::ReverbMode::SmallStereo);
    const auto pitchMode = GENERATE (dsp::PitchMode::Single, dsp::PitchMode::Bypass);
    CAPTURE (modeName (reverbMode), static_cast<int> (pitchMode));

    dsp::EngineParameters p;
    p.feedback = 0.85f;
    p.reverbMode = reverbMode;
    p.pitchMode = pitchMode;
    auto engine = makeEngine (p);

    // 1 s of noise to fill the tank...
    auto excite = whiteNoise (static_cast<std::size_t> (kSr), 0.5f);
    render (engine, excite);

    // ...then silence, in 100 ms chunks, for at most 3 minutes.
    const auto chunk = static_cast<std::size_t> (kSr * 0.1);
    const float threshold = std::pow (10.0f, -90.0f / 20.0f);
    double seconds = 0.0;
    bool decayed = false;
    for (int i = 0; i < 1800 && ! decayed; ++i)
    {
        Stereo s (chunk);
        render (engine, s);
        REQUIRE (allFinite (s.l));
        seconds += 0.1;
        decayed = peakAbs (s.l) < threshold && peakAbs (s.r) < threshold;
    }

    INFO ("decay time to -90 dBFS: " << seconds << " s");
    CHECK (decayed);
}

// =============================================================================================
// Test 3 - Diffusion 0 + Feedback 0 is a pure delay
// =============================================================================================
TEST_CASE ("Test 3 - Diffusion 0 + Feedback 0: impulse response is a pure delay", "[diffusion]")
{
    const auto reverbMode = GENERATE (dsp::ReverbMode::BigStereo, dsp::ReverbMode::MediumStereo,
                                      dsp::ReverbMode::SmallStereo, dsp::ReverbMode::Mono);
    const auto modDepth = GENERATE (0.0f, 0.15f);
    CAPTURE (modeName (reverbMode), modDepth);

    dsp::EngineParameters p;
    p.mix = 1.0f;
    p.feedback = 0.0f;
    p.diffusion = 0.0f;
    p.size = 0.6f;
    p.modDepth = modDepth;
    p.reverbMode = reverbMode;
    p.pitchMode = dsp::PitchMode::Bypass;
    p.lowCutHz = 10.0f;
    p.highCutHz = 20000.0f;
    auto engine = makeEngine (p);

    auto s = impulse (static_cast<std::size_t> (kSr * 2.0), 0.5f);
    render (engine, s);

    const auto numStages = static_cast<double> (dsp::getModeSpec (reverbMode).numStages);
    const double tolerance = 8.0 + static_cast<double> (modDepth) * 64.0 * numStages;
    const auto sizeFactor = static_cast<double> (dsp::ShimmerEngine::sizeToFactor (p.size));
    for (const bool right : { false, true })
    {
        const auto& ch = right ? s.r : s.l;
        const auto peak = argMaxAbs (ch);
        const auto total = energy (ch);
        const auto lo = peak >= 3 ? peak - 3 : 0;
        const auto around = energy (ch, lo, peak + 4);

        const double expected = static_cast<double> (dsp::totalLength (reverbMode, right)) * sizeFactor;
        CAPTURE (right, peak, expected);

        CHECK (around / total > 0.98);                                  // one dominant peak
        CHECK (std::abs (static_cast<double> (peak) - expected) < tolerance); // at the network length
    }

    // Stereo modes use two delays of slightly different length.
    if (reverbMode != dsp::ReverbMode::Mono)
        CHECK (argMaxAbs (s.l) != argMaxAbs (s.r));
}

TEST_CASE ("Mono mode sums the input: a left-only impulse reaches both outputs", "[modes]")
{
    dsp::EngineParameters p;
    p.mix = 1.0f;
    p.feedback = 0.0f;
    p.diffusion = 0.0f;
    p.modDepth = 0.0f;
    p.reverbMode = dsp::ReverbMode::Mono;
    p.pitchMode = dsp::PitchMode::Bypass;
    auto engine = makeEngine (p);

    Stereo s (static_cast<std::size_t> (kSr * 2.0));
    s.l[0] = 1.0f;
    render (engine, s);

    CHECK (energy (s.l) > 0.1);
    CHECK (energy (s.r) > 0.1);
}

TEST_CASE ("Diffusion shapes the impulse response envelope", "[diffusion]")
{
    struct Shape { double concentration, t10, t90; };

    auto measure = [] (float diffusion)
    {
        dsp::EngineParameters p;
        p.mix = 1.0f;
        p.feedback = 0.0f;
        p.diffusion = diffusion;
        p.modDepth = 0.0f;
        p.pitchMode = dsp::PitchMode::Bypass;
        auto engine = makeEngine (p);
        auto s = impulse (static_cast<std::size_t> (kSr * 8.0), 0.5f);
        render (engine, s);

        const auto total = energy (s.l);
        const auto win = static_cast<std::size_t> (kSr * 0.01);
        double best = 0.0;
        for (std::size_t i = 0; i + win < s.l.size(); i += win / 2)
            best = std::max (best, energy (s.l, i, i + win));

        // Times at which 10 % and 90 % of the energy has arrived.
        double acc = 0.0, t10 = 0.0, t90 = 0.0;
        for (std::size_t i = 0; i < s.l.size(); ++i)
        {
            acc += static_cast<double> (s.l[i]) * static_cast<double> (s.l[i]);
            if (t10 == 0.0 && acc >= 0.1 * total)
                t10 = static_cast<double> (i) / kSr;
            if (acc >= 0.9 * total)
            {
                t90 = static_cast<double> (i) / kSr;
                break;
            }
        }
        return Shape { best / total, t10, t90 };
    };

    const auto d0 = measure (0.0f);
    const auto d618 = measure (0.618f);
    const auto d85 = measure (0.85f);
    CAPTURE (d0.concentration, d618.concentration, d85.concentration, d618.t10, d85.t10, d618.t90, d85.t90);

    CHECK (d0.concentration > 0.95);                   // 0.00: a single echo (pure delay)
    CHECK (d618.concentration < 0.5 * d0.concentration); // 0.618: energy spread out ("bloom")
    CHECK (d85.t10 < d618.t10);                        // 0.85: density builds up faster...
    CHECK (d85.t90 > d618.t90);                        // ...and rings for longer
}

TEST_CASE ("Changing the reverb mode while running is click-free and finite", "[modes]")
{
    dsp::EngineParameters p;
    p.feedback = 0.7f;
    auto engine = makeEngine (p);
    auto s = whiteNoise (static_cast<std::size_t> (kSr * 2.0), 0.3f);

    std::size_t pos = 0;
    const std::size_t block = 480;
    int k = 0;
    while (pos < s.size())
    {
        p.reverbMode = static_cast<dsp::ReverbMode> ((k++ / 20) % dsp::kNumReverbModes);
        engine.setParameters (p);
        engine.process (s.l.data() + pos, s.r.data() + pos, static_cast<int> (block));
        pos += block;
    }
    REQUIRE (allFinite (s.l));
    CHECK (peakAbs (s.l) < 1.5f);
}

TEST_CASE ("Host bypass passes the dry signal at unity once the ramp has finished", "[bypass]")
{
    dsp::EngineParameters p;
    p.mix = 0.5f;
    auto engine = makeEngine (p);

    auto warm = whiteNoise (static_cast<std::size_t> (kSr), 0.3f);
    render (engine, warm);

    engine.setBypassed (true);
    Stereo ramp (static_cast<std::size_t> (kSr * 0.1));
    render (engine, ramp);

    auto s = sine (4800, 1000.0, kSr, 0.5f);
    const auto reference = s;
    render (engine, s);
    for (std::size_t i = 0; i < s.size(); ++i)
        REQUIRE_THAT (s.l[i], WithinAbs (reference.l[i], 1.0e-6));
}

TEST_CASE ("Fully bypassed: no processing, and the old tail does not come back", "[bypass]")
{
    dsp::EngineParameters p;
    p.mix = 1.0f;
    p.feedback = 0.95f;
    auto engine = makeEngine (p);

    // Fill the tank, then bypass long enough for the ramp to finish.
    auto warm = whiteNoise (static_cast<std::size_t> (kSr), 0.5f);
    render (engine, warm);
    engine.setBypassed (true);
    Stereo ramp (static_cast<std::size_t> (kSr * 0.1));
    render (engine, ramp);

    // While bypassed the buffer is returned untouched (bit-exact dry, no DSP).
    auto s = whiteNoise (4096, 0.5f, 3);
    const auto reference = s;
    render (engine, s);
    CHECK (s.l == reference.l);
    CHECK (s.r == reference.r);

    // Re-enable with silence: the old tail must not resurface.
    engine.setBypassed (false);
    Stereo after (static_cast<std::size_t> (kSr * 0.5));
    render (engine, after);
    CHECK (peakAbs (after.l) < 1.0e-6f);
    CHECK (peakAbs (after.r) < 1.0e-6f);
}

TEST_CASE ("Zero-length and single-sample blocks are handled", "[host]")
{
    dsp::EngineParameters p;
    auto engine = makeEngine (p);
    float l = 0.5f, r = -0.5f;
    engine.process (&l, &r, 0);
    CHECK (l == 0.5f);
    for (int i = 0; i < 1000; ++i)
        engine.process (&l, &r, 1);
    CHECK (std::isfinite (l));
    CHECK (std::isfinite (r));
}

// =============================================================================================
// Pitch shifter in the loop (Phase 2)
// =============================================================================================
namespace
{
    constexpr std::size_t kFft = 8192;

    /** Renders a continuous sine through the engine and returns the averaged spectrum of the
        left output over the last half of the signal. */
    std::vector<double> sineResponse (const dsp::EngineParameters& p, double freq, double seconds = 4.0)
    {
        auto engine = makeEngine (p);
        auto s = sine (static_cast<std::size_t> (kSr * seconds), freq, kSr, 0.25f);
        render (engine, s);
        const auto half = s.size() / 2;
        return powerSpectrum (s.l, half, s.size() - half, kFft);
    }

    double band (const std::vector<double>& spec, double centre, double halfWidth = 15.0)
    {
        return bandEnergy (spec, kSr, kFft, centre - halfWidth, centre + halfWidth);
    }

    double ratioDb (double a, double b) { return 10.0 * std::log10 (std::max (a, 1.0e-30) / std::max (b, 1.0e-30)); }

    dsp::EngineParameters pitchParams (dsp::PitchMode mode, float semitones, float feedback)
    {
        dsp::EngineParameters p;
        p.mix = 1.0f;
        p.pitchMode = mode;
        p.pitchSemitones = semitones;
        p.feedback = feedback;
        p.lowCutHz = 10.0f;
        p.highCutHz = 20000.0f;
        return p;
    }
} // namespace

TEST_CASE ("Test 4 - Feedback 0 + Pitch Single +12: no octave in the wet signal", "[pitch]")
{
    const auto spec = sineResponse (pitchParams (dsp::PitchMode::Single, 12.0f, 0.0f), 440.0);
    const double octaveDb = ratioDb (band (spec, 880.0), band (spec, 440.0));
    CAPTURE (octaveDb);
    CHECK (octaveDb < -60.0);
}

TEST_CASE ("Test 5 - High feedback: Single adds energy above the fundamental vs Bypass", "[pitch]")
{
    auto aboveFraction = [] (dsp::PitchMode mode)
    {
        const auto spec = sineResponse (pitchParams (mode, 12.0f, 0.85f), 440.0);
        const double total = bandEnergy (spec, kSr, kFft, 20.0, 24000.0);
        return bandEnergy (spec, kSr, kFft, 600.0, 24000.0) / total;
    };

    const double bypass = aboveFraction (dsp::PitchMode::Bypass);
    const double single = aboveFraction (dsp::PitchMode::Single);
    CAPTURE (bypass, single, ratioDb (single, bypass));
    CHECK (single > 0.2);
    CHECK (ratioDb (single, bypass) > 10.0);
}

TEST_CASE ("Single +12 with feedback stacks rising octaves", "[pitch]")
{
    const auto spec = sineResponse (pitchParams (dsp::PitchMode::Single, 12.0f, 0.8f), 220.0, 6.0);
    const double f0 = band (spec, 220.0);
    CAPTURE (ratioDb (band (spec, 440.0), f0), ratioDb (band (spec, 880.0), f0), ratioDb (band (spec, 1760.0), f0));
    CHECK (ratioDb (band (spec, 440.0), f0) > -20.0);
    CHECK (ratioDb (band (spec, 880.0), f0) > -30.0);
    CHECK (ratioDb (band (spec, 1760.0), f0) > -40.0);
    // ...and energy at those octaves stands well above the gaps between them.
    CHECK (ratioDb (band (spec, 880.0), band (spec, 660.0)) > 6.0);
}

TEST_CASE ("Pitch -12 shifts downwards", "[pitch]")
{
    const auto spec = sineResponse (pitchParams (dsp::PitchMode::Single, -12.0f, 0.7f), 880.0);
    CHECK (ratioDb (band (spec, 440.0), band (spec, 880.0)) > -20.0);
    CHECK (ratioDb (band (spec, 440.0), band (spec, 1760.0)) > 10.0);
}

TEST_CASE ("Pitch +7 produces a fifth", "[pitch]")
{
    const auto spec = sineResponse (pitchParams (dsp::PitchMode::Single, 7.0f, 0.5f), 440.0);
    const double fifth = band (spec, 440.0 * std::pow (2.0, 7.0 / 12.0), 10.0);
    CHECK (ratioDb (fifth, band (spec, 620.0, 10.0)) > 10.0);
    CHECK (ratioDb (fifth, band (spec, 700.0, 10.0)) > 10.0);
}

TEST_CASE ("Dual shifts up and down at the same time", "[pitch]")
{
    const auto dual = sineResponse (pitchParams (dsp::PitchMode::Dual, 12.0f, 0.6f), 880.0);
    const auto single = sineResponse (pitchParams (dsp::PitchMode::Single, 12.0f, 0.6f), 880.0);

    const double dualDown = ratioDb (band (dual, 440.0), band (dual, 880.0));
    const double dualUp = ratioDb (band (dual, 1760.0), band (dual, 880.0));
    const double singleDown = ratioDb (band (single, 440.0), band (single, 880.0));
    CAPTURE (dualDown, dualUp, singleDown);
    CHECK (dualDown > -25.0);
    CHECK (dualUp > -25.0);
    CHECK (dualDown > singleDown + 15.0);
}

TEST_CASE ("Reverse modes still pitch shift", "[pitch]")
{
    const auto mode = GENERATE (dsp::PitchMode::SingleReverse, dsp::PitchMode::DualReverse);
    CAPTURE (static_cast<int> (mode));
    const auto spec = sineResponse (pitchParams (mode, 12.0f, 0.7f), 440.0);
    CHECK (ratioDb (band (spec, 880.0), band (spec, 440.0)) > -25.0);
    CHECK (ratioDb (band (spec, 880.0), band (spec, 660.0)) > 6.0);
}

TEST_CASE ("Switching pitch modes and pitch while running stays bounded", "[pitch]")
{
    auto p = pitchParams (dsp::PitchMode::Single, 12.0f, 0.9f);
    auto engine = makeEngine (p);
    auto s = whiteNoise (static_cast<std::size_t> (kSr * 4.0), 0.3f);

    const std::size_t block = 256;
    for (std::size_t pos = 0, k = 0; pos < s.size(); pos += block, ++k)
    {
        p.pitchMode = static_cast<dsp::PitchMode> ((k / 19) % dsp::kNumPitchModes);
        p.pitchSemitones = static_cast<float> (static_cast<int> (k % 25) - 12);
        engine.setParameters (p);
        engine.process (s.l.data() + pos, s.r.data() + pos, static_cast<int> (std::min (block, s.size() - pos)));
    }
    REQUIRE (allFinite (s.l));
    REQUIRE (allFinite (s.r));
    CHECK (peakAbs (s.l) < 1.5f);
}

// =============================================================================================
// Loop filters, modulation and host independence (Phase 3)
// =============================================================================================
namespace
{
    /** Spectrum of the late tail (from tailStart s on) after a short noise burst. */
    std::vector<double> tailSpectrum (const dsp::EngineParameters& p, double tailStart, double seconds)
    {
        auto engine = makeEngine (p);
        auto s = whiteNoise (static_cast<std::size_t> (kSr * seconds), 0.0f);
        auto burst = whiteNoise (static_cast<std::size_t> (kSr * 0.05), 0.5f, 77);
        std::copy (burst.l.begin(), burst.l.end(), s.l.begin());
        std::copy (burst.r.begin(), burst.r.end(), s.r.begin());
        render (engine, s);
        const auto from = static_cast<std::size_t> (kSr * tailStart);
        return powerSpectrum (s.l, from, s.size() - from, kFft);
    }

    double centroid (const std::vector<double>& spec)
    {
        double num = 0.0, den = 0.0;
        for (std::size_t k = 1; k < spec.size(); ++k)
        {
            num += spec[k] * static_cast<double> (k) * kSr / static_cast<double> (kFft);
            den += spec[k];
        }
        return num / den;
    }
} // namespace

TEST_CASE ("High Cut in the loop darkens the tail cumulatively", "[filters]")
{
    dsp::EngineParameters p;
    p.mix = 1.0f;
    p.feedback = 0.85f;
    p.pitchMode = dsp::PitchMode::Bypass;
    p.lowCutHz = 10.0f;

    p.highCutHz = 16000.0f;
    const double open = centroid (tailSpectrum (p, 2.0, 4.0));
    p.highCutHz = 1500.0f;
    const double closed = centroid (tailSpectrum (p, 2.0, 4.0));
    CAPTURE (open, closed);
    CHECK (closed < 0.5 * open);
}

TEST_CASE ("Low Cut in the loop thins the tail cumulatively", "[filters]")
{
    dsp::EngineParameters p;
    p.mix = 1.0f;
    p.feedback = 0.85f;
    p.pitchMode = dsp::PitchMode::Bypass;
    p.highCutHz = 20000.0f;

    p.lowCutHz = 10.0f;
    const auto full = tailSpectrum (p, 2.0, 4.0);
    p.lowCutHz = 1000.0f;
    const auto thin = tailSpectrum (p, 2.0, 4.0);

    const double lowFull = bandEnergy (full, kSr, kFft, 20.0, 300.0) / bandEnergy (full, kSr, kFft, 20.0, 20000.0);
    const double lowThin = bandEnergy (thin, kSr, kFft, 20.0, 300.0) / bandEnergy (thin, kSr, kFft, 20.0, 20000.0);
    CAPTURE (lowFull, lowThin);
    CHECK (lowThin < 0.1 * lowFull);
}

TEST_CASE ("Color Dark removes the top octave of the tail", "[filters]")
{
    dsp::EngineParameters p;
    p.mix = 1.0f;
    p.feedback = 0.8f;
    p.pitchMode = dsp::PitchMode::Single;
    p.lowCutHz = 10.0f;
    p.highCutHz = 20000.0f;

    p.colorMode = dsp::ColorMode::Bright;
    const auto bright = tailSpectrum (p, 1.0, 3.0);
    p.colorMode = dsp::ColorMode::Dark;
    const auto dark = tailSpectrum (p, 1.0, 3.0);

    const double topBright = bandEnergy (bright, kSr, kFft, 13000.0, 20000.0) / bandEnergy (bright, kSr, kFft, 20.0, 20000.0);
    const double topDark = bandEnergy (dark, kSr, kFft, 13000.0, 20000.0) / bandEnergy (dark, kSr, kFft, 20.0, 20000.0);
    CAPTURE (topBright, topDark);
    CHECK (ratioDb (topDark, topBright) < -30.0);
}

TEST_CASE ("Modulation detunes: the tail of a sine spreads around its frequency", "[modulation]")
{
    auto spread = [] (float depth)
    {
        dsp::EngineParameters p;
        p.mix = 1.0f;
        p.feedback = 0.7f;
        p.diffusion = 0.9f;
        p.modDepth = depth;
        p.modRateHz = 2.0f;
        p.pitchMode = dsp::PitchMode::Bypass;
        p.highCutHz = 20000.0f;
        const auto spec = sineResponse (p, 1000.0, 6.0);
        return band (spec, 1000.0, 60.0) / band (spec, 1000.0, 3.0);
    };

    const double still = spread (0.0f);
    const double moving = spread (1.0f);
    CAPTURE (still, moving);
    CHECK (moving > 1.5 * still);
}

TEST_CASE ("Test 7 - RT60 is sample-rate independent (44.1 kHz vs 96 kHz, +-15 %)", "[samplerate]")
{
    const auto pitchMode = GENERATE (dsp::PitchMode::Bypass, dsp::PitchMode::Single);
    const auto reverbMode = GENERATE (dsp::ReverbMode::MediumStereo, dsp::ReverbMode::SmallStereo);
    CAPTURE (static_cast<int> (pitchMode), modeName (reverbMode));

    auto measure = [&] (double sr)
    {
        dsp::EngineParameters p;
        p.mix = 1.0f;
        p.feedback = 0.75f;
        p.pitchMode = pitchMode;
        p.reverbMode = reverbMode;
        auto engine = makeEngine (p, sr);
        auto s = impulse (static_cast<std::size_t> (sr * 20.0), 0.5f);
        render (engine, s);
        return rt60 (s.l, sr);
    };

    const double rt44 = measure (44100.0);
    const double rt96 = measure (96000.0);
    CAPTURE (rt44, rt96);
    CHECK (rt44 > 0.5);
    CHECK (std::abs (rt96 / rt44 - 1.0) < 0.15);
}

TEST_CASE ("Works at 44.1, 48, 88.2, 96 and 192 kHz", "[samplerate]")
{
    const auto sr = GENERATE (44100.0, 48000.0, 88200.0, 96000.0, 192000.0);
    CAPTURE (sr);

    dsp::EngineParameters p;
    p.feedback = 0.95f;
    p.colorMode = dsp::ColorMode::Dark;
    auto engine = makeEngine (p, sr);
    auto s = whiteNoise (static_cast<std::size_t> (sr * 3.0), 0.5f);
    render (engine, s, { 1, 33, 1024 });
    REQUIRE (allFinite (s.l));
    REQUIRE (allFinite (s.r));
    CHECK (peakAbs (s.l) < 2.0f);
    CHECK (engine.getLastWetRms (0) > 0.01f);
}

TEST_CASE ("Test 8 - Variable block sizes produce the same output as a single block", "[blocks]")
{
    const auto pitchMode = GENERATE (dsp::PitchMode::Single, dsp::PitchMode::DualReverse);
    CAPTURE (static_cast<int> (pitchMode));

    dsp::EngineParameters p;
    p.feedback = 0.9f;
    p.pitchMode = pitchMode;
    p.modDepth = 0.5f;
    p.colorMode = dsp::ColorMode::Dark;

    const auto input = whiteNoise (static_cast<std::size_t> (kSr * 3.0), 0.4f);

    auto reference = input;
    auto engineA = makeEngine (p);
    render (engineA, reference, { static_cast<int> (reference.size()) });

    auto chunked = input;
    auto engineB = makeEngine (p);
    render (engineB, chunked, { 1, 7, 64, 513, 4096 });

    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < input.size(); ++i)
        maxDiff = std::max ({ maxDiff, std::abs (reference.l[i] - chunked.l[i]), std::abs (reference.r[i] - chunked.r[i]) });
    CAPTURE (maxDiff);
    CHECK (maxDiff <= 1.0e-6f);
}

TEST_CASE ("prepare() can be called repeatedly and at a new sample rate", "[host]")
{
    dsp::EngineParameters p;
    dsp::ShimmerEngine engine;
    engine.setParameters (p);
    engine.prepare (48000.0);
    engine.prepare (48000.0);
    engine.prepare (96000.0);
    engine.prepare (44100.0);

    auto s = whiteNoise (static_cast<std::size_t> (44100.0), 0.5f);
    render (engine, s, { 4096, 8192 }); // larger than any "maximum block" a host announced
    REQUIRE (allFinite (s.l));
    CHECK (engine.getSampleRate() == 44100.0);
}
