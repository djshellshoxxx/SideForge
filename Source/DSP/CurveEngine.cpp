#include "CurveEngine.h"
#include "../Parameters/Divisions.h"
#include "EnvelopeFollower.h"
#include <cmath>

namespace sf
{
namespace
{
/** A table that is always available, so `process()` never has to branch on null. */
const CurveTable& unityTable()
{
    static const CurveTable t = []
    {
        CurveTable table;
        table.samples.fill (1.0f);
        return table;
    }();

    return t;
}

/** Converts a full-scale traversal time into a per-sample step limit.
    0 ms means "no limit", which is what makes the drawn curve play back exactly. */
float slewStep (float milliseconds, double sampleRate) noexcept
{
    if (milliseconds <= 0.0f || sampleRate <= 0.0)
        return 0.0f;

    return (float) (1.0 / ((double) milliseconds * 0.001 * sampleRate));
}
} // namespace

void CurveEngine::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);

    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    setSettings (settings);
    reset();
}

void CurveEngine::reset()
{
    cyclePhase = 0.0;
    pairIndex = 0;
    oneShotActive = false;
    preDelayCounter = 0;
    postDelayCounter = 0;
    heldValue = 1.0f;
    smoothState = 1.0f;
    shapedValue = 1.0f;
    holdCounter = 0;

    uiPhase.store (0.0f, std::memory_order_relaxed);
    uiGain.store (1.0f, std::memory_order_relaxed);
    uiRunning.store (false, std::memory_order_relaxed);
}

void CurveEngine::setSettings (const Settings& s) noexcept
{
    settings = s;
    settings.divisionIndex = juce::jlimit (0, kNumDivisions - 1, settings.divisionIndex);
    settings.rateHz        = juce::jlimit (0.001f, 200.0f, settings.rateHz);
    settings.depth         = juce::jlimit (0.0f, 1.0f, settings.depth);
    settings.phaseOffset   = settings.phaseOffset - std::floor (settings.phaseOffset);
    settings.swing         = juce::jlimit (-0.49f, 0.49f, settings.swing);
    settings.smoothMs      = juce::jmax (0.0f, settings.smoothMs);

    smoothCoeff = timeConstantCoeff (settings.smoothMs, sr);
    attackStep  = slewStep (settings.attackMs, sr);
    recoverStep = slewStep (settings.recoverMs, sr);

    holdSamples      = (int) std::lround ((double) juce::jmax (0.0f, settings.holdMs) * 0.001 * sr);
    preDelaySamples  = (int) std::lround ((double) juce::jmax (0.0f, settings.preDelayMs) * 0.001 * sr);
    postDelaySamples = (int) std::lround ((double) juce::jmax (0.0f, settings.postDelayMs) * 0.001 * sr);
}

double CurveEngine::cycleLengthSamples (const TransportInfo& transport) const noexcept
{
    if (settings.syncMode == SyncMode::Free)
        return sr / (double) juce::jmax (0.001f, settings.rateHz);

    const auto quarters = divisionInQuarters (settings.divisionIndex,
                                              transport.timeSigNumerator,
                                              transport.timeSigDenominator);
    const auto bpm = juce::jlimit (1.0, 999.0, transport.bpm);

    return juce::jmax (1.0, quarters * (60.0 / bpm) * sr);
}

double CurveEngine::applySwing (double phase) const noexcept
{
    const auto swing = (double) settings.swing;

    if (std::abs (swing) < 1.0e-6)
        return phase;

    // The pair of cycles is treated as one span; swing moves the boundary between
    // its two halves, lengthening one cycle and shortening the other.
    const auto split = juce::jlimit (0.02, 0.98, 0.5 + swing);
    const auto pairPhase = (phase + (double) pairIndex) * 0.5;   // 0..1 across two cycles

    if (pairPhase < split)
        return pairPhase / split;

    return (pairPhase - split) / (1.0 - split);
}

float CurveEngine::sampleCurve (double phase) const noexcept
{
    const auto& t = table != nullptr ? *table : unityTable();
    const auto p = phase + (double) settings.phaseOffset;

    return juce::jlimit (0.0f, 1.0f, t.lookup (p));
}

void CurveEngine::startCycle() noexcept
{
    cyclePhase = 0.0;
    oneShotActive = true;
    preDelayCounter = preDelaySamples;
    postDelayCounter = 0;
    pairIndex = 0;
}

