#pragma once
#include "../Parameters/ParamIDs.h"
#include <juce_core/juce_core.h>
#include <atomic>

namespace sf
{
/** Fixed configuration implied by a quality mode. */
struct QualityConfig
{
    int   fftOrder;          // STFT size = 1 << fftOrder
    int   overlap;           // hop = fftSize / overlap
    float maxLookaheadMs;    // user lookahead is clamped to this
    bool  spectralAvailable; // Live runs broadband engines only

    int fftSize() const noexcept { return 1 << fftOrder; }
    int hopSize() const noexcept { return fftSize() / overlap; }
};

inline QualityConfig qualityConfig (Quality q) noexcept
{
    switch (q)
    {
        case Quality::Live:     return {  9, 4,  0.0f, false };  // 512
        case Quality::Spectral: return { 11, 4, 10.0f, true  };  // 2048
        case Quality::Render:   return { 12, 8, 20.0f, true  };  // 4096
        case Quality::Studio:
        default:                return { 10, 4,  5.0f, true  };  // 1024
    }
}

/** The largest STFT any quality mode can ask for; everything is preallocated to it. */
inline constexpr int kMaxFftOrder = 12;
inline constexpr int kMaxFftSize  = 1 << kMaxFftOrder;
/** The largest lookahead any quality mode allows, in milliseconds. */
inline constexpr float kMaxLookaheadMs = 20.0f;

/**
    Single source of truth for reported latency.

    `latency = lookaheadSamples + spectralLatencySamples`.

    The audio thread calls `update()` every block; when the total changes it raises
    a flag that the message thread drains in order to call
    `AudioProcessor::setLatencySamples`, which must not be called from the audio
    thread. Nothing here allocates.
*/
class LatencyManager
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        lookahead = 0;
        spectral  = 0;
        total.store (0, std::memory_order_relaxed);
        changed.store (true, std::memory_order_release);
    }

    /** Audio thread. Returns the total latency in samples. */
    int update (float lookaheadMs, Quality quality, bool spectralActive) noexcept
    {
        const auto cfg = qualityConfig (quality);
        const auto clampedMs = juce::jlimit (0.0f, cfg.maxLookaheadMs, lookaheadMs);

        lookahead = (int) std::lround ((double) clampedMs * 0.001 * sampleRate);

        // The STFT reports latency only while it is actually in the signal path;
        // switching it off must give the host its samples back.
        spectral = (spectralActive && cfg.spectralAvailable) ? cfg.fftSize() : 0;

        const auto sum = lookahead + spectral;

        if (sum != total.load (std::memory_order_relaxed))
        {
            total.store (sum, std::memory_order_relaxed);
            changed.store (true, std::memory_order_release);
        }

        return sum;
    }

    int getLookaheadSamples() const noexcept { return lookahead; }
    int getSpectralSamples() const noexcept  { return spectral; }
    int getTotalSamples() const noexcept     { return total.load (std::memory_order_relaxed); }

    /** Message thread: true exactly once per change, so the caller can push the
        new value to the host. */
    bool consumeChange() noexcept
    {
        return changed.exchange (false, std::memory_order_acq_rel);
    }

    /** Worst-case lookahead delay in samples, for preallocation. */
    static int maxLookaheadSamples (double sampleRate) noexcept
    {
        return (int) std::lround ((double) kMaxLookaheadMs * 0.001 * sampleRate) + 1;
    }

    /** Worst-case total latency in samples, for preallocating the dry delay. */
    static int maxTotalSamples (double sampleRate) noexcept
    {
        return maxLookaheadSamples (sampleRate) + kMaxFftSize;
    }

private:
    double sampleRate = 44100.0;
    int lookahead = 0;
    int spectral  = 0;
    std::atomic<int>  total   { 0 };
    std::atomic<bool> changed { true };
};
} // namespace sf
