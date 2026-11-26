#include "ShimmerEngine.h"
#include "Denormals.h"

#include <algorithm>
#include <cmath>

namespace ab::dsp
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr double kMixRampMs = 30.0;
    constexpr double kParamRampMs = 50.0;
    // Diffusion and Mod Depth glide slowly: the allpass state stored at a high coefficient is
    // released gradually instead of in one burst, and a depth change is a delay-time change.
    constexpr double kDiffusionRampMs = 250.0;
    constexpr double kModDepthRampMs = 250.0;
    constexpr double kSizeTimeConstantMs = 250.0;
    constexpr double kBypassRampMs = 50.0;
    constexpr double kModeDuckMs = 25.0;
    constexpr double kPitchModeRampMs = 20.0;
    constexpr float kMinus3dB = 0.70710678f;
    constexpr double kDcCutoffHz = 5.0;
    constexpr float kSoftClipKnee = 0.75f;
} // namespace

// =============================================================================================
void DcBlocker::prepare (double sampleRate) noexcept
{
    r = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * kDcCutoffHz / sampleRate));
    reset();
}

void OverlapMakeup::prepare (double sampleRate) noexcept
{
    r = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * kCornerHz / sampleRate));
    reset();
}

// =============================================================================================
float ShimmerEngine::feedbackToGain (float feedback01) noexcept
{
    // 0..90 % of the travel covers 0..0.95 (slightly concave so low values are usable);
    // the last 10 % is reserved for near-infinite tails (0.95..0.995).
    const float p = std::clamp (feedback01, 0.0f, 1.0f);
    if (p <= 0.9f)
        return 0.95f * std::pow (p / 0.9f, 0.8f);
    return 0.95f + 0.045f * (p - 0.9f) / 0.1f;
}

float ShimmerEngine::sizeToFactor (float size01) noexcept
{
    // Exponential: 0 % -> 0.1x, 100 % -> 1.0x of the mode's base lengths.
    return 0.1f * std::pow (10.0f, std::clamp (size01, 0.0f, 1.0f));
}

void ShimmerEngine::mixGains (float mix01, float& dry, float& wet) noexcept
{
    const float theta = std::clamp (mix01, 0.0f, 1.0f) * 0.5f * kPi;
    dry = std::cos (theta);
    wet = std::sin (theta);
}

float ShimmerEngine::softClip (float x) noexcept
{
    // Transparent below the knee (-2.5 dBFS); above it a tanh shoulder approaches 1.0 with a
    // continuous first derivative. Only ever engages near full scale.
    const float a = std::abs (x);
    if (a <= kSoftClipKnee)
        return x;
    const float range = 1.0f - kSoftClipKnee;
    const float y = kSoftClipKnee + range * std::tanh ((a - kSoftClipKnee) / range);
    return std::copysign (y, x);
}

float ShimmerEngine::diffusionToCoefficient (float diffusion01) noexcept
{
    return std::clamp (diffusion01, 0.0f, DiffuserNetwork::kMaxDiffusion);
}

double ShimmerEngine::estimateTailSeconds (const EngineParameters& p) noexcept
{
    if (p.feedback >= 0.99f)
        return 120.0; // practically infinite

    const double size = static_cast<double> (sizeToFactor (p.size));
    const double loopSeconds = static_cast<double> (totalLength (p.reverbMode, true)) * size
                             / static_cast<double> (kReferenceSampleRate);
    const double longestStage = static_cast<double> (maxStageLength()) * size
                              / static_cast<double> (kReferenceSampleRate);

    // Time for 90 dB of decay: through the loop gain and through the allpasses' own ringing.
    const double g = static_cast<double> (feedbackToGain (p.feedback));
    const double fbTail = g > 1.0e-3 ? loopSeconds * 4.5 / -std::log10 (g) : 0.0;
    const double d = static_cast<double> (diffusionToCoefficient (p.diffusion));
    const double apTail = d > 1.0e-2 ? longestStage * 4.5 / -std::log10 (d) : 0.0;

    return std::clamp (loopSeconds + fbTail + apTail, 1.0, 60.0);
}

