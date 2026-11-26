// End-to-end audio tests, one per control. Every test goes through the real plugin
// (APVTS parameter -> ParameterRefs -> ShimmerEngine -> processBlock), so they check both that each
// knob is wired to the right place and that it does what its label promises.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestHelpers.h"

using namespace ab::plugintest;
namespace id = ab::params::id;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr size_t sec (double s) { return static_cast<size_t> (s * kSampleRate); }

    // Choice indices (order of the parameter's options).
    enum ReverbIdx { kMono = 0, kBig = 1, kMedium = 2, kSmall = 3 };
    enum PitchIdx { kSingle = 0, kDual = 1, kSingleRev = 2, kDualRev = 3, kPitchBypass = 4 };
    enum ColorIdx { kBright = 0, kDark = 1 };

    /** A "probe" setting: fully wet, pitch shifter off, open filters, no modulation. Each test then
        changes only what it is about. */
    std::map<juce::String, float> probe()
    {
        return { { id::mix, 100.0f },       { id::pitch, 12.0f },      { id::feedback, 0.0f },
                 { id::diffusion, 0.0f },   { id::size, 60.0f },       { id::lowCut, 10.0f },
                 { id::highCut, 20000.0f }, { id::modRate, 0.4f },     { id::modDepth, 0.0f },
                 { id::reverbMode, kMedium }, { id::pitchMode, kPitchBypass }, { id::colorMode, kBright } };
    }

    std::map<juce::String, float> with (std::map<juce::String, float> base, const std::map<juce::String, float>& changes)
    {
        for (const auto& [k, v] : changes)
            base[k] = v;
        return base;
    }

    size_t firstArrival (const std::vector<float>& v, float threshold)
    {
        for (size_t i = 0; i < v.size(); ++i)
            if (std::abs (v[i]) > threshold)
                return i;
        return v.size();
    }

    double tailDb (const Signal& s, double from, double to)
    {
        return toDb (std::sqrt (0.5 * (rms (s.l, sec (from), sec (to)) * rms (s.l, sec (from), sec (to))
                                       + rms (s.r, sec (from), sec (to)) * rms (s.r, sec (from), sec (to)))));
    }

    /** Band power around a frequency (+-3 %), both channels, over the tail window. */
    double tonePower (const Signal& s, double freq, double from, double length)
    {
        return bandPower (s.l, sec (from), sec (length), freq * 0.97, freq * 1.03)
               + bandPower (s.r, sec (from), sec (length), freq * 0.97, freq * 1.03);
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = from; i < to; ++i)
        {
            ab += static_cast<double> (a[i]) * b[i];
            aa += static_cast<double> (a[i]) * a[i];
            bb += static_cast<double> (b[i]) * b[i];
        }
        return ab / std::sqrt (aa * bb + 1.0e-30);
    }
} // namespace

// =============================================================================================
// Knobs
// =============================================================================================
TEST_CASE ("Knob Mix: dry gain follows cos(mix*pi/2), 0 % is the untouched input", "[knobs][mix]")
{
    // Diffusion 0 + Feedback 0 make the wet a pure delay, so sample 0 is dry only.
    for (const float mixPercent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const auto out = render (with (probe(), { { id::mix, mixPercent } }), impulse (sec (0.2), 0.5f));
        const auto expectedDry = 0.5 * std::cos (mixPercent * 0.01 * juce::MathConstants<double>::halfPi);
        INFO ("mix " << mixPercent << " %");
        CHECK_THAT (out.l[0], WithinAbs (expectedDry, 1.0e-4));
        CHECK_THAT (out.r[0], WithinAbs (expectedDry, 1.0e-4));
    }

    // Mix 0 % on a dense, long reverb: the output is the input, sample for sample.
    const auto in = noiseBurst (sec (1.0), sec (1.0), 0.3f);
    const auto dry = render ({ { id::mix, 0.0f }, { id::feedback, 90.0f } }, in);
    float maxErr = 0.0f;
    for (size_t i = 0; i < in.size(); ++i)
        maxErr = std::max ({ maxErr, std::abs (dry.l[i] - in.l[i]), std::abs (dry.r[i] - in.r[i]) });
    CHECK (maxErr < 1.0e-5f);
}

