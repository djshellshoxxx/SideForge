#include "SpectralDucker.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sf
{
namespace
{
/** Sidechain bins quieter than this, relative to the loudest bin of the frame,
    are treated as absent — so a near-silent sidechain never carves anything. */
constexpr float kRelativeFloor = 0.002f;     // ~ -54 dB below the frame peak
constexpr float kAbsoluteFloor = 1.0e-6f;
constexpr float kMagEpsilon    = 1.0e-12f;

/** Blends the sustained and instantaneous views of a sidechain bin. */
inline float blendMagnitude (float sustained, float instant, float towardsInstant) noexcept
{
    return sustained + (instant - sustained) * juce::jlimit (0.0f, 1.0f, towardsInstant);
}
} // namespace

void SpectralDucker::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (maxBlockSize);

    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedChannels = juce::jlimit (1, kMaxCh, numChannels);

    // Every FFT size any quality mode can select is built here, so switching mode
    // later is an index change rather than an allocation.
    for (int order = 9; order <= kMaxFftOrder; ++order)
        ffts[(size_t) order] = std::make_unique<juce::dsp::FFT> (order);

    currentOrder = -1;   // force applyPendingConfig to do the work
    applyPendingConfig();
    reset();
}

void SpectralDucker::reset()
{
    for (int ch = 0; ch < kMaxCh; ++ch)
    {
        inFifo[(size_t) ch].fill (0.0f);
        outFifo[(size_t) ch].fill (0.0f);
        accum[(size_t) ch].fill (0.0f);
        binGain[(size_t) ch].fill (1.0f);
    }

    scFifo.fill (0.0f);
    scMagSlow.fill (0.0f);
    rover = fftSize - hopSize;
    framesSincePublish = 0;
    lastMinGain = 1.0f;
}

void SpectralDucker::setConfig (int fftOrder, int overlap) noexcept
{
    pendingOrder   = juce::jlimit (9, kMaxFftOrder, fftOrder);
    pendingOverlap = (overlap >= 8) ? 8 : 4;
}

void SpectralDucker::applyPendingConfig() noexcept
{
    if (pendingOrder == currentOrder && pendingOverlap == currentOverlap)
        return;

    currentOrder = pendingOrder;
    currentOverlap = pendingOverlap;

    fftSize = 1 << currentOrder;
    hopSize = fftSize / currentOverlap;
    numBins = fftSize / 2 + 1;

    rebuildWindow();
    rebuildWeights();
    updateFrameCoefficients();

    // The delay line's contents belong to the old geometry; starting clean is the
    // only correct answer and costs one window of fade-in.
    for (int ch = 0; ch < kMaxCh; ++ch)
    {
        inFifo[(size_t) ch].fill (0.0f);
        outFifo[(size_t) ch].fill (0.0f);
        accum[(size_t) ch].fill (0.0f);
        binGain[(size_t) ch].fill (1.0f);
    }

    scFifo.fill (0.0f);
    scMagSlow.fill (0.0f);
    rover = fftSize - hopSize;
}

void SpectralDucker::rebuildWindow() noexcept
{
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) fftSize);

    // The window is applied on analysis *and* synthesis, so the overlap-add gain is
    // the sum of the squared window at hop offsets. Measuring it rather than
    // hard-coding 1.5 keeps reconstruction exact for any window / overlap pairing.
    double sum = 0.0;
    const auto centre = fftSize / 2;

    for (int m = -currentOverlap; m <= currentOverlap; ++m)
    {
        const auto index = centre - m * hopSize;

        if (index >= 0 && index < fftSize)
        {
            const auto w = (double) window[(size_t) index];
            sum += w * w;
        }
    }

    windowNorm = (float) (1.0 / juce::jmax (1.0e-9, sum));
}

