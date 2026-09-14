#pragma once
#include "EnvelopeFollower.h"
#include "LatencyManager.h"
#include "../Utilities/LockFreeFifo.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

namespace sf
{
inline constexpr int kMaxBins = kMaxFftSize / 2 + 1;

/** What the spectral stage saw and did, for the visualiser. Published at a
    throttled rate through a `TripleBuffer`, never through a shared pointer. */
struct SpectralSnapshot
{
    int   numBins   = 0;
    int   fftSize   = 0;
    float sampleRate = 44100.0f;

    std::array<float, kMaxBins> mainDb {};     // destination spectrum, dBFS
    std::array<float, kMaxBins> sideDb {};     // sidechain spectrum, dBFS
    std::array<float, kMaxBins> collision {};  // 0..1 collision map
    std::array<float, kMaxBins> gainDb {};     // applied attenuation, negative dB
};

/**
    STFT collision ducking.

    Rather than subtracting an inverted sidechain spectrum — which thins the
    destination wherever the sidechain merely *exists* — this builds a **collision
    map**: per bin, how much of that bin's combined energy belongs to the sidechain,
    gated by whether the sidechain is meaningfully present there at all. Only bins
    where the two sources genuinely compete are attenuated.

    Overlap-add with a Hann window applied on both analysis and synthesis, and a
    normalisation factor measured from the window itself, so with the depth at zero
    the stage reconstructs its input to within a small fraction of a dB.

    Real-time safety: every buffer is sized for `kMaxFftSize` in `prepare()`.
    Changing quality mode only changes indices and re-derives the window; it never
    allocates, and it is applied at a block boundary so the latency the host is told
    about always matches the latency actually in the signal path.
*/
class SpectralDucker
{
public:
    struct Settings
    {
        float depthDb      = 12.0f;   // maximum attenuation
        float lowFreqHz    = 60.0f;
        float highFreqHz   = 16000.0f;
        float freqSmooth   = 0.35f;   // 0..1
        float timeSmooth   = 0.40f;   // 0..1
        float attackMs     = 5.0f;
        float releaseMs    = 80.0f;
        float preserveBody = 0.30f;   // 0..1
        float tonal        = 0.50f;   // 0 = sustained content, 1 = transients
        float guard        = 0.50f;   // 0..1 artifact protection
        float tiltDbPerOct = 0.0f;
        float amount       = 1.0f;    // overall scaling, used by Hybrid's blend
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setSettings (const Settings& s) noexcept;
    const Settings& getSettings() const noexcept { return settings; }

    /** Requests an STFT size / overlap. Applied at the next `process()` call, which
        is a block boundary, so reported latency never disagrees with reality. */
    void setConfig (int fftOrder, int overlap) noexcept;

    /** Latency of the configuration currently in the signal path.

        A sample is only finished once every analysis window that overlaps it has
        been transformed and added in, and the last such window starts on that
        sample and runs a full window past it — so the delay is one whole FFT
        frame, not a hop less. The impulse test pins this down exactly. */
    int getLatencySamples() const noexcept { return fftSize; }

    /** Latency the *pending* configuration will have once applied. */
    int getPendingLatencySamples() const noexcept { return 1 << pendingOrder; }

    /** Processes `audio` in place against `sidechain`. Both are expected to be
        already time-aligned by the caller. */
    void process (juce::AudioBuffer<float>& audio, const juce::AudioBuffer<float>& sidechain,
                  int numChannels, int numSamples) noexcept;

    /** Pushes silence through without attenuating, keeping the delay line aligned
        while the stage is in the path but inactive. */
    void processBypassed (juce::AudioBuffer<float>& audio, int numChannels, int numSamples) noexcept;

    /** Deepest per-bin attenuation applied by the last frame, as a linear gain. */
    float getLastMinGain() const noexcept { return lastMinGain; }

    /** GUI thread. */
    const SpectralSnapshot& readSnapshot() noexcept { return snapshots.read(); }

private:
    void applyPendingConfig() noexcept;
    void rebuildWindow() noexcept;
    void rebuildWeights() noexcept;
    void updateFrameCoefficients() noexcept;
    void processFrame (int numChannels) noexcept;
    void boxSmooth (float* data, int numBins, int radius) noexcept;
    void publishSnapshot (int numChannels) noexcept;

    static constexpr int kMaxCh = kMaxChannels;
    static constexpr int kAccumSize = kMaxFftSize * 2;

    double sr = 44100.0;
    int preparedChannels = 0;
    Settings settings;

    int fftSize = 1024;
    int hopSize = 256;
    int numBins = 513;
    int rover = 0;

    int pendingOrder = 10;
    int pendingOverlap = 4;
    int currentOrder = 10;
    int currentOverlap = 4;

    std::array<std::unique_ptr<juce::dsp::FFT>, kMaxFftOrder + 1> ffts;

    std::array<float, kMaxFftSize> window {};
    float windowNorm = 1.0f;

    std::array<std::array<float, kMaxFftSize>, kMaxCh> inFifo {};
    std::array<std::array<float, kMaxFftSize>, kMaxCh> outFifo {};
    std::array<std::array<float, kAccumSize>, kMaxCh> accum {};
    std::array<float, kMaxFftSize> scFifo {};

    std::array<float, kAccumSize> fftScratch {};
    std::array<float, kAccumSize> scScratch {};

    std::array<float, kMaxBins> scMag {};
    std::array<float, kMaxBins> scMagSlow {};
    std::array<float, kMaxBins> mainMag {};
    std::array<float, kMaxBins> collision {};
    std::array<float, kMaxBins> smoothScratch {};
    std::array<float, kMaxBins> bandWeight {};
    std::array<float, kMaxBins> tiltWeight {};
    std::array<std::array<float, kMaxBins>, kMaxCh> binGain {};

    float attackCoeff = 0.0f, releaseCoeff = 0.0f, slowCoeff = 0.0f, scSlowCoeff = 0.0f;
    int   freqSmoothRadius = 0;

    float lastMinGain = 1.0f;
    int framesSincePublish = 0;
    int publishInterval = 4;

    TripleBuffer<SpectralSnapshot> snapshots;
};
} // namespace sf