TEST_CASE ("Knob Pitch: the tail moves to the interval set on the knob", "[knobs][pitch]")
{
    const double f0 = 400.0;
    const auto in = sineBurst (sec (3.0), sec (0.5), f0, 0.4f);
    auto shimmer = [&] (float semitones)
    {
        return render (with (probe(), { { id::pitch, semitones }, { id::feedback, 75.0f }, { id::diffusion, 0.7f },
                                        { id::pitchMode, kSingle } }),
                       in);
    };

    struct Case { float st; double expectHz; double competitorHz; };
    for (const auto& c : { Case { 12.0f, 800.0, 599.3 }, Case { 7.0f, 599.3, 800.0 }, Case { -12.0f, 200.0, 800.0 },
                           Case { 5.0f, 533.9, 800.0 } })
    {
        const auto out = shimmer (c.st);
        const auto wanted = tonePower (out, c.expectHz, 1.0, 1.5);
        const auto other = tonePower (out, c.competitorHz, 1.0, 1.5);
        INFO ("pitch " << c.st << " st: " << toDb (std::sqrt (wanted)) << " dB at " << c.expectHz << " Hz vs "
                       << toDb (std::sqrt (other)) << " dB at " << c.competitorHz << " Hz");
        CHECK (wanted > other * 10.0); // > 10 dB
    }

    // Pitch 0: the loop keeps the original note, no octave appears.
    const auto unison = shimmer (0.0f);
    CHECK (tonePower (unison, f0, 1.0, 1.5) > 100.0 * tonePower (unison, 2.0 * f0, 1.0, 1.5));
}

TEST_CASE ("Knob Feedback: more feedback, longer tail; 0 % leaves no recirculation", "[knobs][feedback]")
{
    const auto in = noiseBurst (sec (4.0), sec (0.2), 0.5f);
    std::vector<double> tails;
    for (const float fb : { 0.0f, 40.0f, 70.0f, 90.0f })
    {
        const auto out = render (with (probe(), { { id::feedback, fb }, { id::diffusion, 0.7f } }), in);
        REQUIRE (finite (out));
        tails.push_back (tailDb (out, 2.0, 3.0));
        INFO ("feedback " << fb << " %: tail " << tails.back() << " dB");
        CHECK (true);
    }
    CHECK (tails[0] < -90.0);
    for (size_t i = 1; i < tails.size(); ++i)
        CHECK (tails[i] > tails[i - 1] + 6.0);
}

TEST_CASE ("Knob Diffusion: 0 = discrete echoes, high = dense wash", "[knobs][diffusion]")
{
    auto density = [] (float diffusion)
    {
        const auto out = render (with (probe(), { { id::diffusion, diffusion } }), impulse (sec (0.6)));
        const float thr = 1.0e-3f * peak (out);
        size_t count = 0;
        for (size_t i = 0; i < out.size(); ++i)
            count += std::abs (out.l[i]) > thr ? 1u : 0u;
        return count;
    };
    const auto sparse = density (0.0f);
    const auto medium = density (0.5f);
    const auto dense = density (0.85f);
    INFO ("non-zero samples: " << sparse << " / " << medium << " / " << dense);
    CHECK (sparse < 50);
    CHECK (medium > 100 * sparse);
    CHECK (dense > 100 * sparse);
}

TEST_CASE ("Knob Size: bigger size, later first arrival", "[knobs][size]")
{
    std::vector<size_t> arrivals;
    for (const float size : { 0.0f, 30.0f, 60.0f, 100.0f })
    {
        const auto out = render (with (probe(), { { id::size, size } }), impulse (sec (1.5)));
        arrivals.push_back (firstArrival (out.l, 0.01f));
    }
    INFO ("arrivals (samples): " << arrivals[0] << ", " << arrivals[1] << ", " << arrivals[2] << ", " << arrivals[3]);
    for (size_t i = 1; i < arrivals.size(); ++i)
        CHECK (arrivals[i] > arrivals[i - 1]);
    // Size spans one decade (sizeFactor 0.1 .. 1.0).
    CHECK (static_cast<double> (arrivals.back()) / static_cast<double> (arrivals.front()) > 7.0);
}

