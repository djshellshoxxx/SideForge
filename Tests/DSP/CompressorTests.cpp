#include "../TestHelpers.h"
#include "DSP/CompressorEngine.h"

using namespace sft;

namespace
{
/** Runs the engine against a constant detector magnitude and returns the gain
    reduction, in positive dB, after `numSamples`. */
float reductionAfter (sf::CompressorEngine& comp, float magnitude, int numSamples,
                      double sampleRate = 48000.0)
{
    const int block = 64;
    juce::AudioBuffer<float> envelope { 2, block }, gain { 2, block };

    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (envelope.getWritePointer (ch), magnitude, block);

    juce::ignoreUnused (sampleRate);

    int done = 0;
    float last = 1.0f;

    while (done < numSamples)
    {
        const auto n = juce::jmin (block, numSamples - done);
        comp.process (envelope, gain, 2, n);
        last = gain.getReadPointer (0)[n - 1];
        done += n;
    }

    return -juce::Decibels::gainToDecibels (last, -100.0f);
}

sf::CompressorEngine makeCompressor (const sf::CompressorEngine::Settings& s,
                                     double sampleRate = 48000.0)
{
    sf::CompressorEngine comp;
    comp.prepare (sampleRate, 512, 2);
    comp.setSettings (s);
    return comp;
}
} // namespace

SF_TEST (comp_staticCurveBelowThresholdIsUnity)
{
    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 0.0f;

    auto comp = makeCompressor (s);

    SF_CHECK_NEAR (comp.computeGainReductionDb (-40.0f), 0.0, 1.0e-5);
    SF_CHECK_NEAR (comp.computeGainReductionDb (-20.0f), 0.0, 1.0e-5);
}

SF_TEST (comp_staticCurveAboveThresholdMatchesRatio)
{
    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 0.0f;
    s.rangeDb = 60.0f;

    auto comp = makeCompressor (s);

    // 0 dB in, threshold -20, ratio 4:1  ->  out = -20 + 20/4 = -15, so -15 dB of GR.
    SF_CHECK_NEAR (comp.computeGainReductionDb (0.0f), -15.0, 1.0e-4);
    SF_CHECK_NEAR (comp.computeGainReductionDb (-10.0f), -7.5, 1.0e-4);
}

SF_TEST (comp_softKneeIsContinuousAndTangent)
{
    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 6.0f;
    s.rangeDb = 60.0f;

    auto comp = makeCompressor (s);

    // Below the knee: untouched. Above it: exactly the hard-knee line.
    SF_CHECK_NEAR (comp.computeGainReductionDb (-23.0f), 0.0, 1.0e-4);
    SF_CHECK_NEAR (comp.computeGainReductionDb (-17.0f), -2.25, 1.0e-3);
    SF_CHECK_NEAR (comp.computeGainReductionDb (-20.0f), -0.5625, 1.0e-3);

    // ...and continuous everywhere across the knee.
    float previous = comp.computeGainReductionDb (-30.0f);

    for (float db = -30.0f; db <= 0.0f; db += 0.05f)
    {
        const auto v = comp.computeGainReductionDb (db);
        SF_CHECK_FINITE (v);
        SF_CHECK_MSG (v <= previous + 1.0e-3f, "static curve must be monotonically decreasing");
        SF_CHECK_MSG (std::abs (v - previous) < 0.2f, "static curve has a step discontinuity");
        previous = v;
    }
}

SF_TEST (comp_infiniteRatioActsAsLimiter)
{
    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 40.0f;
    s.kneeDb = 0.0f;
    s.rangeDb = 60.0f;

    auto comp = makeCompressor (s);

    // Everything above the threshold is pinned to the threshold.
    SF_CHECK_NEAR (comp.computeGainReductionDb (0.0f), -20.0, 1.0e-4);
    SF_CHECK_NEAR (comp.computeGainReductionDb (-10.0f), -10.0, 1.0e-4);
}

SF_TEST (comp_attackReaches63PercentInOneTimeConstant)
{
    constexpr double sr = 48000.0;
    constexpr float attackMs = 10.0f;

    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 0.0f;
    s.attackMs = attackMs;
    s.releaseMs = 1000.0f;
    s.rangeDb = 60.0f;
    s.holdMs = 0.0f;

    auto comp = makeCompressor (s, sr);

    const auto samples = (int) std::lround (attackMs * 0.001 * sr);
    const auto target = 15.0;   // positive dB of GR the static curve asks for

    const auto afterOneTau = (double) reductionAfter (comp, 1.0f, samples, sr);
    SF_CHECK_NEAR (afterOneTau, target * 0.632, 0.6);

    // Four time constants gets essentially all the way there.
    const auto afterFourTau = (double) reductionAfter (comp, 1.0f, samples * 3, sr);
    SF_CHECK_NEAR (afterFourTau, target, 0.4);
}

