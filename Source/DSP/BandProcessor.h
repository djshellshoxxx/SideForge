#pragma once
#include "CompressorEngine.h"
#include "DelayLine.h"
#include "Metering.h"
#include "SidechainDetector.h"
#include "SpectralDucker.h"
#include "../Utilities/SmoothedValueHelpers.h"
#include <atomic>

namespace sf
{
/** What a Band Matrix band does to its slice of the spectrum. */
enum class BandProcessorMode
{
    Off = 0,      ///< Passes through untouched; processing is suspended.
    Ducker,       ///< Steep, fixed-depth attenuation keyed from the detector.
    Compressor,   ///< Conventional ratio-based compression.
    Limiter,      ///< Sidechain-controlled ceiling on this band.
    Curve,        ///< Follows the drawn rhythmic envelope.
    Spectral,     ///< STFT collision ducking inside this band.
    Listen        ///< Auditions the detector signal instead of the audio.
};

inline constexpr int kNumBandModes = 7;

/** Where a band's detector gets its signal. The band's own filter, if enabled,
    is applied on top of whichever of these is chosen. */
enum class BandScSource
{
    External = 0,     ///< The whole external sidechain.
    ExternalBand,     ///< The sidechain split by the same crossover network.
    Internal,         ///< The whole destination signal.
    InternalBand      ///< This band of the destination signal.
};

inline constexpr int kNumBandScSources = 4;

/**
    One band of the Band Matrix.

    It owns a detector, a dynamics processor and — only when the band asks for it
    — a spectral ducker, and it reuses the same `SidechainDetector`,
    `CompressorEngine` and `SpectralDucker` the full-range path uses rather than
    reimplementing any of them. Ducker, Compressor and Limiter are one engine
    driven by three different parameter mappings, which is why they behave
    consistently and share their ballistics and metering.

    The band's audio is delayed by its own lookahead here; the *extra* delay
    needed to line this band up with the deepest band in the matrix is applied by
    `BandMatrix`, not by this class.

    Real-time safe: every buffer is sized in `prepare()`.
*/
class BandProcessor
{
public:
    struct Settings
    {
        BandProcessorMode mode = BandProcessorMode::Off;
        bool enabled = true;
        bool bypass = false;

        // -- level ------------------------------------------------------------
        float depth     = 1.0f;    ///< 0..1, scales every kind of reduction
        float rangeDb   = 18.0f;   ///< maximum reduction before depth scaling
        float gainDb    = 0.0f;    ///< band output trim
        float makeupDb  = 0.0f;    ///< compressor/limiter make-up
        float pan       = 0.0f;    ///< -1 .. 1
        float mix       = 1.0f;    ///< 0..1 dry/wet within the band

        // -- dynamics ---------------------------------------------------------
        float thresholdDb = -30.0f;
        float ratio       = 4.0f;
        float kneeDb      = 6.0f;
        float attackMs    = 5.0f;
        float holdMs      = 0.0f;
        float releaseMs   = 120.0f;
        bool  autoRelease = false;
        Detector detector = Detector::Peak;
        float lookaheadMs = 0.0f;

        // -- detector routing --------------------------------------------------
        BandScSource scSource = BandScSource::External;
        bool  scFilterEnable = false;
        float scHighPassHz = 20.0f;
        float scLowPassHz = 20000.0f;
        bool  scListen = false;      ///< audition this band's detector signal

        // -- spectral mode -----------------------------------------------------
        SpectralDucker::Settings spectral;
        int spectralFftOrder = 10;
        int spectralOverlap = 4;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setSettings (const Settings& s);
    const Settings& getSettings() const noexcept { return settings; }

    /** Samples of latency this band introduces on its own. */
    int getLatencySamples() const noexcept;

    /** True when the band does nothing at all and can be skipped entirely. */
    bool isPassThrough() const noexcept
    {
        return settings.bypass || ! settings.enabled || settings.mode == BandProcessorMode::Off;
    }

    /**
        Processes this band in place.

        `detectorSource` is whatever the caller decided this band should listen
        to; `curveGain` is the shared rhythmic envelope, or null when the curve
        engine is not running.
    */
    void process (juce::AudioBuffer<float>& bandAudio,
                  const juce::AudioBuffer<float>& detectorSource,
                  const float* curveGain,
                  int numChannels, int numSamples) noexcept;

    /** Keeps the band's delay lines moving while it is passing through, so that
        switching it back on does not replay stale audio. */
    void advanceBypassed (juce::AudioBuffer<float>& bandAudio, int numChannels, int numSamples) noexcept;

    // -- UI state (atomics) ---------------------------------------------------
    float getGainReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept     { return outputLevel.load (std::memory_order_relaxed); }
    float getDetectorLevel() const noexcept   { return detectorLevel.load (std::memory_order_relaxed); }

    /** The band's spectral snapshot, for the visualiser. Only meaningful while
        the band is in Spectral mode. */
    const SpectralSnapshot& readSpectralSnapshot() noexcept { return spectral.readSnapshot(); }

private:
    void applyDynamics (const juce::AudioBuffer<float>& detectorSource,
                        int numChannels, int numSamples) noexcept;
    void applyCurve (const float* curveGain, int numChannels, int numSamples) noexcept;

    static constexpr int kMaxCh = kMaxChannels;

    double sr = 44100.0;
    int preparedChannels = 0;
    int preparedSamples = 0;
    Settings settings;

    SidechainDetector detector;
    CompressorEngine compressor;
    SpectralDucker spectral;

    DelayLine lookaheadDelay;
    DelayLine detectorDelay;

    juce::AudioBuffer<float> dryBand;      // pre-processing copy, for the band mix
    juce::AudioBuffer<float> gainBuffer;   // per-sample gain, per channel
    juce::AudioBuffer<float> alignedSc;    // detector source, lookahead-aligned

    ParamSmoother outputGain, makeupGain, mixAmount, panAmount;

    std::atomic<float> reductionDb { 0.0f };
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> detectorLevel { 0.0f };
};
} // namespace sf