void CurveEngine::process (int numSamples, const TransportInfo& transport, double quartersThisBlock,
                           const TriggerEvents& triggers, float* gainOut) noexcept
{
    if (gainOut == nullptr || numSamples <= 0)
        return;

    const auto looping = (settings.trigMode == TrigMode::Loop);
    const auto cycleSamples = cycleLengthSamples (transport);
    const auto increment = 1.0 / juce::jmax (1.0, cycleSamples);

    // --- host-locked phase ---------------------------------------------------
    // In tempo-synced Loop mode the phase is a pure function of the transport
    // position, so a mid-bar start, a loop jump or a tempo change all land exactly
    // where the host says they should.
    const auto tempoLocked = looping
                          && settings.syncMode == SyncMode::Tempo
                          && transport.valid && transport.isPlaying;

    double phasePerSample = increment;

    if (tempoLocked)
    {
        const auto quarters = divisionInQuarters (settings.divisionIndex,
                                                  transport.timeSigNumerator,
                                                  transport.timeSigDenominator);

        if (quarters > 0.0)
        {
            const auto preDelayQuarters = (double) preDelaySamples / juce::jmax (1.0, cycleSamples) * quarters;
            const auto absolute = (transport.ppqPosition - preDelayQuarters) / quarters;

            cyclePhase = absolute - std::floor (absolute);
            pairIndex  = ((int) std::floor (absolute)) & 1;
            phasePerSample = (quartersThisBlock / (double) numSamples) / quarters;
        }
    }

    int nextTrigger = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        // --- triggers ---------------------------------------------------------
        while (nextTrigger < triggers.count && triggers.positions[nextTrigger] == i)
        {
            const auto accept = (settings.trigMode == TrigMode::Audio)
                             || (settings.trigMode == TrigMode::Midi)
                             || (settings.trigMode == TrigMode::OneShot && ! oneShotActive);

            if (accept)
                startCycle();

            ++nextTrigger;
        }

        // --- raw curve value --------------------------------------------------
        float raw;

        if (looping)
        {
            raw = sampleCurve (applySwing (cyclePhase));

            cyclePhase += phasePerSample;

            if (cyclePhase >= 1.0)
            {
                cyclePhase -= std::floor (cyclePhase);
                pairIndex ^= 1;
            }
        }
        else if (! oneShotActive)
        {
            // Idle: hold whatever the last cycle left behind, releasing through
            // the post-delay back to unity.
            if (postDelayCounter > 0)
            {
                --postDelayCounter;
                raw = heldValue;
            }
            else
            {
                raw = 1.0f;
            }
        }
        else if (preDelayCounter > 0)
        {
            --preDelayCounter;
            raw = 1.0f;
        }
        else
        {
            raw = sampleCurve (cyclePhase);
            cyclePhase += increment;

            if (cyclePhase >= 1.0)
            {
                cyclePhase = 1.0;
                oneShotActive = false;
                heldValue = raw;
                postDelayCounter = postDelaySamples;
            }
        }

        // --- depth ------------------------------------------------------------
        auto target = 1.0f - settings.depth * (1.0f - raw);
        target = juce::jlimit (0.0f, 1.0f, target);

        // --- attack / hold / recover as rate limits ---------------------------
        // These cap how fast the gain may move rather than filtering it, so at the
        // default of 0 ms the drawn curve is reproduced exactly.
        if (target < shapedValue)
        {
            const auto delta = shapedValue - target;
            shapedValue -= (attackStep > 0.0f) ? juce::jmin (delta, attackStep) : delta;
            holdCounter = holdSamples;
        }
        else if (target > shapedValue)
        {
            if (holdCounter > 0)
            {
                --holdCounter;
            }
            else
            {
                const auto delta = target - shapedValue;
                shapedValue += (recoverStep > 0.0f) ? juce::jmin (delta, recoverStep) : delta;
            }
        }

        shapedValue = juce::jlimit (0.0f, 1.0f, shapedValue);

        // --- anti-click smoothing ---------------------------------------------
        smoothState = flushDenormal (shapedValue + smoothCoeff * (smoothState - shapedValue));
        gainOut[i] = juce::jlimit (0.0f, 1.0f, smoothState);
    }

    uiPhase.store ((float) (cyclePhase - std::floor (cyclePhase)), std::memory_order_relaxed);
    uiGain.store (gainOut[numSamples - 1], std::memory_order_relaxed);
    uiRunning.store (looping || oneShotActive, std::memory_order_relaxed);
}
} // namespace sf
