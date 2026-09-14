#pragma once
#include "Crossover.h"
#include "EnvelopeFollower.h"
#include <atomic>

namespace sf
{
/**
    Applies the broadband gain signal to the audio, per band and per channel.

    This is the only stage that touches the audio in the broadband path. It owns:

      - the 1-band / 3-band split (LR4, allpass-corrected so the bands recombine flat),
      - per-band depth, ballistics and enable,
      - the channel matrix: Stereo (always linked), Left/Right (link amount), Mid/Side,
      - the optional mono-below-frequency stage.

    Real-time safe: every buffer is sized in `prepare()`.
*/
class MultibandProcessor
{
public:
    struct BandSettings
    {
        bool  enabled   = true;
        float depth     = 1.0f;     // 0..1
        float attackMs  = 0.5f;
        float releaseMs = 20.0f;
    };

    struct Settings
    {
        BandMode    bandMode    = BandMode::Single;
        ChannelMode channelMode = ChannelMode::Stereo;
        float xover1Hz     = 180.0f;
        float xover2Hz     = 2500.0f;
        float channelLink  = 1.0f;   // 0..1
        bool  monoLowEnable = false;
        float monoLowHz     = 120.0f;
        BandSettings bands[kNumBands];
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setSettings (const Settings& s);
    const Settings& getSettings() const noexcept { return settings; }

    /** Applies `gain` (per channel, per sample, in `(0, 1]`) to `audio` in place. */
    void process (juce::AudioBuffer<float>& audio, const juce::AudioBuffer<float>& gain,
                  int numChannels, int numSamples) noexcept;

    /** Smallest gain applied anywhere in the last block, for the GR meter. */
    float getLastMinGain() const noexcept { return lastMinGain; }

    /** Per-band gain reduction in positive dB, for the UI. */
    float getBandReductionDb (int band) const noexcept
    {
        return bandReductionDb[(size_t) juce::jlimit (0, kNumBands - 1, band)].load (std::memory_order_relaxed);
    }

    /** Number of bands currently active. */
    int getActiveBandCount() const noexcept
    {
        return settings.bandMode == BandMode::Three ? kNumBands : 1;
    }

private:
    void updateCoefficients();
    void applyMonoLow (juce::AudioBuffer<float>& audio, int numChannels, int numSamples) noexcept;

    static constexpr int kMaxCh = kMaxChannels;

    double sr = 44100.0;
    int preparedChannels = 0;
    int preparedSamples = 0;
    Settings settings;

    // Split buffers: one per band, each with the prepared channel count.
    juce::AudioBuffer<float> bandBuffer[kNumBands];
    juce::AudioBuffer<float> monoLowBuffer;

    TwoWayCrossover<kMaxCh>  crossoverLow;    // xover1: band 1 vs the rest
    TwoWayCrossover<kMaxCh>  crossoverHigh;   // xover2: band 2 vs band 3
    CrossoverAllpass<kMaxCh> lowAllpass;      // keeps band 1 aligned with 2 + 3
    TwoWayCrossover<kMaxCh>  monoLowCrossover;

    // Depth and channel link scale the applied gain directly, so they are ramped
    // across the block rather than stepped when they are automated.
    float currentDepth[kNumBands] { 1.0f, 1.0f, 1.0f };
    float currentLink = 1.0f;

    // Per-band, per-channel ballistic state on the applied gain.
    float bandGainState[kNumBands][kMaxCh] {};
    float attackCoeff[kNumBands] {};
    float releaseCoeff[kNumBands] {};

    std::atomic<float> bandReductionDb[kNumBands] {};
    float lastMinGain = 1.0f;
};
} // namespace sf
