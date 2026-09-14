#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace sf
{
/**
    One meter channel: peak with decay, RMS, latching clip flag.

    The audio thread only ever stores into the atomics; the GUI only ever loads
    from them. Decay is applied on the audio thread so the reading is independent
    of how often the GUI happens to repaint.
*/
class LevelMeter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        peakValue.store (0.0f, std::memory_order_relaxed);
        rmsValue.store (0.0f, std::memory_order_relaxed);
        clipped.store (false, std::memory_order_relaxed);
        peakState = 0.0f;
        msState   = 0.0f;
    }

    /** Audio thread. `numSamples` samples of one channel. */
    void processBlock (const float* data, int numSamples) noexcept
    {
        if (data == nullptr || numSamples <= 0)
            return;

        float blockPeak = 0.0f;
        double sumSq = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto v = data[i];

            if (! std::isfinite (v))
                continue;

            const auto a = std::abs (v);
            blockPeak = juce::jmax (blockPeak, a);
            sumSq += (double) v * (double) v;
        }

        pushBlock (blockPeak, (float) (sumSq / (double) numSamples), numSamples);
    }

    /** Audio thread. Feeds an already-computed block peak / mean square. */
    void pushBlock (float blockPeak, float blockMeanSquare, int numSamples) noexcept
    {
        if (! std::isfinite (blockPeak))  blockPeak = 0.0f;
        if (! std::isfinite (blockMeanSquare) || blockMeanSquare < 0.0f) blockMeanSquare = 0.0f;

        // ~20 dB per second peak fall-back, applied once per block.
        const auto seconds = (double) juce::jmax (1, numSamples) / sr;
        const auto decay   = (float) std::pow (10.0, -20.0 * seconds / 20.0);

        peakState = juce::jmax (blockPeak, peakState * decay);

        // 300 ms RMS window.
        const auto rmsCoeff = (float) std::exp (-seconds / 0.3);
        msState = blockMeanSquare + rmsCoeff * (msState - blockMeanSquare);

        if (blockPeak > 1.0f)
            clipped.store (true, std::memory_order_relaxed);

        peakValue.store (peakState, std::memory_order_relaxed);
        rmsValue.store (std::sqrt (juce::jmax (0.0f, msState)), std::memory_order_relaxed);
    }

    // -- GUI thread ----------------------------------------------------------
    float getPeak() const noexcept { return peakValue.load (std::memory_order_relaxed); }
    float getRms()  const noexcept { return rmsValue.load (std::memory_order_relaxed); }
    bool  isClipped() const noexcept { return clipped.load (std::memory_order_relaxed); }
    void  clearClip() noexcept { clipped.store (false, std::memory_order_relaxed); }

    float getPeakDb() const noexcept { return juce::Decibels::gainToDecibels (getPeak(), -100.0f); }
    float getRmsDb()  const noexcept { return juce::Decibels::gainToDecibels (getRms(),  -100.0f); }

private:
    double sr = 44100.0;
    float peakState = 0.0f, msState = 0.0f;
    std::atomic<float> peakValue { 0.0f }, rmsValue { 0.0f };
    std::atomic<bool>  clipped { false };
};

/**
    Gain-reduction meter. Fed the *minimum* linear gain applied over a block, it
    reports positive dB of reduction with a peak hold that falls back smoothly.
*/
class GainReductionMeter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        state = 0.0f;
        maxDb.store (0.0f, std::memory_order_relaxed);
        currentDb.store (0.0f, std::memory_order_relaxed);
    }

    /** Audio thread. `minGain` is linear in (0, 1]. */
    void pushBlock (float minGain, int numSamples) noexcept
    {
        if (! std::isfinite (minGain) || minGain <= 0.0f)
            minGain = 1.0e-6f;

        const auto reductionDb = -juce::Decibels::gainToDecibels (juce::jmin (1.0f, minGain), -80.0f);

        const auto seconds = (double) juce::jmax (1, numSamples) / sr;
        const auto decay   = (float) std::exp (-seconds / 0.25);

        state = juce::jmax (reductionDb, state * decay);

        currentDb.store (reductionDb, std::memory_order_relaxed);
        maxDb.store (state, std::memory_order_relaxed);
    }

    /** Instantaneous reduction for this block, in positive dB. */
    float getCurrentDb() const noexcept { return currentDb.load (std::memory_order_relaxed); }
    /** Peak-held reduction, in positive dB. */
    float getHeldDb() const noexcept { return maxDb.load (std::memory_order_relaxed); }

private:
    double sr = 44100.0;
    float state = 0.0f;
    std::atomic<float> maxDb { 0.0f }, currentDb { 0.0f };
};

/** Every meter the UI shows, in one place. */
struct MeterBank
{
    static constexpr int kChannels = 2;

    void prepare (double sampleRate) noexcept
    {
        for (int i = 0; i < kChannels; ++i)
        {
            input[i].prepare (sampleRate);
            sidechain[i].prepare (sampleRate);
            output[i].prepare (sampleRate);
        }

        gainReduction.prepare (sampleRate);
    }

    void reset() noexcept
    {
        for (int i = 0; i < kChannels; ++i)
        {
            input[i].reset();
            sidechain[i].reset();
            output[i].reset();
        }

        gainReduction.reset();
    }

    void clearClips() noexcept
    {
        for (int i = 0; i < kChannels; ++i)
        {
            input[i].clearClip();
            sidechain[i].clearClip();
            output[i].clearClip();
        }
    }

    bool anyClip() const noexcept
    {
        for (int i = 0; i < kChannels; ++i)
            if (input[i].isClipped() || output[i].isClipped() || sidechain[i].isClipped())
                return true;

        return false;
    }

    LevelMeter input[kChannels], sidechain[kChannels], output[kChannels];
    GainReductionMeter gainReduction;
};
} // namespace sf
