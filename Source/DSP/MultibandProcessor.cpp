#include "MultibandProcessor.h"

namespace sf
{
void MultibandProcessor::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedChannels = juce::jlimit (1, kMaxCh, numChannels);
    preparedSamples = juce::jmax (1, maxBlockSize);

    for (auto& b : bandBuffer)
        b.setSize (preparedChannels, preparedSamples, false, true, true);

    monoLowBuffer.setSize (preparedChannels, preparedSamples, false, true, true);

    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) preparedSamples,
                                        (juce::uint32) preparedChannels };

    crossoverLow.prepare (spec);
    crossoverHigh.prepare (spec);
    lowAllpass.prepare (spec);
    monoLowCrossover.prepare (spec);

    setSettings (settings);
    reset();
}

void MultibandProcessor::reset()
{
    crossoverLow.reset();
    crossoverHigh.reset();
    lowAllpass.reset();
    monoLowCrossover.reset();

    for (auto& b : bandBuffer)
        b.clear();

    monoLowBuffer.clear();

    for (int band = 0; band < kNumBands; ++band)
    {
        for (int ch = 0; ch < kMaxCh; ++ch)
            bandGainState[band][ch] = 1.0f;

        bandReductionDb[(size_t) band].store (0.0f, std::memory_order_relaxed);
    }

    lastMinGain = 1.0f;
    currentLink = settings.channelLink;

    for (int band = 0; band < kNumBands; ++band)
        currentDepth[band] = settings.bands[band].depth;
}

void MultibandProcessor::setSettings (const Settings& s)
{
    settings = s;
    settings.channelLink = juce::jlimit (0.0f, 1.0f, settings.channelLink);

    // Keep the crossovers ordered so band 2 can never be inverted or empty.
    settings.xover1Hz = juce::jlimit (20.0f, (float) (sr * 0.45), settings.xover1Hz);
    settings.xover2Hz = juce::jlimit (settings.xover1Hz * 1.1f, (float) (sr * 0.47), settings.xover2Hz);
    settings.monoLowHz = juce::jlimit (20.0f, (float) (sr * 0.45), settings.monoLowHz);

    for (auto& b : settings.bands)
    {
        b.depth = juce::jlimit (0.0f, 1.0f, b.depth);
        b.attackMs = juce::jmax (0.0f, b.attackMs);
        b.releaseMs = juce::jmax (0.0f, b.releaseMs);
    }

    crossoverLow.setCutoff (settings.xover1Hz, sr);
    crossoverHigh.setCutoff (settings.xover2Hz, sr);
    lowAllpass.setCutoff (settings.xover2Hz, sr);
    monoLowCrossover.setCutoff (settings.monoLowHz, sr);

    updateCoefficients();
}

void MultibandProcessor::updateCoefficients()
{
    for (int band = 0; band < kNumBands; ++band)
    {
        attackCoeff[band]  = timeConstantCoeff (settings.bands[band].attackMs, sr);
        releaseCoeff[band] = timeConstantCoeff (settings.bands[band].releaseMs, sr);
    }
}