TEST_CASE ("Knob Low Cut: removes low end from the tail", "[knobs][filters]")
{
    const auto in = noiseBurst (sec (3.0), sec (0.3), 0.5f);
    auto lowFraction = [&] (float lowCut)
    {
        const auto out = render (with (probe(), { { id::lowCut, lowCut }, { id::feedback, 80.0f }, { id::diffusion, 0.7f } }),
                                 in);
        return 1.0 - highBandRatio (out.l, sec (1.0), sec (2.5), 200.0);
    };
    const auto open = lowFraction (10.0f);
    const auto mid = lowFraction (300.0f);
    const auto closed = lowFraction (2000.0f);
    INFO ("energy below 200 Hz: " << open << " / " << mid << " / " << closed);
    CHECK (mid < open * 0.5);
    CHECK (closed < open * 0.05);
}

TEST_CASE ("Knob High Cut: removes high end from the tail", "[knobs][filters]")
{
    const auto in = noiseBurst (sec (3.0), sec (0.3), 0.5f);
    auto highFraction = [&] (float highCut)
    {
        const auto out = render (with (probe(), { { id::highCut, highCut }, { id::feedback, 80.0f }, { id::diffusion, 0.7f } }),
                                 in);
        return highBandRatio (out.l, sec (1.0), sec (2.5), 4000.0);
    };
    const auto open = highFraction (20000.0f);
    const auto mid = highFraction (5000.0f);
    const auto closed = highFraction (800.0f);
    INFO ("energy above 4 kHz: " << open << " / " << mid << " / " << closed);
    CHECK (mid < open * 0.6);
    CHECK (closed < open * 0.05);
}

TEST_CASE ("Knob Mod Depth: detunes the tail; 0 makes Mod Rate irrelevant", "[knobs][modulation]")
{
    const double f0 = 1000.0;
    const auto in = sineBurst (sec (3.0), sec (0.5), f0, 0.4f);
    auto render1 = [&] (float depth, float rate)
    {
        return render (with (probe(), { { id::modDepth, depth }, { id::modRate, rate }, { id::feedback, 80.0f },
                                        { id::diffusion, 0.6f } }),
                       in);
    };
    auto spread = [&] (const Signal& out)
    {
        // Energy that leaked out of +-3 Hz around the note, relative to the energy on it.
        const auto onNote = bandPower (out.l, sec (1.0), sec (1.5), f0 - 3.0, f0 + 3.0);
        const auto around = bandPower (out.l, sec (1.0), sec (1.5), f0 - 60.0, f0 + 60.0) - onNote;
        return around / (onNote + 1.0e-30);
    };

    const auto still = render1 (0.0f, 5.0f);
    const auto deep = render1 (100.0f, 5.0f);
    INFO ("spread depth 0: " << spread (still) << ", depth 100: " << spread (deep));
    CHECK (spread (deep) > 3.0 * spread (still));

    // With depth 0 the rate knob must not change a single sample.
    const auto stillSlow = render1 (0.0f, 0.02f);
    float maxDiff = 0.0f;
    for (size_t i = 0; i < still.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (still.l[i] - stillSlow.l[i]));
    CHECK (maxDiff < 1.0e-6f);
}

TEST_CASE ("Knob Mod Rate: faster modulation, wider detune", "[knobs][modulation]")
{
    const double f0 = 1000.0;
    const auto in = sineBurst (sec (3.0), sec (0.5), f0, 0.4f);
    auto spreadAt = [&] (float rate)
    {
        const auto out = render (with (probe(), { { id::modDepth, 100.0f }, { id::modRate, rate }, { id::feedback, 80.0f },
                                                  { id::diffusion, 0.6f } }),
                                 in);
        REQUIRE (finite (out));
        const auto onNote = bandPower (out.l, sec (1.0), sec (1.5), f0 - 3.0, f0 + 3.0);
        const auto around = bandPower (out.l, sec (1.0), sec (1.5), f0 - 60.0, f0 + 60.0) - onNote;
        return around / (onNote + 1.0e-30);
    };
    const auto slow = spreadAt (0.02f);
    const auto fast = spreadAt (8.0f);
    INFO ("spread at 0.02 Hz: " << slow << ", at 8 Hz: " << fast);
    CHECK (fast > 2.0 * slow);
}