// =============================================================================================
void ShimmerEngine::prepare (double newSampleRate)
{
    const bool needsAllocation = ! prepared || newSampleRate != sampleRate;
    sampleRate = newSampleRate;

    if (needsAllocation)
    {
        diffuser.prepare (sampleRate);
        shifterL.prepare (sampleRate, 0x1234ABCDu); // different seeds: decorrelated L/R grains
        shifterR.prepare (sampleRate, 0x9E3779B9u);
    }

    dryGain.prepare (sampleRate, kMixRampMs);
    wetGain.prepare (sampleRate, kMixRampMs);
    feedbackGain.prepare (sampleRate, kParamRampMs);
    diffusionG.prepare (sampleRate, kDiffusionRampMs);
    modDepthSamples.prepare (sampleRate, kModDepthRampMs);
    bypassAmount.prepare (sampleRate, kBypassRampMs);
    pitchThru.prepare (sampleRate, kPitchModeRampMs);
    pitchVoiceA.prepare (sampleRate, kPitchModeRampMs);
    pitchVoiceB.prepare (sampleRate, kPitchModeRampMs);
    sizeFactor.prepare (sampleRate, kSizeTimeConstantMs);
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    makeupL.prepare (sampleRate);
    makeupR.prepare (sampleRate);
    filters.prepare (sampleRate);
    duckStep = static_cast<float> (1.0 / (kModeDuckMs * 0.001 * sampleRate));

    prepared = true;
    reset();
}

void ShimmerEngine::reset() noexcept
{
    if (! prepared)
        return;

    applyMode (params.reverbMode); // also resets the diffuser and the loop state
    duckGain = 1.0f;

    // Jump every smoother to its target: after a reset the engine starts at the current
    // settings instead of gliding from stale values.
    float d = 0.0f, w = 0.0f;
    mixGains (params.mix, d, w);
    dryGain.snap (d);
    wetGain.snap (w);
    feedbackGain.snap (feedbackToGain (params.feedback));
    diffusionG.snap (diffusionToCoefficient (params.diffusion));
    sizeFactor.snap (sizeToFactor (params.size));
    bypassAmount.snap (bypassAmount.getTarget());
    updateModulation();
    modDepthSamples.snap (modDepthSamples.getTarget());
    updatePitchMode();
    updateFilters();
    filters.snap();
    pitchThru.snap (pitchThru.getTarget());
    pitchVoiceA.snap (pitchVoiceA.getTarget());
    pitchVoiceB.snap (pitchVoiceB.getTarget());
}

void ShimmerEngine::updateFilters() noexcept
{
    filters.setLowCut (std::clamp (params.lowCutHz, 10.0f, 2000.0f));
    filters.setHighCut (std::clamp (params.highCutHz, 500.0f, 20000.0f));
    filters.setDark (params.colorMode == ColorMode::Dark);
}

void ShimmerEngine::updatePitchMode() noexcept
{
    float thru = 0.0f, a = 0.0f, b = 0.0f;
    switch (params.pitchMode)
    {
        case PitchMode::Single:
        case PitchMode::SingleReverse: a = 1.0f; break;
        case PitchMode::Dual:
        case PitchMode::DualReverse: a = b = kMinus3dB; break;
        case PitchMode::Bypass: thru = 1.0f; break;
    }
    pitchThru.setTarget (thru);
    pitchVoiceA.setTarget (a);
    pitchVoiceB.setTarget (b);

    const bool rev = params.pitchMode == PitchMode::SingleReverse || params.pitchMode == PitchMode::DualReverse;
    const float st = std::clamp (params.pitchSemitones, -12.0f, 12.0f);
    shifterL.setReverse (rev);
    shifterR.setReverse (rev);
    shifterL.setSemitones (st);
    shifterR.setSemitones (st);
}

void ShimmerEngine::updateModulation() noexcept
{
    const auto srScale = static_cast<float> (sampleRate / static_cast<double> (kReferenceSampleRate));
    const float depth = std::clamp (params.modDepth, 0.0f, 1.0f) * DiffuserNetwork::kMaxModDepthSamples48k * srScale;
    diffuser.setModulationRate (std::clamp (params.modRateHz, 0.01f, 10.0f));
    modDepthSamples.setTarget (depth);
}

void ShimmerEngine::clearLoopState() noexcept
{
    fbL = fbR = 0.0f;
    makeupL.reset();
    makeupR.reset();
    shifterL.reset();
    shifterR.reset();
    filters.reset();
    dcL.reset();
    dcR.reset();
}

void ShimmerEngine::applyMode (ReverbMode newMode) noexcept
{
    activeMode = newMode;
    diffuser.reset();
    diffuser.setMode (newMode);
    clearLoopState();

    const auto& spec = getModeSpec (newMode);
    monoInput = spec.monoInput;
    filters.setDarkCutoff (spec.darkCutoffHz);
    crossCos = std::cos (spec.crossMixAngle);
    crossSin = std::sin (spec.crossMixAngle);
}

void ShimmerEngine::setParameters (const EngineParameters& newParams) noexcept
{
    params = newParams;

    float d = 0.0f, w = 0.0f;
    mixGains (params.mix, d, w);
    dryGain.setTarget (d);
    wetGain.setTarget (w);
    feedbackGain.setTarget (feedbackToGain (params.feedback));
    diffusionG.setTarget (diffusionToCoefficient (params.diffusion));
    sizeFactor.setTarget (sizeToFactor (params.size));
    updateModulation();
    updatePitchMode();
    updateFilters();
}

