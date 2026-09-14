#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace sf
{
/**
    A linear-ramp smoother whose target is set once per block from a parameter.

    Every automatable value that could otherwise produce a step discontinuity goes
    through one of these. `setTargetFrom` is cheap enough to call unconditionally
    at the top of every block.
*/
class ParamSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds = 0.02) noexcept
    {
        value.reset (sampleRate, rampSeconds);
    }

    void snapTo (float v) noexcept { value.setCurrentAndTargetValue (v); }

    void setTargetFrom (std::atomic<float>* p, float fallback = 0.0f) noexcept
    {
        const auto v = p != nullptr ? p->load() : fallback;
        value.setTargetValue (std::isfinite (v) ? v : fallback);
    }

    void setTarget (float v) noexcept
    {
        value.setTargetValue (std::isfinite (v) ? v : value.getTargetValue());
    }

    float next() noexcept              { return value.getNextValue(); }
    float getTarget() const noexcept   { return value.getTargetValue(); }
    float getCurrent() const noexcept  { return value.getCurrentValue(); }
    bool  isSmoothing() const noexcept { return value.isSmoothing(); }

    /** Advances by `n` samples without producing output. */
    void skip (int n) noexcept { value.skip (n); }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> value;
};

/**
    Equal-power-ish crossfade used for click-free bypass.

    Bypass is a discrete parameter, so switching it flips `target` between 0 and 1
    and the ramp does the rest. `isInactive()` lets the caller skip all processing
    once the fade has fully settled on "bypassed".
*/
class CrossfadeRamp
{
public:
    void prepare (double sampleRate, double rampSeconds = 0.015) noexcept
    {
        ramp.reset (sampleRate, rampSeconds);
        ramp.setCurrentAndTargetValue (ramp.getTargetValue());
    }

    void setActive (bool shouldBeActive) noexcept
    {
        ramp.setTargetValue (shouldBeActive ? 1.0f : 0.0f);
    }

    void snapActive (bool shouldBeActive) noexcept
    {
        ramp.setCurrentAndTargetValue (shouldBeActive ? 1.0f : 0.0f);
    }

    float next() noexcept { return ramp.getNextValue(); }

    /** True once the fade has fully reached "inactive" — safe to bypass entirely. */
    bool isInactive() const noexcept
    {
        return ! ramp.isSmoothing() && ramp.getTargetValue() <= 0.0f;
    }

    bool isFullyActive() const noexcept
    {
        return ! ramp.isSmoothing() && ramp.getTargetValue() >= 1.0f;
    }

    void skip (int n) noexcept { ramp.skip (n); }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ramp { 1.0f };
};

/** Maps a 0..100 percent parameter to a 0..1 multiplier. */
inline float percentToUnit (float percent) noexcept
{
    return juce::jlimit (0.0f, 1.0f, percent * 0.01f);
}

/** Linear interpolation that is safe for any finite inputs. */
inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * juce::jlimit (0.0f, 1.0f, t);
}
} // namespace sf
