// Offline renderer for listening tests and spectrogram analysis (not part of the plugin).
//
//   ab_render out.wav [input=impulse|burst|sine:<hz>|chord] [seconds=12] [sr=48000]
//                     [mix=0.5] [pitch=12] [feedback=0.6] [diffusion=0.8] [size=0.6]
//                     [lowcut=20] [highcut=8000] [modrate=0.4] [moddepth=0.15]
//                     [reverb=0..3] [pitchmode=0..4] [color=0..1] [block=512]
//
// Percent-type parameters are given as 0..1. Writes a 32-bit float stereo WAV.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "dsp/RandomLfo.h"
#include "dsp/ShimmerEngine.h"

namespace
{
    void writeU32 (std::ofstream& f, std::uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); }
    void writeU16 (std::ofstream& f, std::uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); }

    bool writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sampleRate)
    {
        std::ofstream f (path, std::ios::binary);
        if (! f)
            return false;
        const auto frames = static_cast<std::uint32_t> (l.size());
        const std::uint32_t dataBytes = frames * 2u * 4u;
        f.write ("RIFF", 4);
        writeU32 (f, 36u + dataBytes);
        f.write ("WAVEfmt ", 8);
        writeU32 (f, 16);
        writeU16 (f, 3); // IEEE float
        writeU16 (f, 2);
        writeU32 (f, static_cast<std::uint32_t> (sampleRate));
        writeU32 (f, static_cast<std::uint32_t> (sampleRate) * 8u);
        writeU16 (f, 8);
        writeU16 (f, 32);
        f.write ("data", 4);
        writeU32 (f, dataBytes);
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            f.write (reinterpret_cast<const char*> (&l[i]), 4);
            f.write (reinterpret_cast<const char*> (&r[i]), 4);
        }
        return static_cast<bool> (f);
    }

    float arg (const std::map<std::string, std::string>& a, const char* key, float fallback)
    {
        const auto it = a.find (key);
        return it == a.end() ? fallback : std::strtof (it->second.c_str(), nullptr);
    }
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: ab_render out.wav [key=value ...]\n");
        return 1;
    }

    std::map<std::string, std::string> a;
    for (int i = 2; i < argc; ++i)
    {
        const std::string s (argv[i]);
        const auto eq = s.find ('=');
        if (eq != std::string::npos)
            a[s.substr (0, eq)] = s.substr (eq + 1);
    }

    const int sampleRate = static_cast<int> (arg (a, "sr", 48000.0f));
    const auto seconds = static_cast<double> (arg (a, "seconds", 12.0f));
    const int block = std::max (1, static_cast<int> (arg (a, "block", 512.0f)));
    const auto sr = static_cast<double> (sampleRate);

    ab::dsp::EngineParameters p;
    p.mix = arg (a, "mix", 0.5f);
    p.pitchSemitones = arg (a, "pitch", 12.0f);
    p.feedback = arg (a, "feedback", 0.6f);
    p.diffusion = arg (a, "diffusion", 0.8f);
    p.size = arg (a, "size", 0.6f);
    p.lowCutHz = arg (a, "lowcut", 20.0f);
    p.highCutHz = arg (a, "highcut", 8000.0f);
    p.modRateHz = arg (a, "modrate", 0.4f);
    p.modDepth = arg (a, "moddepth", 0.15f);
    p.reverbMode = static_cast<ab::dsp::ReverbMode> (static_cast<int> (arg (a, "reverb", 2.0f)));
    p.pitchMode = static_cast<ab::dsp::PitchMode> (static_cast<int> (arg (a, "pitchmode", 0.0f)));
    p.colorMode = static_cast<ab::dsp::ColorMode> (static_cast<int> (arg (a, "color", 0.0f)));

    const auto n = static_cast<std::size_t> (seconds * sr);
    std::vector<float> l (n, 0.0f), r (n, 0.0f);

    const std::string input = a.count ("input") ? a["input"] : "burst";
    const auto twoPi = 2.0 * 3.141592653589793;
    if (input == "impulse")
    {
        l[0] = r[0] = 0.5f;
    }
    else if (input.rfind ("sine:", 0) == 0)
    {
        const double f = std::atof (input.c_str() + 5);
        for (std::size_t i = 0; i < std::min (n, static_cast<std::size_t> (sr * 1.0)); ++i)
            l[i] = r[i] = 0.3f * static_cast<float> (std::sin (twoPi * f * static_cast<double> (i) / sr));
    }
    else if (input == "chord")
    {
        // A short plucked A-minor triad with exponential decay.
        const double freqs[] = { 220.0, 261.63, 329.63 };
        for (std::size_t i = 0; i < std::min (n, static_cast<std::size_t> (sr * 2.0)); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double v = 0.0;
            for (const auto f : freqs)
                v += std::sin (twoPi * f * t) + 0.3 * std::sin (twoPi * 2.0 * f * t);
            l[i] = r[i] = static_cast<float> (0.12 * v * std::exp (-2.5 * t));
        }
    }
    else // burst: 50 ms of noise
    {
        ab::dsp::FastRandom rng (7);
        for (std::size_t i = 0; i < std::min (n, static_cast<std::size_t> (sr * 0.05)); ++i)
        {
            l[i] = 0.5f * rng.nextBipolar();
            r[i] = 0.5f * rng.nextBipolar();
        }
    }

    ab::dsp::ShimmerEngine engine;
    engine.setParameters (p);
    engine.prepare (sr);

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t pos = 0; pos < n; pos += static_cast<std::size_t> (block))
    {
        const auto len = std::min (static_cast<std::size_t> (block), n - pos);
        engine.process (l.data() + pos, r.data() + pos, static_cast<int> (len));
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    float peak = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
        peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });

    if (! writeWav (argv[1], l, r, sampleRate))
    {
        std::fprintf (stderr, "could not write %s\n", argv[1]);
        return 1;
    }

    std::printf ("%s: %.1f s rendered in %.3f s (%.2f %% of one core), peak %.2f dBFS\n", argv[1], seconds, elapsed,
                 100.0 * elapsed / seconds, 20.0 * std::log10 (static_cast<double> (std::max (peak, 1.0e-9f))));
    return 0;
}