// =============================================================================================
// Mode selectors
// =============================================================================================
TEST_CASE ("Reverb Mode: Mono sums the input, stereo modes keep left and right apart", "[modes][reverbmode]")
{
    auto sided = [] (bool left)
    {
        Signal s (sec (1.0));
        (left ? s.l : s.r)[0] = 0.5f;
        return s;
    };
    for (const int mode : { kMono, kBig, kMedium, kSmall })
    {
        const auto settings = with (probe(), { { id::reverbMode, static_cast<float> (mode) }, { id::diffusion, 0.7f },
                                               { id::feedback, 50.0f } });
        const auto fromLeft = render (settings, sided (true));
        const auto fromRight = render (settings, sided (false));
        double diff = 0.0;
        for (size_t i = 0; i < fromLeft.size(); ++i)
            diff += std::abs (fromLeft.l[i] - fromRight.l[i]) + std::abs (fromLeft.r[i] - fromRight.r[i]);
        INFO ("mode " << mode << " L/R difference " << diff);
        if (mode == kMono)
            CHECK (diff < 1.0e-3);
        else
            CHECK (diff > 1.0);

        // Whatever the mode, a left-only impulse reaches both outputs.
        CHECK (rms (fromLeft.r) > 1.0e-4);
    }
}

TEST_CASE ("Reverb Mode: every mode sounds different, Small decays fastest", "[modes][reverbmode]")
{
    const auto in = noiseBurst (sec (3.0), sec (0.2), 0.5f);
    std::vector<Signal> outs;
    std::vector<double> tails;
    for (const int mode : { kMono, kBig, kMedium, kSmall })
    {
        outs.push_back (render (with (probe(), { { id::reverbMode, static_cast<float> (mode) }, { id::diffusion, 0.7f },
                                                 { id::feedback, 70.0f } }),
                                in));
        tails.push_back (tailDb (outs.back(), 1.5, 2.5));
    }
    INFO ("tail dB mono/big/medium/small: " << tails[0] << " " << tails[1] << " " << tails[2] << " " << tails[3]);
    CHECK (tails[kSmall] < tails[kMedium]);
    CHECK (tails[kMedium] < tails[kBig]);

    for (size_t a = 0; a < outs.size(); ++a)
        for (size_t b = a + 1; b < outs.size(); ++b)
            CHECK (std::abs (correlation (outs[a].l, outs[b].l, sec (0.3), sec (1.5))) < 0.5);
}

TEST_CASE ("Pitch Mode: each mode produces its own set of intervals", "[modes][pitchmode]")
{
    const double f0 = 400.0;
    const auto in = sineBurst (sec (3.0), sec (0.5), f0, 0.4f);
    auto run = [&] (int mode)
    {
        return render (with (probe(), { { id::pitchMode, static_cast<float> (mode) }, { id::pitch, 12.0f },
                                        { id::feedback, 75.0f }, { id::diffusion, 0.7f } }),
                       in);
    };
    struct Layers { double up, down, root; };
    auto layers = [&] (const Signal& s)
    { return Layers { tonePower (s, 2.0 * f0, 1.0, 1.5), tonePower (s, 0.5 * f0, 1.0, 1.5), tonePower (s, f0, 1.0, 1.5) }; };

    const auto bypass = layers (run (kPitchBypass));
    const auto single = layers (run (kSingle));
    const auto dual = layers (run (kDual));
    const auto singleRev = layers (run (kSingleRev));
    const auto dualRev = layers (run (kDualRev));

    // Bypass: a plain reverb, the note stays where it is.
    CHECK (bypass.root > 100.0 * bypass.up);
    CHECK (bypass.root > 100.0 * bypass.down);
    // Single +12: octave up, nothing below.
    CHECK (single.up > 10.0 * single.down);
    CHECK (single.up > 100.0 * bypass.up);
    // Dual: both directions.
    CHECK (dual.up > 100.0 * bypass.up);
    CHECK (dual.down > 100.0 * bypass.down);
    // Reverse modes keep shifting.
    CHECK (singleRev.up > 10.0 * singleRev.down);
    CHECK (dualRev.up > 100.0 * bypass.up);
    CHECK (dualRev.down > 100.0 * bypass.down);

    // Reverse really is a different sound from forward grains.
    const auto fwd = run (kSingle);
    const auto rev = run (kSingleRev);
    CHECK (std::abs (correlation (fwd.l, rev.l, sec (0.6), sec (2.0))) < 0.5);
}