void MultibandProcessor::process (juce::AudioBuffer<float>& audio,
                                  const juce::AudioBuffer<float>& gain,
                                  int numChannels, int numSamples) noexcept
{
    const auto channels = juce::jlimit (1, juce::jmin (preparedChannels, audio.getNumChannels()),
                                        numChannels);
    numSamples = juce::jmin (numSamples, juce::jmin (audio.getNumSamples(), preparedSamples));

    if (numSamples <= 0)
        return;

    const auto threeBand = (settings.bandMode == BandMode::Three);
    const auto numBands = threeBand ? kNumBands : 1;
    const auto midSide = (settings.channelMode == ChannelMode::MidSide) && channels >= 2;

    // --- channel matrix: encode ---------------------------------------------
    if (midSide)
    {
        auto* l = audio.getWritePointer (0);
        auto* r = audio.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto mid  = 0.5f * (l[i] + r[i]);
            const auto side = 0.5f * (l[i] - r[i]);
            l[i] = mid;
            r[i] = side;
        }
    }

    // --- band split ----------------------------------------------------------
    if (threeBand)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* src = audio.getReadPointer (ch);
            auto* b0 = bandBuffer[0].getWritePointer (ch);
            auto* b1 = bandBuffer[1].getWritePointer (ch);
            auto* b2 = bandBuffer[2].getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float low = 0.0f, high = 0.0f;
                crossoverLow.split (ch, sanitise (src[i]), low, high);

                float mid = 0.0f, top = 0.0f;
                crossoverHigh.split (ch, high, mid, top);

                // The low band skipped the second crossover, so it needs that
                // crossover's allpass to stay phase-aligned with the other two.
                b0[i] = lowAllpass.process (ch, low);
                b1[i] = mid;
                b2[i] = top;
            }
        }
    }
    else
    {
        // Sanitised on the way in, exactly as the three-band path does, so a bad
        // sample from upstream can never reach the ballistics or the meters.
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* src = audio.getReadPointer (ch);
            auto* dst = bandBuffer[0].getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
                dst[i] = sanitise (src[i]);
        }
    }

    // --- per-band gain, then recombine --------------------------------------
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::clear (audio.getWritePointer (ch), numSamples);

    float globalMinGain = 1.0f;
    float bandDeepest[kNumBands] { 1.0f, 1.0f, 1.0f };

    const auto inverseLength = numSamples > 0 ? 1.0f / (float) numSamples : 0.0f;
    const auto linkAtStart = currentLink;
    const auto linkStep = (settings.channelLink - linkAtStart) * inverseLength;

    for (int band = 0; band < numBands; ++band)
    {
        const auto& bandSettings = settings.bands[band];
        const auto attack = attackCoeff[band];
        const auto release = releaseCoeff[band];
        const auto depthAtStart = currentDepth[band];
        const auto depthStep = (bandSettings.depth - depthAtStart) * inverseLength;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* src = bandBuffer[band].getReadPointer (ch);
            auto* dst = audio.getWritePointer (ch);

            const auto gainCh = juce::jmin (ch, gain.getNumChannels() - 1);
            const auto* g = gainCh >= 0 ? gain.getReadPointer (gainCh) : nullptr;
            const auto* gOther = gain.getNumChannels() > 1
                                     ? gain.getReadPointer (gainCh == 0 ? 1 : 0)
                                     : g;

            auto state = bandGainState[band][ch];
            auto depth = depthAtStart;
            auto link  = linkAtStart;

            for (int i = 0; i < numSamples; ++i)
            {
                depth += depthStep;
                link  += linkStep;

                const auto own    = g != nullptr ? juce::jlimit (0.0f, 1.0f, g[i]) : 1.0f;
                const auto other  = gOther != nullptr ? juce::jlimit (0.0f, 1.0f, gOther[i]) : own;
                const auto linked = juce::jmin (own, other);   // deepest reduction of the pair

                float channelGain;

                if (midSide)
                {
                    // Mid always takes the full reduction; Channel Link decides how
                    // much of it the Side channel receives, which is what makes
                    // "duck the centre, keep the sides open" a single control.
                    channelGain = (ch == 0)
                                      ? linked
                                      : 1.0f - link * (1.0f - linked);
                }
                else if (settings.channelMode == ChannelMode::LeftRight)
                {
                    channelGain = own + (linked - own) * link;
                }
                else
                {
                    channelGain = linked;   // Stereo: always fully linked
                }

                auto target = bandSettings.enabled
                                  ? 1.0f - depth * (1.0f - channelGain)
                                  : 1.0f;
                target = juce::jlimit (0.0f, 1.0f, target);

                // Per-band ballistics on the applied gain.
                const auto coeff = (target < state) ? attack : release;
                state = flushDenormal (target + coeff * (state - target));
                state = juce::jlimit (0.0f, 1.0f, state);

                dst[i] += src[i] * state;

                bandDeepest[band] = juce::jmin (bandDeepest[band], state);
                globalMinGain = juce::jmin (globalMinGain, state);
            }

            bandGainState[band][ch] = state;
        }

        currentDepth[band] = bandSettings.depth;
    }

    currentLink = settings.channelLink;

    // --- channel matrix: decode ---------------------------------------------
    if (midSide)
    {
        auto* m = audio.getWritePointer (0);
        auto* s = audio.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto left  = m[i] + s[i];
            const auto right = m[i] - s[i];
            m[i] = left;
            s[i] = right;
        }
    }

    if (settings.monoLowEnable && channels >= 2)
        applyMonoLow (audio, channels, numSamples);

    for (int band = 0; band < kNumBands; ++band)
    {
        const auto reduction = band < numBands
                                   ? -juce::Decibels::gainToDecibels (bandDeepest[band], -80.0f)
                                   : 0.0f;
        bandReductionDb[(size_t) band].store (reduction, std::memory_order_relaxed);
    }

    lastMinGain = globalMinGain;
}

void MultibandProcessor::applyMonoLow (juce::AudioBuffer<float>& audio, int numChannels,
                                       int numSamples) noexcept
{
    juce::ignoreUnused (numChannels);

    auto* l = audio.getWritePointer (0);
    auto* r = audio.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        float lowL = 0.0f, highL = 0.0f, lowR = 0.0f, highR = 0.0f;
        monoLowCrossover.split (0, l[i], lowL, highL);
        monoLowCrossover.split (1, r[i], lowR, highR);

        const auto mono = 0.5f * (lowL + lowR);

        l[i] = mono + highL;
        r[i] = mono + highR;
    }
}
} // namespace sf
