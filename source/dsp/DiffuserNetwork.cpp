#include "DiffuserNetwork.h"

#include <cmath>

namespace ab::dsp
{

void DiffuserNetwork::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    const double srScale = sampleRate / static_cast<double> (kReferenceSampleRate);
    const double maxLen = static_cast<double> (maxStageLength()) * srScale
                        + static_cast<double> (kMaxModDepthSamples48k) * srScale * 1.5 + 8.0;
    const auto maxSamples = static_cast<std::size_t> (std::ceil (maxLen));

    for (std::size_t i = 0; i < kMaxStages; ++i)
    {
        stagesL[i].allocate (maxSamples);
        stagesR[i].allocate (maxSamples);
    }

    reset();
    setMode (mode);
}

void DiffuserNetwork::reset() noexcept
{
    // Fixed seeds: identical output after every reset (reproducible renders and tests).
    FastRandom seeder (0xA5B1C3D7u);
    for (std::size_t i = 0; i < kMaxStages; ++i)
    {
        stagesL[i].clear();
        stagesR[i].clear();
        // Individual LFO rates spread over 0.8x .. 1.25x of Mod Rate.
        const float fL = 0.8f + 0.45f * seeder.nextFloat();
        const float fR = 0.8f + 0.45f * seeder.nextFloat();
        lfoL[i].prepare (sampleRate, seeder.nextU32(), fL);
        lfoR[i].prepare (sampleRate, seeder.nextU32(), fR);
    }
}

void DiffuserNetwork::setMode (ReverbMode newMode) noexcept
{
    mode = newMode;
    const auto& spec = getModeSpec (mode);
    numStages = spec.numStages;
    const auto srScale = static_cast<float> (sampleRate / static_cast<double> (kReferenceSampleRate));

    for (std::size_t i = 0; i < kMaxStages; ++i)
    {
        baseL[i] = spec.left[i] * srScale;
        baseR[i] = spec.right[i] * srScale;
    }
}

void DiffuserNetwork::setModulation (float rateHz, float depthSamples) noexcept
{
    modDepth = depthSamples;
    for (std::size_t i = 0; i < kMaxStages; ++i)
    {
        lfoL[i].setRate (rateHz);
        lfoR[i].setRate (rateHz);
    }
}

float DiffuserNetwork::getBaseLength (int stage, bool right) const noexcept
{
    const auto i = static_cast<std::size_t> (stage);
    return right ? baseR[i] : baseL[i];
}

} // namespace ab::dsp
