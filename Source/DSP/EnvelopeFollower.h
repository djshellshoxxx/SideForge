#pragma once
#include "../Parameters/ParamIDs.h"
#include <cmath>

namespace sf
{
/** Flushes denormals to zero without relying on the FTZ CPU flag being set. */
inline float flushDenormal (float v) noexcept
{
    return std::abs (v) < 1.0e-20f ? 0.0f : v;
}

inline float sanitise (float v) noexcept
{
    return std::isfinite (v) ? v : 0.0f;
}

/** One-pole coefficient for a time constant in milliseconds. */
inline float timeConstantCoeff (float ms, double sampleRate) noexcept
{
    if (ms <= 0.0f || sampleRate <= 0.0)
        return 0.0f;

    return (float) std::exp (-1.0 / ((double) ms * 0.001 * sampleRate));
}

/**
    Magnitude detector + attack/hold/release ballistics.

    `process()` takes a raw (signed) sample and returns a linear magnitude
    envelope. Everything is preallocated; `process()` is allocation and branch-cheap.
*/
class EnvelopeFollower
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        rmsCoeff   = timeConstantCoeff (10.0f, sampleRate);
        crestFastCoeff = timeConstantCoeff (2.0f, sampleRate);
        crestSlowCoeff = timeConstantCoeff (250.0f, sampleRate);
        updateCoeffs();
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        rmsState = 0.0f;
        crestFast = 0.0f;
        crestSlow = 0.0f;
        holdCounter = 0;
    }

    void setTimes (float attackMs, float holdMs, float releaseMs) noexcept
    {
        if (attackMs != attackTime || holdMs != holdTime || releaseMs != releaseTime)
        {
            attackTime  = attackMs;
            holdTime    = holdMs;
            releaseTime = releaseMs;
            updateCoeffs();
        }
    }

    void setDetector (Detector d) noexcept { detector = d; }
    void setAutoRelease (bool shouldAdapt) noexcept { autoRelease = shouldAdapt; }

    /** Current envelope without advancing. */
    float getEnvelope() const noexcept { return env; }

    float process (float x) noexcept
    {
        x = sanitise (x);
        const auto sq = x * x;

        rmsState  = flushDenormal (sq + rmsCoeff * (rmsState - sq));
        crestFast = flushDenormal (sq + crestFastCoeff * (crestFast - sq));
        crestSlow = flushDenormal (sq + crestSlowCoeff * (crestSlow - sq));

        const auto peak = std::abs (x);
        const auto rms  = std::sqrt (rmsState);

        float mag = peak;

        switch (detector)
        {
            case Detector::Rms:    mag = rms; break;
            case Detector::Hybrid: mag = 0.5f * peak + 0.5f * rms; break;
            case Detector::Peak:
            default:               mag = peak; break;
        }

        if (mag >= env)
        {
            env = mag + attackCoeff * (env - mag);
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;
        }
        else
        {
            const auto coeff = autoRelease ? adaptiveReleaseCoeff() : releaseCoeff;
            env = mag + coeff * (env - mag);
        }

        env = flushDenormal (env);
        return env;
    }

private:
    void updateCoeffs() noexcept
    {
        attackCoeff  = timeConstantCoeff (attackTime, sampleRate);
        releaseCoeff = timeConstantCoeff (releaseTime, sampleRate);
        holdSamples  = (int) std::lround ((double) holdTime * 0.001 * sampleRate);
    }

    /** Program-dependent release: a transient-heavy signal (high crest factor)
        releases faster, a sustained one releases slower. */
    float adaptiveReleaseCoeff() const noexcept
    {
        const auto fast = crestFast + 1.0e-12f;
        const auto slow = crestSlow + 1.0e-12f;
        const auto crest = std::sqrt (fast / slow);           // 1 = flat, > 1 = transient
        const auto scale = 1.0f / juce_clamp (crest, 0.33f, 3.0f);
        return timeConstantCoeff (releaseTime * juce_clamp (scale, 0.33f, 3.0f), sampleRate);
    }

    static float juce_clamp (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    double sampleRate = 44100.0;
    Detector detector = Detector::Peak;
    bool autoRelease = false;

    float attackTime = 5.0f, holdTime = 0.0f, releaseTime = 120.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, rmsCoeff = 0.0f;
    float crestFastCoeff = 0.0f, crestSlowCoeff = 0.0f;
    int   holdSamples = 0, holdCounter = 0;

    float env = 0.0f, rmsState = 0.0f, crestFast = 0.0f, crestSlow = 0.0f;
};
} // namespace sf
