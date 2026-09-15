#include "BandProcessor.h"
#include "../Utilities/SmoothedValueHelpers.h"
#include <cmath>

namespace sf
{
namespace
{
/** Ducker and Limiter are the compressor engine with the ratio pinned. A ducker
    is steep but not absolute; a limiter is a wall. */
constexpr float kDuckerRatio  = 20.0f;
constexpr float kLimiterRatio = 40.0f;    // reads as inf:1 in the engine
/** A limiter that lets a transient through is not a limiter. */
constexpr float kLimiterMaxAttackMs = 5.0f;

/** Constant-power pan. */
inline void panGains (float pan, float& left, float& right) noexcept
{
    const auto angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    left = std::cos (angle) * juce::MathConstants<float>::sqrt2;
    right = std::sin (angle) * juce::MathConstants<float>::sqrt2;
}
} // namespace

void BandProcessor::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedChannels = juce::jlimit (1, kMaxCh, numChannels);
    preparedSamples = juce::jmax (1, maxBlockSize);

    detector.prepare (sr, preparedSamples, kMaxCh);
    compressor.prepare (sr, preparedSamples, preparedChannels);
    spectral.prepare (sr, preparedSamples, preparedChannels);

    const auto maxLookahead = LatencyManager::maxLookaheadSamples (sr);
    lookaheadDelay.prepare (preparedChannels, maxLookahead);
    detectorDelay.prepare (kMaxCh, maxLookahead);

    dryBand.setSize (preparedChannels, preparedSamples, false, true, true);
    gainBuffer.setSize (preparedChannels, preparedSamples, false, true, true);
    alignedSc.setSize (kMaxCh, preparedSamples, false, true, true);

    outputGain.prepare (sr);
    makeupGain.prepare (sr);
    mixAmount.prepare (sr);
    panAmount.prepare (sr);

    outputGain.snapTo (1.0f);
    makeupGain.snapTo (1.0f);
    mixAmount.snapTo (1.0f);
    panAmount.snapTo (0.0f);

    setSettings (settings);
    reset();
}

void BandProcessor::reset()
{
    detector.reset();
    compressor.reset();
    spectral.reset();
    lookaheadDelay.reset();
    detectorDelay.reset();
    dryBand.clear();
    gainBuffer.clear();
    alignedSc.clear();

    reductionDb.store (0.0f, std::memory_order_relaxed);
    outputLevel.store (0.0f, std::memory_order_relaxed);
    detectorLevel.store (0.0f, std::memory_order_relaxed);
}

