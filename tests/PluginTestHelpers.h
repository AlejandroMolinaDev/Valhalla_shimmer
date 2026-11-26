#pragma once

// Helpers shared by the plugin-level test files (they drive the real AuroraBloomProcessor, i.e.
// APVTS -> ParameterRefs -> ShimmerEngine, exactly as a host does).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <map>
#include <vector>

#include "Parameters.h"
#include "PluginProcessor.h"

namespace ab::plugintest
{

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

inline void setPlain (AuroraBloomProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getApvts().getParameter (id);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

inline float getPlain (AuroraBloomProcessor& p, const juce::String& id)
{
    return p.getApvts().getRawParameterValue (id)->load();
}

/** Applies a set of plain values (choice parameters take the option index). */
inline void applySettings (AuroraBloomProcessor& p, const std::map<juce::String, float>& settings)
{
    for (const auto& [id, value] : settings)
        setPlain (p, id, value);
}

inline void prepareStereo (AuroraBloomProcessor& p, double sr = kSampleRate, int block = kBlock, int inChannels = 2)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (inChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (p.setBusesLayout (layout));
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
}

struct Signal
{
    std::vector<float> l, r;
    explicit Signal (size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
    size_t size() const { return l.size(); }
};

inline Signal noiseBurst (size_t total, size_t burst, float amp, juce::int64 seed = 7)
{
    Signal s (total);
    juce::Random rng (seed);
    for (size_t i = 0; i < burst && i < total; ++i)
    {
        s.l[i] = amp * (rng.nextFloat() * 2.0f - 1.0f);
        s.r[i] = amp * (rng.nextFloat() * 2.0f - 1.0f);
    }
    return s;
}

inline Signal sineBurst (size_t total, size_t burst, double freq, float amp)
{
    Signal s (total);
    for (size_t i = 0; i < burst && i < total; ++i)
    {
        // Short raised-cosine fades avoid broadband clicks that would pollute spectral checks.
        const double fade = std::min (1.0, std::min (static_cast<double> (i), static_cast<double> (burst - i)) / 480.0);
        const auto v = static_cast<float> (amp * fade
                                           * std::sin (2.0 * juce::MathConstants<double>::pi * freq
                                                       * static_cast<double> (i) / kSampleRate));
        s.l[i] = v;
        s.r[i] = v;
    }
    return s;
}

inline Signal impulse (size_t total, float amp = 0.5f)
{
    Signal s (total);
    s.l[0] = amp;
    s.r[0] = amp;
    return s;
}

/** Runs the signal through processBlock in host-sized blocks and returns the output. */
inline Signal process (AuroraBloomProcessor& p, const Signal& in, int block = kBlock)
{
    Signal out (in.size());
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < in.size(); pos += static_cast<size_t> (block))
    {
        const auto n = static_cast<int> (std::min (static_cast<size_t> (block), in.size() - pos));
        buffer.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            buffer.setSample (0, i, in.l[pos + static_cast<size_t> (i)]);
            buffer.setSample (1, i, in.r[pos + static_cast<size_t> (i)]);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            out.l[pos + static_cast<size_t> (i)] = buffer.getSample (0, i);
            out.r[pos + static_cast<size_t> (i)] = buffer.getSample (1, i);
        }
    }
    return out;
}

/** Fresh processor with the given settings applied before prepareToPlay (no smoothing ramps). */
inline Signal render (const std::map<juce::String, float>& settings, const Signal& in)
{
    AuroraBloomProcessor p;
    applySettings (p, settings);
    prepareStereo (p);
    return process (p, in);
}

inline double energy (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, v.size());
    double e = 0.0;
    for (size_t i = from; i < to; ++i)
        e += static_cast<double> (v[i]) * static_cast<double> (v[i]);
    return e;
}

inline double rms (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, v.size());
    return to > from ? std::sqrt (energy (v, from, to) / static_cast<double> (to - from)) : 0.0;
}

inline float peak (const Signal& s, size_t from = 0)
{
    float m = 0.0f;
    for (size_t i = from; i < s.size(); ++i)
        m = std::max ({ m, std::abs (s.l[i]), std::abs (s.r[i]) });
    return m;
}

inline bool finite (const Signal& s)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
            return false;
    return true;
}

inline double toDb (double x)
{
    return 20.0 * std::log10 (std::max (x, 1.0e-12));
}

/** Band power (Hann-windowed DFT bins summed over [lo, hi] Hz) of v[from, from + n). */
inline double bandPower (const std::vector<float>& v, size_t from, size_t n, double lo, double hi)
{
    n = std::min (n, v.size() - from);
    // Direct DFT restricted to the band: cheap for the narrow bands used in the tests and
    // avoids pulling an FFT dependency into the tests.
    const double binHz = kSampleRate / static_cast<double> (n);
    const auto k0 = static_cast<size_t> (std::max (1.0, std::floor (lo / binHz)));
    const auto k1 = static_cast<size_t> (std::ceil (hi / binHz));
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
        w[i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * static_cast<double> (i)
                                     / static_cast<double> (n - 1));

    double total = 0.0;
    for (size_t k = k0; k <= k1; ++k)
    {
        const double omega = 2.0 * juce::MathConstants<double>::pi * static_cast<double> (k) / static_cast<double> (n);
        // Goertzel
        double s1 = 0.0, s2 = 0.0;
        const double coeff = 2.0 * std::cos (omega);
        for (size_t i = 0; i < n; ++i)
        {
            const double s0 = w[i] * static_cast<double> (v[from + i]) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        total += s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }
    return total;
}

/** Fraction of the [from, to) segment's energy above a cutoff, measured with a steep (48 dB/oct,
    four cascaded RBJ biquads) high-pass; the filter runs from sample 0 so it is settled at from. */
inline double highBandRatio (const std::vector<float>& v, size_t from, size_t to, double cutoffHz)
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * cutoffHz / kSampleRate;
    const double alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
    const double cw = std::cos (w0);
    const double a0 = 1.0 + alpha;
    const double b0 = (1.0 + cw) * 0.5 / a0, b1 = -(1.0 + cw) / a0, b2 = b0;
    const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;

    std::array<std::array<double, 4>, 4> z {}; // x1, x2, y1, y2 per stage
    double eHigh = 0.0, eAll = 0.0;
    to = std::min (to, v.size());
    for (size_t i = 0; i < to; ++i)
    {
        double x = v[i];
        const double in = x;
        for (auto& st : z)
        {
            const double y = b0 * x + b1 * st[0] + b2 * st[1] - a1 * st[2] - a2 * st[3];
            st[1] = st[0];
            st[0] = x;
            st[3] = st[2];
            st[2] = y;
            x = y;
        }
        if (i >= from)
        {
            eHigh += x * x;
            eAll += in * in;
        }
    }
    return eAll > 0.0 ? eHigh / eAll : 0.0;
}

} // namespace ab::plugintest