TEST_CASE ("Color Mode: Dark removes the top of the spectrum, Bright keeps it", "[modes][color]")
{
    const auto in = noiseBurst (sec (2.0), sec (0.02), 0.5f);
    auto top = [&] (int color)
    {
        const auto out = render (with (probe(), { { id::colorMode, static_cast<float> (color) }, { id::feedback, 70.0f },
                                                  { id::diffusion, 0.7f } }),
                                 in);
        // Spectral tilt of the tail: 13-17 kHz against 1-3 kHz, in 100 ms windows.
        double hi = 0.0, lo = 0.0;
        for (const double t : { 1.2, 1.3, 1.4, 1.5 }) // after several passes through the loop
        {
            hi += bandPower (out.l, sec (t), sec (0.1), 13000.0, 17000.0);
            lo += bandPower (out.l, sec (t), sec (0.1), 1000.0, 3000.0);
        }
        return hi / lo;
    };
    const auto bright = top (kBright);
    const auto dark = top (kDark);
    INFO ("13-17 kHz vs 1-3 kHz: bright " << toDb (std::sqrt (bright)) << " dB, dark " << toDb (std::sqrt (dark)) << " dB");
    CHECK (dark < bright * 0.01); // at least 20 dB darker
}

// =============================================================================================
// Output quality
// =============================================================================================
TEST_CASE ("Output: digital silence in gives digital silence out (every preset)", "[output]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const int numPresets = AuroraBloomProcessor().getPresetManager().getNumFactoryPresets();
    for (int i = 0; i < numPresets; ++i)
    {
        AuroraBloomProcessor fresh;
        fresh.getPresetManager().loadPreset (i);
        prepareStereo (fresh);
        const auto out = process (fresh, Signal (sec (2.0)));
        INFO ("preset " << fresh.getPresetManager().getCurrentName());
        CHECK (peak (out) == 0.0f);
    }
}

TEST_CASE ("Output: every factory preset is finite, safe and level-matched", "[output][presets]")
{
    const auto in = noiseBurst (sec (8.0), sec (1.0), 0.25f); // ~-17 dBFS RMS burst
    const double inDb = toDb (rms (in.l, 0, sec (1.0)));
    juce::ScopedJuceInitialiser_GUI juceInit;

    AuroraBloomProcessor probeProc;
    const int numPresets = probeProc.getPresetManager().getNumFactoryPresets();
    REQUIRE (numPresets == 12);

    for (int i = 0; i < numPresets; ++i)
    {
        AuroraBloomProcessor p;
        REQUIRE (p.getPresetManager().loadPreset (i));
        prepareStereo (p);
        const auto out = process (p, in);
        const auto name = p.getPresetManager().getCurrentName();
        const double burstDb = toDb (std::sqrt (0.5 * (std::pow (rms (out.l, sec (0.2), sec (1.0)), 2.0)
                                                       + std::pow (rms (out.r, sec (0.2), sec (1.0)), 2.0))));
        double mean = 0.0;
        for (size_t k = sec (1.0); k < out.size(); ++k)
            mean += out.l[k] + out.r[k];
        mean /= 2.0 * static_cast<double> (out.size() - sec (1.0));

        INFO (name << ": peak " << toDb (peak (out)) << " dBFS, level " << (burstDb - inDb) << " dB vs input, DC "
                   << mean);
        REQUIRE (finite (out));
        CHECK (peak (out) < 1.0f);                       // never clips at a normal input level
        CHECK (std::abs (burstDb - inDb) < 9.0);         // no surprising volume jump between presets
        CHECK (std::abs (mean) < 1.0e-3);                // no DC build-up in the loop
        CHECK (rms (out.l, sec (1.02), sec (1.2)) > 1.0e-3); // there is a reverb tail after the burst
    }
}