void BandProcessor::setSettings (const Settings& s)
{
    settings = s;
    settings.depth = juce::jlimit (0.0f, 1.0f, settings.depth);
    settings.mix = juce::jlimit (0.0f, 1.0f, settings.mix);
    settings.pan = juce::jlimit (-1.0f, 1.0f, settings.pan);
    settings.rangeDb = juce::jlimit (0.0f, 60.0f, settings.rangeDb);
    settings.lookaheadMs = juce::jlimit (0.0f, kMaxLookaheadMs, settings.lookaheadMs);

    // -- detector -------------------------------------------------------------
    // With the filter switched off the detector still runs, just wide open, so
    // that toggling it does not change the detector's character.
    const auto nyquistLimit = (float) (sr * 0.49);

    if (settings.scFilterEnable)
        detector.setFilters (settings.scHighPassHz, settings.scLowPassHz);
    else
        detector.setFilters (10.0f, nyquistLimit);

    detector.setDetectorType (settings.detector);
    detector.setGainDb (0.0f);
    detector.setStereoLink (100.0f);

    // -- dynamics -------------------------------------------------------------
    CompressorEngine::Settings comp;
    comp.thresholdDb = settings.thresholdDb;
    comp.kneeDb      = settings.kneeDb;
    comp.attackMs    = settings.attackMs;
    comp.holdMs      = settings.holdMs;
    comp.releaseMs   = settings.releaseMs;
    comp.autoRelease = settings.autoRelease;

    // Depth is the headline control: it scales the maximum reduction, so 0 %
    // really is "off" and 100 % is exactly the configured range.
    comp.rangeDb = settings.rangeDb * settings.depth;

    switch (settings.mode)
    {
        case BandProcessorMode::Ducker:
            comp.ratio = kDuckerRatio;
            break;

        case BandProcessorMode::Limiter:
            comp.ratio = kLimiterRatio;
            comp.kneeDb = settings.kneeDb * 0.25f;   // a limiter's corner is tighter
            comp.attackMs = juce::jmin (settings.attackMs, kLimiterMaxAttackMs);
            break;

        case BandProcessorMode::Compressor:
        default:
            comp.ratio = settings.ratio;
            break;
    }

    compressor.setSettings (comp);

    // -- spectral -------------------------------------------------------------
    auto spectralSettings = settings.spectral;
    spectralSettings.amount = settings.depth;
    spectral.setSettings (spectralSettings);
    spectral.setConfig (settings.spectralFftOrder, settings.spectralOverlap);

    // -- levels ---------------------------------------------------------------
    const auto makeupActive = (settings.mode == BandProcessorMode::Compressor
                            || settings.mode == BandProcessorMode::Limiter);

    outputGain.setTarget (juce::Decibels::decibelsToGain (settings.gainDb));
    makeupGain.setTarget (makeupActive ? juce::Decibels::decibelsToGain (settings.makeupDb) : 1.0f);
    mixAmount.setTarget (settings.mix);
    panAmount.setTarget (settings.pan);

    lookaheadDelay.setDelay ((int) std::lround ((double) settings.lookaheadMs * 0.001 * sr));
    detectorDelay.setDelay ((int) std::lround ((double) settings.lookaheadMs * 0.001 * sr));
}

int BandProcessor::getLatencySamples() const noexcept
{
    if (isPassThrough())
        return 0;

    const auto lookahead = (int) std::lround ((double) settings.lookaheadMs * 0.001 * sr);

    return lookahead + (settings.mode == BandProcessorMode::Spectral ? spectral.getPendingLatencySamples() : 0);
}

void BandProcessor::advanceBypassed (juce::AudioBuffer<float>& bandAudio,
                                     int numChannels, int numSamples) noexcept
{
    juce::ignoreUnused (bandAudio, numChannels);

    // Nothing to keep aligned: a pass-through band reports zero latency, so the
    // matrix pads it instead. Only the meters need to settle back.
    reductionDb.store (0.0f, std::memory_order_relaxed);
    juce::ignoreUnused (numSamples);
}

void BandProcessor::applyDynamics (const juce::AudioBuffer<float>& detectorSource,
                                   int numChannels, int numSamples) noexcept
{
    detector.process (detectorSource, 0, juce::jmin (kMaxCh, detectorSource.getNumChannels()),
                      numSamples);

    compressor.process (detector.getEnvelopeBuffer(), gainBuffer, numChannels, numSamples);
}

void BandProcessor::applyCurve (const float* curveGain, int numChannels, int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* g = gainBuffer.getWritePointer (ch);

        if (curveGain == nullptr)
        {
            juce::FloatVectorOperations::fill (g, 1.0f, numSamples);
            continue;
        }

        for (int i = 0; i < numSamples; ++i)
            g[i] = juce::jlimit (0.0f, 1.0f, 1.0f - settings.depth * (1.0f - curveGain[i]));
    }
}