void SpectralDucker::rebuildWeights() noexcept
{
    const auto binHz = (float) (sr / (double) fftSize);

    const auto lo = juce::jlimit (10.0f, (float) (sr * 0.49), settings.lowFreqHz);
    const auto hi = juce::jlimit (lo * 1.05f, (float) (sr * 0.49), settings.highFreqHz);

    // A third-octave taper at each edge keeps the range control from putting a
    // hard step into the gain curve.
    const auto loEdge = lo / 1.26f;
    const auto hiEdge = hi * 1.26f;

    for (int k = 0; k < numBins; ++k)
    {
        const auto f = juce::jmax (1.0f, (float) k * binHz);

        float w = 1.0f;

        if (f < lo)
            w = f <= loEdge ? 0.0f : (f - loEdge) / juce::jmax (1.0e-6f, lo - loEdge);
        else if (f > hi)
            w = f >= hiEdge ? 0.0f : (hiEdge - f) / juce::jmax (1.0e-6f, hiEdge - hi);

        bandWeight[(size_t) k] = juce::jlimit (0.0f, 1.0f, w);

        const auto octaves = std::log2 (f / 1000.0f);
        tiltWeight[(size_t) k] = juce::jlimit (0.25f,
                                               4.0f,
                                               juce::Decibels::decibelsToGain (settings.tiltDbPerOct * octaves));
    }

    freqSmoothRadius = (int) std::lround ((double) juce::jlimit (0.0f, 1.0f, settings.freqSmooth)
                                          * (double) numBins / 24.0);
}

void SpectralDucker::updateFrameCoefficients() noexcept
{
    // Ballistics run once per frame, so the effective rate is the hop rate.
    const auto frameRate = sr / (double) juce::jmax (1, hopSize);

    attackCoeff  = timeConstantCoeff (settings.attackMs, frameRate);
    releaseCoeff = timeConstantCoeff (settings.releaseMs, frameRate);

    // Time smoothing adds an extra, slower pole on top of attack / release.
    const auto extraMs = juce::jlimit (0.0f, 1.0f, settings.timeSmooth) * 250.0f;
    slowCoeff = timeConstantCoeff (extraMs, frameRate);

    // The "tonal" reference magnitude is a sustained view of the sidechain.
    scSlowCoeff = timeConstantCoeff (120.0f, frameRate);

    publishInterval = juce::jmax (1, (int) std::lround (frameRate / 50.0));
}

void SpectralDucker::setSettings (const Settings& s) noexcept
{
    const auto rangeChanged = s.lowFreqHz != settings.lowFreqHz
                           || s.highFreqHz != settings.highFreqHz
                           || s.tiltDbPerOct != settings.tiltDbPerOct
                           || s.freqSmooth != settings.freqSmooth;

    const auto timesChanged = s.attackMs != settings.attackMs
                           || s.releaseMs != settings.releaseMs
                           || s.timeSmooth != settings.timeSmooth;

    settings = s;
    settings.depthDb      = juce::jlimit (0.0f, 60.0f, settings.depthDb);
    settings.freqSmooth   = juce::jlimit (0.0f, 1.0f, settings.freqSmooth);
    settings.timeSmooth   = juce::jlimit (0.0f, 1.0f, settings.timeSmooth);
    settings.preserveBody = juce::jlimit (0.0f, 1.0f, settings.preserveBody);
    settings.tonal        = juce::jlimit (0.0f, 1.0f, settings.tonal);
    settings.guard        = juce::jlimit (0.0f, 1.0f, settings.guard);
    settings.amount       = juce::jlimit (0.0f, 1.0f, settings.amount);

    if (rangeChanged)
        rebuildWeights();

    if (timesChanged)
        updateFrameCoefficients();
}

void SpectralDucker::boxSmooth (float* data, int bins, int radius) noexcept
{
    if (radius <= 0 || bins <= 1)
        return;

    radius = juce::jmin (radius, bins - 1);

    double running = 0.0;
    const auto width = 2 * radius + 1;

    // Prime the window with the clamped left edge.
    for (int k = -radius; k <= radius; ++k)
        running += (double) data[(size_t) juce::jlimit (0, bins - 1, k)];

    for (int k = 0; k < bins; ++k)
    {
        smoothScratch[(size_t) k] = (float) (running / (double) width);

        const auto outgoing = data[(size_t) juce::jlimit (0, bins - 1, k - radius)];
        const auto incoming = data[(size_t) juce::jlimit (0, bins - 1, k + radius + 1)];
        running += (double) incoming - (double) outgoing;
    }

    for (int k = 0; k < bins; ++k)
        data[(size_t) k] = smoothScratch[(size_t) k];
}

