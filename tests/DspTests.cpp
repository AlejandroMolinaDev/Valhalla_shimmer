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
