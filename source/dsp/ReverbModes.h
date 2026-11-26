#pragma once

#include <array>

namespace ab::dsp
{

enum class ReverbMode : int
{
    Mono = 0,      // mono in (L+R summed) -> stereo out, largest, slowest fade-in
    BigStereo,     // cathedral-like, very dense, slow fades
    MediumStereo,  // classic hall
    SmallStereo    // small and noticeably less dense: more comb colouration (springs, chorus)
};

inline constexpr int kNumReverbModes = 4;
inline constexpr int kMaxStages = 12;

/** Per-mode configuration. Stage lengths are in samples at 48 kHz for Size = 100 %. They are
    primes (mutually prime by construction) on a roughly geometric progression so that echo
    densities build up quickly without coinciding arrival times. The right channel uses a
    different, slightly longer, interleaved set for decorrelation; at Diffusion = 0 the two
    totals differ by ~4 %, producing the "two slightly different delays" stereo image. */
struct ReverbModeSpec
{
    int numStages;
    std::array<float, kMaxStages> left;
    std::array<float, kMaxStages> right;
    bool monoInput;
    float darkCutoffHz;    // Color = Dark: steep in-loop low-pass corner for this mode
    float crossMixAngle;   // feedback rotation angle (radians) between L and R
};

inline constexpr std::array<ReverbModeSpec, kNumReverbModes> kReverbModes {{
    // Mono: 12 stages, ~1.00 s / 1.04 s total
    { 12,
      { 1609, 1861, 2143, 2477, 2861, 3301, 3821, 4397, 5081, 5867, 6779, 7817 },
      { 1733, 1889, 2311, 2521, 3083, 3359, 4111, 4463, 5479, 5953, 7307, 7937 },
      true, 8800.0f, 0.70f },

    // Big Stereo: 10 stages, ~0.85 s / 0.89 s total
    { 10,
      { 1669, 1993, 2371, 2803, 3329, 3947, 4691, 5563, 6599, 7829, 0, 0 },
      { 1811, 2027, 2549, 2851, 3593, 4013, 5059, 5651, 7121, 7951, 0, 0 },
      false, 9600.0f, 0.62f },

    // Medium Stereo: 8 stages, ~0.55 s / 0.57 s total
    { 8,
      { 1423, 1753, 2161, 2659, 3271, 4027, 4973, 6121, 0, 0, 0, 0 },
      { 1531, 1777, 2333, 2699, 3533, 4099, 5351, 6221, 0, 0, 0, 0 },
      false, 10200.0f, 0.62f },

    // Small Stereo: 5 stages, ~0.18 s / 0.19 s total
    { 5,
      { 929, 1217, 1607, 2111, 2777, 0, 0, 0, 0, 0, 0, 0 },
      { 997, 1237, 1733, 2143, 2999, 0, 0, 0, 0, 0, 0, 0 },
      false, 11000.0f, 0.55f },
}};

inline constexpr float kReferenceSampleRate = 48000.0f;

/** Longest base stage length across all modes (samples @ 48 kHz). */
inline constexpr float maxStageLength() noexcept
{
    float m = 0.0f;
    for (const auto& spec : kReverbModes)
        for (int i = 0; i < spec.numStages; ++i)
        {
            const auto idx = static_cast<std::size_t> (i);
            m = spec.left[idx] > m ? spec.left[idx] : m;
            m = spec.right[idx] > m ? spec.right[idx] : m;
        }
    return m;
}

/** Sum of the stage lengths of a mode (samples @ 48 kHz, Size = 100 %). */
inline constexpr float totalLength (ReverbMode mode, bool rightChannel) noexcept
{
    const auto& spec = kReverbModes[static_cast<std::size_t> (mode)];
    float sum = 0.0f;
    for (int i = 0; i < spec.numStages; ++i)
        sum += rightChannel ? spec.right[static_cast<std::size_t> (i)]
                            : spec.left[static_cast<std::size_t> (i)];
    return sum;
}

inline const ReverbModeSpec& getModeSpec (ReverbMode mode) noexcept
{
    return kReverbModes[static_cast<std::size_t> (mode)];
}

} // namespace ab::dsp