TEST_CASE ("Output: the stereo image is wide (decorrelated tail from a mono source)", "[output][stereo]")
{
    Signal in (sec (3.0));
    juce::Random rng (3);
    for (size_t i = 0; i < sec (0.3); ++i)
        in.l[i] = in.r[i] = 0.4f * (rng.nextFloat() * 2.0f - 1.0f);

    for (const int mode : { kMono, kBig, kMedium, kSmall })
    {
        const auto out = render ({ { id::mix, 100.0f }, { id::reverbMode, static_cast<float> (mode) } }, in);
        const auto c = correlation (out.l, out.r, sec (0.6), sec (2.0));
        INFO ("mode " << mode << " L/R correlation " << c);
        CHECK (std::abs (c) < 0.6);
    }
}

TEST_CASE ("Output: the reported tail covers the actual decay", "[output][host]")
{
    AuroraBloomProcessor p;
    prepareStereo (p);
    CHECK (p.getLatencySamples() == 0);

    const double tail = p.getTailLengthSeconds();
    const auto out = process (p, noiseBurst (sec (std::min (tail, 30.0) + 1.0), sec (0.2), 0.5f));
    // After the reported tail the output must be below -90 dBFS.
    const auto end = out.size();
    CHECK (toDb (peak (out, end - sec (0.5))) < -90.0);
}

TEST_CASE ("Automation: jumping any knob to its extremes never clicks or blows up", "[output][automation]")
{
    // A steady note through the default settings; each knob is flipped between its minimum and
    // maximum every 100 ms. The largest sample-to-sample step must stay in the range of what the
    // plugin produces anyway when parked at either extreme.
    const auto in = sineBurst (sec (3.0), sec (3.0), 220.0, 0.3f);

    auto maxStep = [] (const Signal& s, size_t from)
    {
        float m = 0.0f;
        for (size_t i = from + 1; i < s.size(); ++i)
            m = std::max ({ m, std::abs (s.l[i] - s.l[i - 1]), std::abs (s.r[i] - s.r[i - 1]) });
        return m;
    };

    for (const juce::String knob : { id::mix, id::pitch, id::feedback, id::diffusion, id::size, id::lowCut, id::highCut,
                                     id::modRate, id::modDepth })
    {
        AuroraBloomProcessor ref;
        auto* param = ref.getApvts().getParameter (knob);
        const auto lo = param->convertFrom0to1 (0.0f);
        const auto hi = param->convertFrom0to1 (1.0f);

        const auto steadyLo = render ({ { knob, lo } }, in);
        const auto steadyHi = render ({ { knob, hi } }, in);
        const float baseline = std::max (maxStep (steadyLo, sec (1.0)), maxStep (steadyHi, sec (1.0)));

        AuroraBloomProcessor p;
        prepareStereo (p);
        Signal out (in.size());
        const size_t chunk = sec (0.1);
        for (size_t pos = 0, k = 0; pos < in.size(); pos += chunk, ++k)
        {
            if (pos >= sec (1.0))
                setPlain (p, knob, (k % 2) != 0 ? hi : lo);
            Signal part (std::min (chunk, in.size() - pos));
            std::copy_n (in.l.begin() + static_cast<long> (pos), part.size(), part.l.begin());
            std::copy_n (in.r.begin() + static_cast<long> (pos), part.size(), part.r.begin());
            const auto res = process (p, part);
            std::copy (res.l.begin(), res.l.end(), out.l.begin() + static_cast<long> (pos));
            std::copy (res.r.begin(), res.r.end(), out.r.begin() + static_cast<long> (pos));
        }

        const float automated = maxStep (out, sec (1.0));
        INFO (knob << ": max step " << automated << " (parked: " << baseline << "), peak " << peak (out));
        REQUIRE (finite (out));
        CHECK (automated < 1.5f * baseline + 0.01f);
        CHECK (peak (out) < 1.5f);
    }
}