void SpectralDucker::processFrame (int numChannels) noexcept
{
    auto& fft = *ffts[(size_t) currentOrder];

    // --- sidechain analysis --------------------------------------------------
    scScratch.fill (0.0f);

    for (int i = 0; i < fftSize; ++i)
        scScratch[(size_t) i] = scFifo[(size_t) i] * window[(size_t) i];

    fft.performRealOnlyForwardTransform (scScratch.data());

    float scPeak = 0.0f;

    for (int k = 0; k < numBins; ++k)
    {
        const auto re = scScratch[(size_t) (2 * k)];
        const auto im = scScratch[(size_t) (2 * k + 1)];
        const auto mag = std::sqrt (re * re + im * im);

        scMag[(size_t) k] = std::isfinite (mag) ? mag : 0.0f;
        scPeak = juce::jmax (scPeak, scMag[(size_t) k]);

        scMagSlow[(size_t) k] = flushDenormal (scMag[(size_t) k]
                                               + scSlowCoeff * (scMagSlow[(size_t) k] - scMag[(size_t) k]));
    }

    const auto floorLevel = juce::jmax (kAbsoluteFloor, scPeak * kRelativeFloor);
    const auto floorSq = floorLevel * floorLevel;
    const auto preserveExponent = 1.0f + 3.0f * settings.preserveBody;
    const auto depth = settings.depthDb * settings.amount;
    const auto minGain = juce::Decibels::decibelsToGain (-depth, -80.0f);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        // --- destination analysis --------------------------------------------
        fftScratch.fill (0.0f);

        for (int i = 0; i < fftSize; ++i)
            fftScratch[(size_t) i] = inFifo[(size_t) ch][(size_t) i] * window[(size_t) i];

        fft.performRealOnlyForwardTransform (fftScratch.data());

        // --- collision map ----------------------------------------------------
        for (int k = 0; k < numBins; ++k)
        {
            const auto re = fftScratch[(size_t) (2 * k)];
            const auto im = fftScratch[(size_t) (2 * k + 1)];
            const auto main = std::sqrt (re * re + im * im);

            mainMag[(size_t) k] = std::isfinite (main) ? main : 0.0f;

            // Tonal / transient: which view of the sidechain drives the decision.
            const auto sideValue = blendMagnitude (scMagSlow[(size_t) k], scMag[(size_t) k], settings.tonal);

            // The sidechain's share of the combined energy in this bin: high only
            // where the two sources are actually competing.
            const auto share = sideValue / (sideValue + mainMag[(size_t) k] + kMagEpsilon);

            // ...gated by whether the sidechain is meaningfully present at all.
            const auto sq = sideValue * sideValue;
            const auto gate = sq / (sq + floorSq);

            auto c = share * gate * bandWeight[(size_t) k] * tiltWeight[(size_t) k];
            c = juce::jlimit (0.0f, 1.0f, c);

            // Preserve body: raising the map to a power > 1 leaves decisive
            // collisions untouched while relaxing marginal ones, so the
            // destination keeps its own fundamental instead of being thinned out.
            collision[(size_t) k] = std::pow (c, preserveExponent);
        }

        boxSmooth (collision.data(), numBins, freqSmoothRadius);

        // --- per-bin gain, with ballistics and artifact guard ------------------
        auto& gains = binGain[(size_t) ch];

        for (int k = 0; k < numBins; ++k)
        {
            const auto target = juce::jlimit (minGain, 1.0f,
                                              juce::Decibels::decibelsToGain (-depth * collision[(size_t) k],
                                                                              -80.0f));
            const auto coeff = (target < gains[(size_t) k]) ? attackCoeff : releaseCoeff;

            auto g = target + coeff * (gains[(size_t) k] - target);
            g = target + slowCoeff * (g - target);
            gains[(size_t) k] = flushDenormal (juce::jlimit (minGain, 1.0f, g));
        }

        {
            auto deepest = 1.0f;

            for (int k = 0; k < numBins; ++k)
                deepest = juce::jmin (deepest, gains[(size_t) k]);

            lastMinGain = (ch == 0) ? deepest : juce::jmin (lastMinGain, deepest);
        }

        if (settings.guard > 0.0f)
        {
            // Artifact guard: pull every bin toward its neighbourhood, which is what
            // stops isolated deep notches from turning into musical noise.
            // `collision` is reused as scratch here; it has already done its job.
            std::copy (gains.begin(), gains.begin() + numBins, collision.begin());
            boxSmooth (collision.data(), numBins, 2 + (int) std::lround (settings.guard * 6.0));

            const auto pull = settings.guard * 0.6f;

            for (int k = 0; k < numBins; ++k)
                gains[(size_t) k] = juce::jlimit (minGain, 1.0f,
                                                  gains[(size_t) k]
                                                      + (collision[(size_t) k] - gains[(size_t) k]) * pull);
        }

        // --- apply, keeping the spectrum conjugate-symmetric -------------------
        for (int k = 0; k < numBins; ++k)
        {
            const auto g = gains[(size_t) k];

            fftScratch[(size_t) (2 * k)]     *= g;
            fftScratch[(size_t) (2 * k + 1)] *= g;

            if (k > 0 && k < fftSize / 2)
            {
                const auto mirror = fftSize - k;
                fftScratch[(size_t) (2 * mirror)]     *= g;
                fftScratch[(size_t) (2 * mirror + 1)] *= g;
            }
        }

        fft.performRealOnlyInverseTransform (fftScratch.data());

        // --- overlap-add ------------------------------------------------------
        auto& acc = accum[(size_t) ch];

        for (int i = 0; i < fftSize; ++i)
            acc[(size_t) i] += fftScratch[(size_t) i] * window[(size_t) i] * windowNorm;

        for (int i = 0; i < hopSize; ++i)
            outFifo[(size_t) ch][(size_t) i] = flushDenormal (sanitise (acc[(size_t) i]));

        std::memmove (acc.data(), acc.data() + hopSize, (size_t) (fftSize - hopSize) * sizeof (float));
        std::fill (acc.begin() + (fftSize - hopSize), acc.begin() + fftSize, 0.0f);
    }

    // --- housekeeping --------------------------------------------------------
    std::memmove (scFifo.data(), scFifo.data() + hopSize,
                  (size_t) (fftSize - hopSize) * sizeof (float));

    for (int ch = 0; ch < numChannels; ++ch)
        std::memmove (inFifo[(size_t) ch].data(), inFifo[(size_t) ch].data() + hopSize,
                      (size_t) (fftSize - hopSize) * sizeof (float));

    if (++framesSincePublish >= publishInterval)
    {
        framesSincePublish = 0;
        publishSnapshot (numChannels);
    }
}

