#pragma once

#include <cmath>

namespace ab::dsp
{

/** Exact float comparison without tripping -Wfloat-equal (intentional here: smoothers must
    behave identically whatever the block size, so no tolerance is wanted). */
constexpr bool exactlyEqual (float a, float b) noexcept { return ! (a < b) && ! (b < a); }

/** Linear ramp towards a target over a fixed time (like juce::SmoothedValue<Linear>).
    Re-targeting to the value it is already heading for does not restart the ramp, so calling
    setTarget() with the same value at every block start is free and block-size independent. */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampMs) noexcept
    {
        const auto steps = static_cast<int> (std::lround (sampleRate * rampMs * 0.001));
        rampSteps = steps > 0 ? steps : 1;
        snap (target);
    }

    void snap (float value) noexcept
    {
        current = target = value;
        increment = 0.0f;
        remaining = 0;
    }

    void setTarget (float newTarget) noexcept
    {
        if (exactlyEqual (newTarget, target))
            return;

        target = newTarget;
        remaining = rampSteps;
        increment = (target - current) / static_cast<float> (rampSteps);
    }

    float next() noexcept
    {
        if (remaining > 0)
        {
            current += increment;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return remaining > 0; }

private:
    float current = 0.0f, target = 0.0f, increment = 0.0f;
    int rampSteps = 1, remaining = 0;
};

/** One-pole exponential smoother. Used for Size, whose slow glide produces the classic
    "pitch sweep" of changing delay lengths without clicks. */
class OnePoleSmoother
{
public:
    void prepare (double sampleRate, double timeConstantMs) noexcept
    {
        const auto samples = sampleRate * timeConstantMs * 0.001;
        coeff = samples > 1.0 ? static_cast<float> (std::exp (-1.0 / samples)) : 0.0f;
    }

    void snap (float value) noexcept { current = target = value; }
    void setTarget (float newTarget) noexcept { target = newTarget; }

    float next() noexcept
    {
        current = target + coeff * (current - target);
        if (std::abs (current - target) < 1.0e-7f)
            current = target;
        return current;
    }

    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return ! exactlyEqual (current, target); }

private:
    float current = 0.0f, target = 0.0f, coeff = 0.0f;
};

} // namespace ab::dsp