SF_TEST (comp_releaseReturnsTowardsUnity)
{
    constexpr double sr = 48000.0;
    constexpr float releaseMs = 50.0f;

    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 0.0f;
    s.attackMs = 0.1f;
    s.releaseMs = releaseMs;
    s.rangeDb = 60.0f;

    auto comp = makeCompressor (s, sr);

    reductionAfter (comp, 1.0f, (int) (0.1 * sr), sr);          // settle fully compressed
    const auto settled = reductionAfter (comp, 1.0f, 16, sr);
    SF_CHECK_NEAR (settled, 15.0, 0.3);

    // Drop the detector to silence: after one time constant ~63 % has recovered.
    const auto tau = (int) std::lround (releaseMs * 0.001 * sr);
    const auto afterOneTau = reductionAfter (comp, 0.0f, tau, sr);
    SF_CHECK_NEAR (afterOneTau, 15.0 * (1.0 - 0.632), 0.7);

    const auto afterLong = reductionAfter (comp, 0.0f, tau * 8, sr);
    SF_CHECK_MSG (afterLong < 0.15f, "release should return to unity gain");
}

SF_TEST (comp_holdDelaysTheRelease)
{
    constexpr double sr = 48000.0;

    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.kneeDb = 0.0f;
    s.attackMs = 0.1f;
    s.releaseMs = 20.0f;
    s.holdMs = 50.0f;
    s.rangeDb = 60.0f;

    auto comp = makeCompressor (s, sr);

    reductionAfter (comp, 1.0f, (int) (0.1 * sr), sr);

    // Still inside the hold window: the reduction must not have moved.
    const auto duringHold = reductionAfter (comp, 0.0f, (int) (0.03 * sr), sr);
    SF_CHECK_NEAR (duringHold, 15.0, 0.2);

    // Past it, release proceeds normally.
    const auto afterHold = reductionAfter (comp, 0.0f, (int) (0.2 * sr), sr);
    SF_CHECK_MSG (afterHold < 1.0f, "release must proceed once the hold expires");
}

SF_TEST (comp_rangeClampsMaximumReduction)
{
    for (const float range : { 0.0f, 3.0f, 6.0f, 24.0f })
    {
        sf::CompressorEngine::Settings s;
        s.thresholdDb = -40.0f;
        s.ratio = 40.0f;
        s.kneeDb = 0.0f;
        s.attackMs = 0.05f;
        s.releaseMs = 10.0f;
        s.rangeDb = range;

        auto comp = makeCompressor (s);

        const auto reduction = reductionAfter (comp, 1.0f, 48000, 48000.0);

        SF_CHECK_FINITE (reduction);
        SF_CHECK_MSG (reduction <= range + 0.05f,
                      ("range " + std::to_string (range) + " exceeded: " + std::to_string (reduction)).c_str());

        if (range > 0.5f)
            SF_CHECK_NEAR (reduction, range, 0.2);
    }
}

SF_TEST (comp_autoReleaseStaysFiniteAndBounded)
{
    constexpr double sr = 48000.0;

    sf::CompressorEngine::Settings s;
    s.thresholdDb = -30.0f;
    s.ratio = 6.0f;
    s.attackMs = 1.0f;
    s.releaseMs = 100.0f;
    s.rangeDb = 40.0f;
    s.autoRelease = true;

    auto comp = makeCompressor (s, sr);

    const int block = 128;
    juce::AudioBuffer<float> envelope { 2, block }, gain { 2, block };
    std::mt19937 rng { 7 };
    std::uniform_real_distribution<float> dist { 0.0f, 1.2f };

    for (int b = 0; b < 400; ++b)
    {
        const auto level = dist (rng);

        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (envelope.getWritePointer (ch), level, block);

        comp.process (envelope, gain, 2, block);

        for (int i = 0; i < block; ++i)
        {
            const auto g = gain.getReadPointer (0)[i];
            SF_CHECK_FINITE (g);
            SF_CHECK (g > 0.0f && g <= 1.0f);
        }
    }
}

SF_TEST (comp_nonFiniteDetectorInputCannotPoisonTheGain)
{
    sf::CompressorEngine::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    auto comp = makeCompressor (s);

    const int block = 64;
    juce::AudioBuffer<float> envelope { 2, block }, gain { 2, block };

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = envelope.getWritePointer (ch);

        for (int i = 0; i < block; ++i)
            d[i] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN()
                 : (i % 3 == 1) ? std::numeric_limits<float>::infinity()
                                : 0.5f;
    }

    comp.process (envelope, gain, 2, block);

    for (int i = 0; i < block; ++i)
    {
        SF_CHECK_FINITE (gain.getReadPointer (0)[i]);
        SF_CHECK (gain.getReadPointer (0)[i] > 0.0f);
    }
}

SF_TEST (comp_autoMakeupNeverAttenuates)
{
    for (const float threshold : { -60.0f, -30.0f, -12.0f, 0.0f })
    {
        for (const float ratio : { 1.0f, 2.0f, 8.0f, 40.0f })
        {
            sf::CompressorEngine::Settings s;
            s.thresholdDb = threshold;
            s.ratio = ratio;
            auto comp = makeCompressor (s);

            const auto makeup = comp.computeAutoMakeupGain();
            SF_CHECK_FINITE (makeup);
            SF_CHECK (makeup >= 1.0f);
            SF_CHECK (makeup < 100.0f);
        }
    }
}
