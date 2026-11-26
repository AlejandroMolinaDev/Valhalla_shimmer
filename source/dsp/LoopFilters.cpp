#include "LoopFilters.h"

#include <algorithm>
#include <cmath>

namespace ab::dsp
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kButterworthQ = 0.70710678118654752;
    constexpr double kCutoffRampMs = 50.0;
    constexpr double kColorRampMs = 20.0;

    /** Section Qs of an 8th-order Butterworth low-pass: Q_k = 1 / (2 cos((2k - 1) pi / 16)). */
    constexpr std::array<double, 4> kDarkQs { 0.50979557910415917, 0.60134488693504528,
                                              0.89997622313641570, 2.56291544774150617 };

    double clampCutoff (double hz, double sampleRate) noexcept
    {
        return std::clamp (hz, 5.0, 0.45 * sampleRate);
    }
} // namespace

void SvfTpt::setCoefficients (double cutoffHz, double sampleRate, double q) noexcept
{
    const double g = std::tan (kPi * clampCutoff (cutoffHz, sampleRate) / sampleRate);
    const double kk = 1.0 / q;
    const double aa1 = 1.0 / (1.0 + g * (g + kk));
    k = static_cast<float> (kk);
    a1 = static_cast<float> (aa1);
    a2 = static_cast<float> (g * aa1);
    a3 = static_cast<float> (g * g * aa1);
}

// =============================================================================================
void LoopFilters::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    lowCutLog.prepare (sampleRate, kCutoffRampMs);
    highCutLog.prepare (sampleRate, kCutoffRampMs);
    darkMix.prepare (sampleRate, kColorRampMs);
    setDarkCutoff (darkCutoffHz);
    snap();
    reset();
}

void LoopFilters::reset() noexcept
{
    lowCutL.reset();
    lowCutR.reset();
    highCutL.reset();
    highCutR.reset();
    for (auto& f : darkL) f.reset();
    for (auto& f : darkR) f.reset();
}

void LoopFilters::snap() noexcept
{
    lowCutLog.snap (lowCutLog.getTarget());
    highCutLog.snap (highCutLog.getTarget());
    darkMix.snap (darkMix.getTarget());
    updateLowCut (lowCutLog.getCurrent());
    updateHighCut (highCutLog.getCurrent());
}

void LoopFilters::setDarkCutoff (float hz) noexcept
{
    darkCutoffHz = hz;
    for (std::size_t i = 0; i < kDarkSections; ++i)
    {
        darkL[i].setCoefficients (static_cast<double> (hz), sampleRate, kDarkQs[i]);
        darkR[i].setCoefficients (static_cast<double> (hz), sampleRate, kDarkQs[i]);
    }
}

void LoopFilters::updateLowCut (float logHz) noexcept
{
    const auto hz = std::exp (static_cast<double> (logHz));
    lowCutL.setCoefficients (hz, sampleRate, kButterworthQ);
    lowCutR.setCoefficients (hz, sampleRate, kButterworthQ);
}

void LoopFilters::updateHighCut (float logHz) noexcept
{
    const auto hz = std::exp (static_cast<double> (logHz));
    highCutL.setCoefficients (hz, sampleRate, kButterworthQ);
    highCutR.setCoefficients (hz, sampleRate, kButterworthQ);
}

} // namespace ab::dsp