void SpectralDucker::publishSnapshot (int numChannels) noexcept
{
    juce::ignoreUnused (numChannels);

    snapshots.write ([this] (SpectralSnapshot& s)
    {
        s.numBins = numBins;
        s.fftSize = fftSize;
        s.sampleRate = (float) sr;

        const auto scale = 2.0f / (float) fftSize;

        for (int k = 0; k < numBins; ++k)
        {
            s.mainDb[(size_t) k]    = juce::Decibels::gainToDecibels (mainMag[(size_t) k] * scale, -100.0f);
            s.sideDb[(size_t) k]    = juce::Decibels::gainToDecibels (scMag[(size_t) k] * scale, -100.0f);
            s.collision[(size_t) k] = collision[(size_t) k];
            s.gainDb[(size_t) k]    = juce::Decibels::gainToDecibels (binGain[0][(size_t) k], -80.0f);
        }
    });
}

void SpectralDucker::process (juce::AudioBuffer<float>& audio,
                              const juce::AudioBuffer<float>& sidechain,
                              int numChannels, int numSamples) noexcept
{
    applyPendingConfig();

    const auto channels = juce::jlimit (1, juce::jmin (preparedChannels, audio.getNumChannels()),
                                        numChannels);
    numSamples = juce::jmin (numSamples, audio.getNumSamples());

    if (numSamples <= 0)
        return;

    const auto latency = fftSize - hopSize;   // the rover's read offset, not the reported latency
    const auto scChannels = juce::jmax (0, sidechain.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        // Mono-sum the sidechain: the collision map asks "is this frequency region
        // occupied", which does not need per-channel resolution and halves the
        // number of transforms.
        float scSample = 0.0f;

        if (scChannels > 0)
        {
            for (int ch = 0; ch < scChannels; ++ch)
                scSample += sidechain.getReadPointer (ch)[i];

            scSample /= (float) scChannels;
        }

        scFifo[(size_t) rover] = sanitise (scSample);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = audio.getWritePointer (ch);
            inFifo[(size_t) ch][(size_t) rover] = sanitise (data[i]);
            data[i] = outFifo[(size_t) ch][(size_t) (rover - latency)];
        }

        if (++rover >= fftSize)
        {
            rover = latency;
            processFrame (channels);
        }
    }
}

void SpectralDucker::processBypassed (juce::AudioBuffer<float>& audio, int numChannels,
                                      int numSamples) noexcept
{
    juce::ignoreUnused (audio, numChannels, numSamples);
    // Nothing to do: when the stage is out of the path the processor also removes
    // its latency, so there is no delay line left to keep aligned.
}
} // namespace sf
