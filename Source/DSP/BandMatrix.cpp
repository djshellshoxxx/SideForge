#include "BandMatrix.h"

namespace sf
{
void BandMatrix::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedChannels = juce::jlimit (1, kMaxCh, numChannels);
    preparedSamples = juce::jmax (1, maxBlockSize);

    destCrossover.prepare (sr, preparedSamples, preparedChannels);
    sideCrossover.prepare (sr, preparedSamples, kMaxCh);

    // The alignment delay has to cover the worst case a band can ask for: the
    // largest lookahead plus the largest FFT frame.
    const auto maxAlign = LatencyManager::maxTotalSamples (sr);

    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        destBands[(size_t) b].setSize (preparedChannels, preparedSamples, false, true, true);
        sideBands[(size_t) b].setSize (kMaxCh, preparedSamples, false, true, true);
        bands[(size_t) b].prepare (sr, preparedSamples, preparedChannels);
        alignDelays[(size_t) b].prepare (preparedChannels, maxAlign);
    }

    internalFull.setSize (kMaxCh, preparedSamples, false, true, true);

    setSettings (settings);
    reset();
}

void BandMatrix::reset()
{
    destCrossover.reset();
    sideCrossover.reset();

    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        destBands[(size_t) b].clear();
        sideBands[(size_t) b].clear();
        bands[(size_t) b].reset();
        alignDelays[(size_t) b].reset();
    }

    internalFull.clear();
    lastMinGain = 1.0f;
}

void BandMatrix::setSettings (const Settings& s)
{
    settings = s;
    settings.numBands = juce::jlimit (1, kMaxMatrixBands, settings.numBands);

    destCrossover.setNumBands (settings.numBands);
    sideCrossover.setNumBands (settings.numBands);
    destCrossover.setCrossovers (settings.crossoverHz, kMaxCrossovers);
    sideCrossover.setCrossovers (settings.crossoverHz, kMaxCrossovers);

    anySolo = false;

    for (int b = 0; b < settings.numBands; ++b)
    {
        bands[(size_t) b].setSettings (settings.bands[b]);
        anySolo = anySolo || settings.solo[b];
    }

    updateLatencyAlignment();
}

void BandMatrix::updateLatencyAlignment()
{
    auto deepest = 0;

    for (int b = 0; b < settings.numBands; ++b)
        deepest = juce::jmax (deepest, bands[(size_t) b].getLatencySamples());

    alignedLatency = deepest;

    // Every band, including the ones doing nothing, is padded up to the deepest.
    // That is what stops switching a band's mode from shifting it in time
    // against its neighbours and smearing the recombination.
    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        const auto own = b < settings.numBands ? bands[(size_t) b].getLatencySamples() : 0;
        alignDelays[(size_t) b].setDelay (juce::jmax (0, deepest - own));
    }
}

void BandMatrix::getBandRange (int index, float& lowHz, float& highHz) const noexcept
{
    index = juce::jlimit (0, kMaxMatrixBands - 1, index);

    lowHz = (index == 0) ? 20.0f : destCrossover.getCrossoverHz (index - 1);
    highHz = (index >= settings.numBands - 1) ? (float) (sr * 0.5)
                                              : destCrossover.getCrossoverHz (index);
}

const juce::AudioBuffer<float>& BandMatrix::detectorSourceFor (int band,
                                                               const juce::AudioBuffer<float>& sidechain,
                                                               int numSamples) noexcept
{
    juce::ignoreUnused (numSamples);

    switch (settings.bands[band].scSource)
    {
        case BandScSource::ExternalBand:  return sideBands[(size_t) band];
        case BandScSource::Internal:      return internalFull;
        case BandScSource::InternalBand:  return destBands[(size_t) band];
        case BandScSource::External:
        default:                          return sidechain;
    }
}

void BandMatrix::process (juce::AudioBuffer<float>& audio,
                          const juce::AudioBuffer<float>& sidechain,
                          const float* curveGain,
                          int numChannels, int numSamples) noexcept
{
    const auto channels = juce::jlimit (1, juce::jmin (preparedChannels, audio.getNumChannels()),
                                        numChannels);
    numSamples = juce::jmin (numSamples, juce::jmin (audio.getNumSamples(), preparedSamples));

    if (numSamples <= 0)
        return;

    const auto numBands = settings.numBands;

    // ---- crossover glide ----------------------------------------------------
    destCrossover.updateSmoothing (numSamples);
    sideCrossover.updateSmoothing (numSamples);

    // ---- keep a copy of the untouched destination for Internal routing ------
    for (int ch = 0; ch < kMaxCh; ++ch)
        internalFull.copyFrom (ch, 0, audio, juce::jmin (ch, channels - 1), 0, numSamples);

    // ---- split ---------------------------------------------------------------
    destCrossover.process (audio, destBands, channels, numSamples);
    sideCrossover.process (sidechain, sideBands, kMaxCh, numSamples);

    // Latency can change when a band switches mode, so it is re-derived every
    // block. `setDelay` only moves an index; nothing is allocated here.
    updateLatencyAlignment();

    // ---- process each band ---------------------------------------------------
    auto deepestGain = 1.0f;

    for (int b = 0; b < numBands; ++b)
    {
        auto& band = bands[(size_t) b];
        auto& bandAudio = destBands[(size_t) b];

        if (band.isPassThrough())
        {
            band.advanceBypassed (bandAudio, channels, numSamples);
        }
        else
        {
            band.process (bandAudio, detectorSourceFor (b, sidechain, numSamples),
                          curveGain, channels, numSamples);

            deepestGain = juce::jmin (deepestGain,
                                      juce::Decibels::decibelsToGain (-band.getGainReductionDb(), -80.0f));
        }

        // Pad up to the deepest band, whatever this one did.
        alignDelays[(size_t) b].process (bandAudio, numSamples);
    }

    // ---- recombine -----------------------------------------------------------
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::clear (audio.getWritePointer (ch), numSamples);

    for (int b = 0; b < numBands; ++b)
    {
        // Solo wins over mute, and a soloed matrix silences everything else.
        const auto heard = anySolo ? settings.solo[b] : ! settings.mute[b];

        if (! heard)
            continue;

        for (int ch = 0; ch < channels; ++ch)
            audio.addFrom (ch, 0, destBands[(size_t) b], ch, 0, numSamples);
    }

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = audio.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
            data[i] = flushDenormal (sanitise (data[i]));
    }

    lastMinGain = deepestGain;
}
} // namespace sf