void ShimmerEngine::setBypassed (bool shouldBeBypassed) noexcept
{
    bypassAmount.setTarget (shouldBeBypassed ? 1.0f : 0.0f);
}

// =============================================================================================
void ShimmerEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (! prepared || numSamples <= 0 || left == nullptr || right == nullptr)
        return;

    // Fully bypassed by the host (after the 50 ms ramp): the buffer already holds the dry
    // signal, so skip all DSP (no CPU while bypassed in FL Studio) and flush the tank once so
    // a stale tail never resurfaces when the plugin is re-enabled.
    if (bypassAmount.getTarget() >= 1.0f && ! bypassAmount.isSmoothing())
    {
        if (! flushedWhileBypassed)
        {
            applyMode (params.reverbMode);
            duckGain = 1.0f;
            flushedWhileBypassed = true;
        }
        wetRmsL = wetRmsR = 0.0f;
        inputRms = 0.0f;
        return;
    }
    flushedWhileBypassed = false;

    const ScopedFlushDenormals noDenormals;
    float wetSumL = 0.0f, wetSumR = 0.0f, inSum = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float dryL = left[n];
        const float dryR = right[n];

        // --- Reverb mode switching (duck -> reconfigure -> fade in) ---------------------------
        if (params.reverbMode != activeMode)
        {
            duckGain -= duckStep;
            if (duckGain <= 0.0f)
            {
                duckGain = 0.0f;
                applyMode (params.reverbMode);
            }
        }
        else if (duckGain < 1.0f)
        {
            duckGain = std::min (1.0f, duckGain + duckStep);
        }

        const float bypass = bypassAmount.next();
        const float active = 1.0f - bypass;

        float inL = dryL, inR = dryR;
        if (monoInput)
            inL = inR = 0.5f * (dryL + dryR);
        inL *= active;
        inR *= active;

        // --- Feedback injection with energy-preserving L/R rotation ---------------------------
        const float g = feedbackGain.next() * active;
        const float rotL = crossCos * fbL - crossSin * fbR;
        const float rotR = crossSin * fbL + crossCos * fbR;
        const float uL = inL + g * rotL;
        const float uR = inR + g * rotR;

        // --- Diffuser network ------------------------------------------------------------------
        float netL = 0.0f, netR = 0.0f;
        diffuser.process (uL, uR, netL, netR, sizeFactor.next(), diffusionG.next(), modDepthSamples.next());

        const float wetL = softClip (netL);
        const float wetR = softClip (netR);

        // --- Pitch shifter (inside the loop) -----------------------------------------------------
        const float thru = pitchThru.next();
        const float gA = pitchVoiceA.next();
        const float gB = pitchVoiceB.next();
        const bool useA = gA > 0.0f || pitchVoiceA.getTarget() > 0.0f;
        const bool useB = gB > 0.0f || pitchVoiceB.getTarget() > 0.0f;
        float aL = 0.0f, bL = 0.0f, aR = 0.0f, bR = 0.0f;
        shifterL.process (wetL, useA, useB, aL, bL);
        shifterR.process (wetR, useA, useB, aR, bR);
        float pL = thru * wetL + makeupL.process (gA * aL + gB * bL);
        float pR = thru * wetR + makeupR.process (gA * aR + gB * bR);

        // --- Loop filters: Low Cut -> High Cut -> Color (cumulative on every pass) --------------
        filters.process (pL, pR);

        // --- Loop return path -------------------------------------------------------------------
        fbL = dcL.process (pL);
        fbR = dcR.process (pR);

        // --- Output mix ----------------------------------------------------------------------
        const float dg = dryGain.next() * active + bypass;
        const float wg = wetGain.next() * active * duckGain;
        left[n] = dg * dryL + wg * wetL;
        right[n] = dg * dryR + wg * wetR;

        wetSumL += wetL * wetL;
        wetSumR += wetR * wetR;
        inSum += 0.5f * (dryL * dryL + dryR * dryR);
    }

    // --- Safety: never let NaN/Inf escape or persist in the loop ----------------------------
    if (! std::isfinite (wetSumL + wetSumR + fbL + fbR))
    {
        ++numSafetyResets;
        std::fill (left, left + numSamples, 0.0f);
        std::fill (right, right + numSamples, 0.0f);
        reset();
        wetSumL = wetSumR = 0.0f;
    }

    const float inv = 1.0f / static_cast<float> (numSamples);
    wetRmsL = std::sqrt (wetSumL * inv);
    wetRmsR = std::sqrt (wetSumR * inv);
    inputRms = std::sqrt (inSum * inv);
}

} // namespace ab::dsp
