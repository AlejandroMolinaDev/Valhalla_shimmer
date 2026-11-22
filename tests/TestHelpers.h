#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numeric>
#include <vector>

#include "dsp/RandomLfo.h"
#include "dsp/ShimmerEngine.h"

namespace ab::test
{

struct Stereo
{
    std::vector<float> l, r;

    explicit Stereo (std::size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
    std::size_t size() const { return l.size(); }
};

inline Stereo whiteNoise (std::size_t n, float amplitude, std::uint32_t seed = 1234)
{
    Stereo s (n);
    dsp::FastRandom rng (seed);
    for (std::size_t i = 0; i < n; ++i)
    {
        s.l[i] = amplitude * rng.nextBipolar();
        s.r[i] = amplitude * rng.nextBipolar();
    }
    return s;
}

inline Stereo sine (std::size_t n, double freq, double sampleRate, float amplitude)
{
    Stereo s (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto v = amplitude * static_cast<float> (std::sin (2.0 * 3.141592653589793 * freq
                                                                  * static_cast<double> (i) / sampleRate));
        s.l[i] = v;
        s.r[i] = v;
    }
    return s;
}

inline Stereo impulse (std::size_t n, float amplitude = 1.0f)
{
    Stereo s (n);
    s.l[0] = amplitude;
    s.r[0] = amplitude;
    return s;
}

/** Processes the whole signal in place, split into blocks of the given size(s) (cycled). */
inline void render (dsp::ShimmerEngine& engine, Stereo& s, const std::vector<int>& blockSizes = { 512 })
{
    std::size_t pos = 0, k = 0;
    while (pos < s.size())
    {
        const auto want = static_cast<std::size_t> (blockSizes[k++ % blockSizes.size()]);
        const auto n = std::min (want, s.size() - pos);
        engine.process (s.l.data() + pos, s.r.data() + pos, static_cast<int> (n));
        pos += n;
    }
}

inline dsp::ShimmerEngine makeEngine (const dsp::EngineParameters& p, double sampleRate = 48000.0)
{
    dsp::ShimmerEngine e;
    e.setParameters (p);
    e.prepare (sampleRate);
    return e;
}

inline float peakAbs (const std::vector<float>& v, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    to = std::min (to, v.size());
    float m = 0.0f;
    for (std::size_t i = from; i < to; ++i)
        m = std::max (m, std::abs (v[i]));
    return m;
}

inline bool allFinite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
}

inline double energy (const std::vector<float>& v, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    to = std::min (to, v.size());
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i)
        e += static_cast<double> (v[i]) * static_cast<double> (v[i]);
    return e;
}

inline std::size_t argMaxAbs (const std::vector<float>& v)
{
    std::size_t idx = 0;
    float m = -1.0f;
    for (std::size_t i = 0; i < v.size(); ++i)
        if (std::abs (v[i]) > m)
        {
            m = std::abs (v[i]);
            idx = i;
        }
    return idx;
}

inline float toDb (double x) { return static_cast<float> (20.0 * std::log10 (std::max (x, 1.0e-12))); }

// --- Spectrum ---------------------------------------------------------------------------------

inline void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.141592653589793 / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Averaged (Welch, Hann window) power spectrum of v[from, from + frames*fftSize/2...]. */
inline std::vector<double> powerSpectrum (const std::vector<float>& v, std::size_t from, std::size_t length,
                                          std::size_t fftSize = 8192)
{
    std::vector<double> acc (fftSize / 2, 0.0);
    std::vector<double> window (fftSize);
    for (std::size_t i = 0; i < fftSize; ++i)
        window[i] = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * static_cast<double> (i)
                                          / static_cast<double> (fftSize));

    std::size_t frames = 0;
    for (std::size_t start = from; start + fftSize <= std::min (v.size(), from + length); start += fftSize / 2)
    {
        std::vector<std::complex<double>> buf (fftSize);
        for (std::size_t i = 0; i < fftSize; ++i)
            buf[i] = static_cast<double> (v[start + i]) * window[i];
        fft (buf);
        for (std::size_t k = 0; k < fftSize / 2; ++k)
            acc[k] += std::norm (buf[k]);
        ++frames;
    }
    if (frames > 0)
        for (auto& x : acc)
            x /= static_cast<double> (frames);
    return acc;
}

inline double bandEnergy (const std::vector<double>& spectrum, double sampleRate, std::size_t fftSize,
                          double loHz, double hiHz)
{
    double e = 0.0;
    const double binHz = sampleRate / static_cast<double> (fftSize);
    for (std::size_t k = 0; k < spectrum.size(); ++k)
    {
        const double f = static_cast<double> (k) * binHz;
        if (f >= loHz && f < hiHz)
            e += spectrum[k];
    }
    return e;
}

// --- Decay ------------------------------------------------------------------------------------

/** RT60 estimated from the Schroeder energy decay curve (T30 fit between -5 and -35 dB). */
inline double rt60 (const std::vector<float>& ir, double sampleRate)
{
    std::vector<double> edc (ir.size());
    double acc = 0.0;
    for (std::size_t i = ir.size(); i-- > 0;)
    {
        acc += static_cast<double> (ir[i]) * static_cast<double> (ir[i]);
        edc[i] = acc;
    }
    const double total = edc.front();
    std::size_t i5 = 0, i35 = 0;
    for (std::size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (edc[i] / total, 1.0e-30));
        if (i5 == 0 && db <= -5.0)
            i5 = i;
        if (db <= -35.0)
        {
            i35 = i;
            break;
        }
    }
    if (i35 <= i5)
        return 0.0;
    return 2.0 * static_cast<double> (i35 - i5) / sampleRate; // T30 * 2
}

} // namespace ab::test
