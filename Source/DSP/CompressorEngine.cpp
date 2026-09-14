#include "CompressorEngine.h"
#include <cmath>

namespace sf
{
namespace
{
/** Ratios at and above this read as inf:1 in the UI, so treat them as a limiter. */
constexpr float kInfiniteRatio = 39.5f;
constexpr float kMinLevelDb    = -120.0f;
} // namespace

void CompressorEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (maxBlockSize);

    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedChannels = juce::jlimit (1, kMaxCh, numChannels);
    grAverageCoeff = timeConstantCoeff (200.0f, sr);

    updateCoefficients();
    reset();
}

void CompressorEngine::reset()
{
    for (int ch = 0; ch < kMaxCh; ++ch)
    {
        grDb[ch] = 0.0f;
        grAverage[ch] = 0.0f;
        holdCounter[ch] = 0;
    }

    lastMaxReductionDb = 0.0f;
}

void CompressorEngine::setSettings (const Settings& s) noexcept
{
    const auto timesChanged = s.attackMs != settings.attackMs
                           || s.releaseMs != settings.releaseMs
                           || s.holdMs != settings.holdMs;

    settings = s;
    settings.ratio     = juce::jmax (1.0f, settings.ratio);
    settings.kneeDb    = juce::jmax (0.0f, settings.kneeDb);
    settings.rangeDb   = juce::jlimit (0.0f, 80.0f, settings.rangeDb);
    settings.attackMs  = juce::jmax (0.0f, settings.attackMs);
    settings.holdMs    = juce::jmax (0.0f, settings.holdMs);
    settings.releaseMs = juce::jmax (0.1f, settings.releaseMs);

    if (timesChanged)
        updateCoefficients();
}

void CompressorEngine::updateCoefficients() noexcept
{
    attackCoeff  = timeConstantCoeff (settings.attackMs, sr);
    releaseCoeff = timeConstantCoeff (settings.releaseMs, sr);
    holdSamples  = (int) std::lround ((double) settings.holdMs * 0.001 * sr);
}

float CompressorEngine::computeGainReductionDb (float inputDb) const noexcept
{
    if (! std::isfinite (inputDb))
        return 0.0f;

    const auto threshold = settings.thresholdDb;
    const auto knee      = settings.kneeDb;
    const auto over      = inputDb - threshold;

    // 1/ratio, with the inf:1 end of the control treated as a hard limiter.
    const auto slope = settings.ratio >= kInfiniteRatio ? 0.0f : 1.0f / settings.ratio;

    float outDb;

    if (knee > 0.0f && std::abs (over) <= knee * 0.5f)
    {
        // Quadratic soft knee, tangent to both straight segments at its edges.
        const auto x = over + knee * 0.5f;
        outDb = inputDb + (slope - 1.0f) * x * x / (2.0f * knee);
    }
    else if (over <= 0.0f)
    {
        outDb = inputDb;
    }
    else
    {
        outDb = threshold + over * slope;
    }

    const auto reduction = outDb - inputDb;   // <= 0

    return juce::jlimit (-settings.rangeDb, 0.0f, reduction);
}

float CompressorEngine::releaseCoeffFor (float channelGrDb, int channel) const noexcept
{
    if (! settings.autoRelease)
        return releaseCoeff;

    // Programme-dependent release: a reduction that is deeper than this channel's
    // recent average is a transient event and recovers quickly; a reduction that
    // matches the average is sustained programme material and recovers slowly.
    const auto depth   = std::abs (channelGrDb);
    const auto average = grAverage[channel];
    const auto factor  = juce::jlimit (0.25f, 4.0f, depth / (average + 1.0f));

    return timeConstantCoeff (juce::jmax (0.1f, settings.releaseMs / factor), sr);
}

void CompressorEngine::process (const juce::AudioBuffer<float>& envelope,
                                juce::AudioBuffer<float>& gainOut,
                                int numChannels, int numSamples) noexcept
{
    const auto channels = juce::jlimit (1, juce::jmin (preparedChannels, gainOut.getNumChannels()),
                                        numChannels);
    numSamples = juce::jmin (numSamples, gainOut.getNumSamples());

    float deepest = 0.0f;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto envCh = juce::jmin (ch, envelope.getNumChannels() - 1);
        const auto* env  = envCh >= 0 ? envelope.getReadPointer (envCh) : nullptr;
        auto* out = gainOut.getWritePointer (ch);

        auto current = grDb[ch];
        auto average = grAverage[ch];
        auto hold    = holdCounter[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const auto magnitude = env != nullptr ? sanitise (env[i]) : 0.0f;
            const auto inputDb   = juce::Decibels::gainToDecibels (magnitude, kMinLevelDb);
            const auto target    = computeGainReductionDb (inputDb);

            if (target < current)
            {
                current = target + attackCoeff * (current - target);
                hold = holdSamples;
            }
            else if (hold > 0)
            {
                --hold;
            }
            else
            {
                const auto coeff = releaseCoeffFor (current, ch);
                current = target + coeff * (current - target);
            }

            current = flushDenormal (juce::jlimit (-settings.rangeDb, 0.0f, current));

            const auto depth = -current;
            average = flushDenormal (depth + grAverageCoeff * (average - depth));

            deepest = juce::jmax (deepest, depth);
            out[i] = juce::Decibels::decibelsToGain (current, -settings.rangeDb - 1.0f);
        }

        grDb[ch] = current;
        grAverage[ch] = average;
        holdCounter[ch] = hold;
    }

    // Channels the caller did not ask for still need defined contents.
    for (int ch = channels; ch < gainOut.getNumChannels(); ++ch)
        gainOut.copyFrom (ch, 0, gainOut, 0, 0, numSamples);

    lastMaxReductionDb = deepest;
}

float CompressorEngine::computeAutoMakeupGain() const noexcept
{
    // Compensate the loss the static curve imposes on a nominal -12 dBFS programme
    // level. That is a conservative reference: it never boosts more than the
    // compressor is actually taking away at a realistic mix level.
    constexpr float kReferenceDb = -12.0f;
    // Even an extreme setting should not be allowed to hand the output stage a
    // 40 dB boost; past this point the user wants the output trim, not auto gain.
    constexpr float kMaxMakeupDb = 24.0f;

    const auto reduction = computeGainReductionDb (kReferenceDb);   // <= 0
    const auto makeupDb = juce::jlimit (0.0f, kMaxMakeupDb, -reduction);

    return juce::jmax (1.0f, juce::Decibels::decibelsToGain (makeupDb));
}
} // namespace sf