void BandProcessor::process (juce::AudioBuffer<float>& bandAudio,
                             const juce::AudioBuffer<float>& detectorSource,
                             const float* curveGain,
                             int numChannels, int numSamples) noexcept
{
    const auto channels = juce::jlimit (1, juce::jmin (preparedChannels, bandAudio.getNumChannels()),
                                        numChannels);
    numSamples = juce::jmin (numSamples, juce::jmin (bandAudio.getNumSamples(), preparedSamples));

    if (numSamples <= 0)
        return;

    // ---- listen: hand back the detector signal instead of the audio ---------
    if (settings.mode == BandProcessorMode::Listen || settings.scListen)
    {
        detector.process (detectorSource, 0, juce::jmin (kMaxCh, detectorSource.getNumChannels()),
                          numSamples);

        const auto& filtered = detector.getFiltered();

        for (int ch = 0; ch < channels; ++ch)
            bandAudio.copyFrom (ch, 0, filtered,
                                juce::jmin (ch, filtered.getNumChannels() - 1), 0, numSamples);

        reductionDb.store (0.0f, std::memory_order_relaxed);
        outputLevel.store (bandAudio.getMagnitude (0, 0, numSamples), std::memory_order_relaxed);
        return;
    }

    // ---- lookahead ----------------------------------------------------------
    lookaheadDelay.process (bandAudio, numSamples);

    for (int ch = 0; ch < channels; ++ch)
        dryBand.copyFrom (ch, 0, bandAudio, ch, 0, numSamples);

    // The detector must see the *undelayed* source, which is what lookahead is
    // for; the spectral stage, which works on the delayed audio, needs it
    // delayed to match.
    for (int ch = 0; ch < kMaxCh; ++ch)
        alignedSc.copyFrom (ch, 0, detectorSource,
                            juce::jmin (ch, detectorSource.getNumChannels() - 1), 0, numSamples);

    detectorDelay.process (alignedSc, numSamples);

    // ---- gain signal --------------------------------------------------------
    switch (settings.mode)
    {
        case BandProcessorMode::Ducker:
        case BandProcessorMode::Compressor:
        case BandProcessorMode::Limiter:
            applyDynamics (detectorSource, channels, numSamples);
            break;

        case BandProcessorMode::Curve:
            applyCurve (curveGain, channels, numSamples);
            break;

        case BandProcessorMode::Spectral:
        case BandProcessorMode::Off:
        default:
            for (int ch = 0; ch < channels; ++ch)
                juce::FloatVectorOperations::fill (gainBuffer.getWritePointer (ch), 1.0f, numSamples);
            break;
    }

    // ---- apply --------------------------------------------------------------
    auto deepest = 1.0f;

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* audio = bandAudio.getWritePointer (ch);
        const auto* g = gainBuffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto gain = juce::jlimit (0.0f, 1.0f, g[i]);
            audio[i] *= gain;
            deepest = juce::jmin (deepest, gain);
        }
    }

    if (settings.mode == BandProcessorMode::Spectral)
    {
        spectral.process (bandAudio, alignedSc, channels, numSamples);
        deepest = juce::jmin (deepest, spectral.getLastMinGain());
    }

    // ---- makeup, trim, pan, band mix ---------------------------------------
    const auto stereo = (channels >= 2);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto makeup = makeupGain.next();
        const auto trim   = outputGain.next();
        const auto mix    = mixAmount.next();
        const auto pan    = panAmount.next();

        float leftPan = 1.0f, rightPan = 1.0f;

        if (stereo)
            panGains (pan, leftPan, rightPan);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* audio = bandAudio.getWritePointer (ch);
            const auto dry = dryBand.getReadPointer (ch)[i];
            const auto wet = audio[i] * makeup;

            auto value = dry + (wet - dry) * mix;
            value *= trim;

            if (stereo)
                value *= (ch == 0 ? leftPan : rightPan);

            audio[i] = flushDenormal (sanitise (value));
        }
    }

    reductionDb.store (-juce::Decibels::gainToDecibels (deepest, -80.0f), std::memory_order_relaxed);
    outputLevel.store (bandAudio.getMagnitude (0, 0, numSamples), std::memory_order_relaxed);
    detectorLevel.store (detector.getEnvelopeBuffer().getMagnitude (0, 0, numSamples),
                         std::memory_order_relaxed);
}
} // namespace sf
