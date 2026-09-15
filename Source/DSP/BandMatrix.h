#pragma once
#include "BandProcessor.h"
#include "LinkwitzRileyCrossover.h"
#include <array>

namespace sf
{
/**
    The Band Matrix: a frequency-selective sidechain router.

    It splits the destination into 1 to 4 bands, gives each one its own detector
    routing and its own processor, aligns them all to the deepest latency in the
    matrix, and sums them back. That last part is the reason it is not simply
    three compressors side by side — the crossover network is allpass-corrected,
    every band is sample-aligned to every other, and the recombination is flat.

    Solo, mute and bypass are resolved here rather than inside the bands, because
    they are decisions about the *sum*: soloing one band has to silence the
    others, which no band can know on its own.

    Real-time safe. Everything, including the worst-case FFT and delay memory for
    all four bands, is allocated in `prepare()`.
*/
class BandMatrix
{
public:
    struct Settings
    {
        int numBands = 3;
        float crossoverHz[kMaxCrossovers] { 120.0f, 2500.0f, 8000.0f };

        BandProcessor::Settings bands[kMaxMatrixBands];
        bool solo[kMaxMatrixBands] { false, false, false, false };
        bool mute[kMaxMatrixBands] { false, false, false, false };
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setSettings (const Settings& s);
    const Settings& getSettings() const noexcept { return settings; }

    /** The latency the whole matrix introduces: the deepest of any active band.
        Every other band is padded up to it. */
    int getLatencySamples() const noexcept { return alignedLatency; }

    /**
        Splits `audio`, processes each band, and sums the result back into
        `audio`.

        `sidechain` is the external (or internal) sidechain, already gain-staged
        by the caller. `curveGain` is the shared rhythmic envelope, or null.
    */
    void process (juce::AudioBuffer<float>& audio,
                  const juce::AudioBuffer<float>& sidechain,
                  const float* curveGain,
                  int numChannels, int numSamples) noexcept;

    // -- UI state --------------------------------------------------------------
    const BandProcessor& getBand (int index) const noexcept
    {
        return bands[(size_t) juce::jlimit (0, kMaxMatrixBands - 1, index)];
    }

    BandProcessor& getBand (int index) noexcept
    {
        return bands[(size_t) juce::jlimit (0, kMaxMatrixBands - 1, index)];
    }

    /** The smoothed crossover position actually in the filters right now, which
        is what the display should draw while a handle is being dragged. */
    float getCrossoverHz (int index) const noexcept { return destCrossover.getCrossoverHz (index); }

    /** True while any band is soloed. */
    bool isAnyBandSoloed() const noexcept { return anySolo; }

    /** Deepest gain applied anywhere in the matrix, for the global GR meter. */
    float getLastMinGain() const noexcept { return lastMinGain; }

    /** The frequency range a band currently covers, for labelling. */
    void getBandRange (int index, float& lowHz, float& highHz) const noexcept;

private:
    void updateLatencyAlignment();
    const juce::AudioBuffer<float>& detectorSourceFor (int band, const juce::AudioBuffer<float>& sidechain,
                                                       int numSamples) noexcept;

    static constexpr int kMaxCh = kMaxChannels;

    double sr = 44100.0;
    int preparedChannels = 0;
    int preparedSamples = 0;
    Settings settings;

    LinkwitzRileyCrossover<kMaxCh> destCrossover;
    LinkwitzRileyCrossover<kMaxCh> sideCrossover;

    std::array<juce::AudioBuffer<float>, kMaxMatrixBands> destBands;
    std::array<juce::AudioBuffer<float>, kMaxMatrixBands> sideBands;
    std::array<BandProcessor, kMaxMatrixBands> bands;

    // Pads each band up to the deepest latency in the matrix, so switching a
    // band's mode never shifts it against the others.
    std::array<DelayLine, kMaxMatrixBands> alignDelays;

    juce::AudioBuffer<float> internalFull;   // the destination, for Internal routing

    int alignedLatency = 0;
    bool anySolo = false;
    float lastMinGain = 1.0f;
};
} // namespace sf
