#pragma once
#include "EnvelopeFollower.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace sf
{
/**
    Feed-forward compressor.

    It consumes the detector envelope produced by `SidechainDetector` (a fast
    linear magnitude, already stereo-linked) and emits a **gain multiplier** in
    `(0, 1]` per sample and channel. It never touches the audio itself — applying
    the gain is the multiband stage's job — which is what lets Hybrid mode combine
    this engine with the curve engine by simple multiplication.

    Ballistics run on the gain-reduction signal in dB, not on the detector, so
    Attack / Hold / Release mean what a mixing engineer expects them to mean.

    Real-time safe: `prepare()` sizes everything, `process()` only does arithmetic.
*/
class CompressorEngine
{
public:
    struct Settings
    {
        float thresholdDb = -20.0f;
        float ratio       = 4.0f;
        float kneeDb      = 6.0f;
        float attackMs    = 5.0f;
        float holdMs      = 0.0f;
        float releaseMs   = 120.0f;
        float rangeDb     = 60.0f;   // maximum gain reduction, positive dB
        bool  autoRelease = false;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setSettings (const Settings& s) noexcept;
    const Settings& getSettings() const noexcept { return settings; }

    /** Static transfer curve, exposed for the UI and for tests.
        Returns gain reduction in dB (<= 0) for an input level in dB. */
    float computeGainReductionDb (float inputDb) const noexcept;

    /** `envelope` holds the per-sample linear detector magnitude per channel.
        Writes the resulting gain multiplier into `gainOut`. */
    void process (const juce::AudioBuffer<float>& envelope,
                  juce::AudioBuffer<float>& gainOut,
                  int numChannels, int numSamples) noexcept;

    /** Deepest reduction applied by the last `process()` call, in positive dB. */
    float getLastMaxReductionDb() const noexcept { return lastMaxReductionDb; }

    /** Static make-up gain that compensates the curve's loss at 0 dBFS-ish
        programme level. Linear multiplier, always >= 1. */
    float computeAutoMakeupGain() const noexcept;

private:
    void updateCoefficients() noexcept;
    float releaseCoeffFor (float channelGrDb, int channel) const noexcept;

    static constexpr int kMaxCh = kMaxChannels;

    double sr = 44100.0;
    Settings settings;

    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    int   holdSamples = 0;

    // Range is ramped rather than applied as a step: it clamps the gain
    // reduction directly, so automating it would otherwise jump the output by
    // the whole difference in one sample.
    float currentRangeDb = 60.0f;

    // Per-channel ballistic state.
    float grDb[kMaxCh] {};          // current gain reduction, <= 0
    float grAverage[kMaxCh] {};     // slow average of |grDb|, for auto release
    int   holdCounter[kMaxCh] {};
    float grAverageCoeff = 0.0f;

    int   preparedChannels = 0;
    float lastMaxReductionDb = 0.0f;
};
} // namespace sf
