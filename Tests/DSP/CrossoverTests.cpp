#include "../TestHelpers.h"
#include "DSP/LinkwitzRileyCrossover.h"

using namespace sft;

namespace
{
using Crossover = sf::LinkwitzRileyCrossover<2>;
using BandSet = std::array<juce::AudioBuffer<float>, sf::kMaxMatrixBands>;

BandSet makeBands (int numChannels, int numSamples)
{
    BandSet bands;

    for (auto& band : bands)
    {
        band.setSize (numChannels, numSamples);
        band.clear();
    }

    return bands;
}

/** Splits and immediately re-sums, which is what the Band Matrix does when every
    band is unprocessed. */
void splitAndSum (Crossover& crossover, const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& output, BandSet& bands,
                  int numChannels, int numSamples)
{
    crossover.updateSmoothing (numSamples);
    crossover.process (input, bands, numChannels, numSamples);

    output.clear();

    for (int b = 0; b < crossover.getNumBands(); ++b)
        for (int ch = 0; ch < numChannels; ++ch)
            output.addFrom (ch, 0, bands[(size_t) b], ch, 0, numSamples);
}

/** Steady-state level of the re-summed output at one frequency, relative to the
    input, in dB. RMS rather than peak, because at rational frequency ratios a
    sine is only sampled at a handful of phases. */
double reconstructionAtFrequency (int numBands, double freq, double sampleRate,
                                  int numChannels = 2, int block = 512)
{
    Crossover crossover;
    crossover.prepare (sampleRate, block, numChannels);
    crossover.setNumBands (numBands);

    const float defaults[3] = { 120.0f, 2500.0f, 8000.0f };
    crossover.setCrossovers (defaults, 3);
    crossover.updateSmoothing (block);

    juce::AudioBuffer<float> input { numChannels, block }, output { numChannels, block };
    auto bands = makeBands (numChannels, block);

    const int settle = (int) (0.4 * sampleRate);
    const int measure = (int) (0.25 * sampleRate);

    double phase = 0.0;
    const auto inc = 2.0 * kPi * freq / sampleRate;
    double inSum = 0.0, outSum = 0.0;
    int produced = 0;

    while (produced < settle + measure)
    {
        for (int i = 0; i < block; ++i)
        {
            const auto v = (float) std::sin (phase);
            phase += inc;

            for (int ch = 0; ch < numChannels; ++ch)
                input.getWritePointer (ch)[i] = v;
        }

        splitAndSum (crossover, input, output, bands, numChannels, block);

        if (produced >= settle)
        {
            for (int i = 0; i < block; ++i)
            {
                const auto in = (double) input.getReadPointer (0)[i];
                const auto out = (double) output.getReadPointer (0)[i];
                inSum += in * in;
                outSum += out * out;
            }
        }

        produced += block;
    }

    return 10.0 * std::log10 (juce::jmax (1.0e-12, outSum) / juce::jmax (1.0e-12, inSum));
}
} // namespace

// ---------------------------------------------------------------------------
//  Reconstruction
// ---------------------------------------------------------------------------

SF_TEST (crossover_oneBandIsBitTransparent)
{
    constexpr int block = 512;
    Crossover crossover;
    crossover.prepare (48000.0, block, 2);
    crossover.setNumBands (1);

    juce::AudioBuffer<float> input { 2, block }, output { 2, block };
    auto bands = makeBands (2, block);

    fillNoise (input, 0, 0.7f, 3);
    fillNoise (input, 1, 0.7f, 4);

    splitAndSum (crossover, input, output, bands, 2, block);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < block; ++i)
            SF_CHECK_NEAR (output.getReadPointer (ch)[i], input.getReadPointer (ch)[i], 1.0e-6);
}

SF_TEST (crossover_reconstructsToUnityAtEveryBandCount)
{
    constexpr double sr = 48000.0;

    // Right across the audible range, and deliberately sitting on every default
    // crossover point, where a broken allpass correction shows up first.
    const double probes[] = { 25.0, 40.0, 80.0, 120.0, 200.0, 400.0, 800.0,
                              1500.0, 2500.0, 4000.0, 8000.0, 12000.0, 16000.0 };

    for (int numBands = 1; numBands <= sf::kMaxMatrixBands; ++numBands)
    {
        for (const auto freq : probes)
        {
            const auto responseDb = reconstructionAtFrequency (numBands, freq, sr);

            SF_CHECK_FINITE (responseDb);
            SF_CHECK_MSG (std::abs (responseDb) < 0.5,
                          (std::to_string (numBands) + "-band reconstruction at "
                           + std::to_string ((int) freq) + " Hz was " + num (responseDb)
                           + " dB").c_str());
        }
    }
}

SF_TEST (crossover_reconstructsAtEverySampleRate)
{
    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (const double freq : { 60.0, 500.0, 5000.0 })
        {
            const auto responseDb = reconstructionAtFrequency (4, freq, sr);

            SF_CHECK_MSG (std::abs (responseDb) < 0.5,
                          (num (sr) + " Hz, probe " + std::to_string ((int) freq) + " Hz gave "
                           + num (responseDb) + " dB").c_str());
        }
    }
}

SF_TEST (crossover_reconstructionIsIndependentOfBlockSize)
{
    constexpr double sr = 48000.0;

    for (const int block : { 16, 64, 128, 480, 512, 2048 })
    {
        const auto responseDb = reconstructionAtFrequency (3, 300.0, sr, 2, block);

        SF_CHECK_MSG (std::abs (responseDb) < 0.5,
                      ("block " + std::to_string (block) + " gave " + num (responseDb) + " dB").c_str());
    }
}

SF_TEST (crossover_noiseEnergyIsPreservedThroughTheSplit)
{
    constexpr double sr = 48000.0;
    constexpr int block = 1024;

    for (int numBands = 2; numBands <= sf::kMaxMatrixBands; ++numBands)
    {
        Crossover crossover;
        crossover.prepare (sr, block, 2);
        crossover.setNumBands (numBands);

        const float defaults[3] = { 120.0f, 2500.0f, 8000.0f };
        crossover.setCrossovers (defaults, 3);
        crossover.updateSmoothing (block);

        juce::AudioBuffer<float> input { 2, block }, output { 2, block };
        auto bands = makeBands (2, block);

        double inSum = 0.0, outSum = 0.0;

        for (int b = 0; b < 48; ++b)
        {
            fillNoise (input, 0, 0.5f, (unsigned) (b * 2));
            fillNoise (input, 1, 0.5f, (unsigned) (b * 2 + 1));

            splitAndSum (crossover, input, output, bands, 2, block);

            if (b >= 8)   // let the filters settle
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    for (int i = 0; i < block; ++i)
                    {
                        const auto in = (double) input.getReadPointer (ch)[i];
                        const auto out = (double) output.getReadPointer (ch)[i];
                        inSum += in * in;
                        outSum += out * out;
                        SF_CHECK_FINITE (out);
                    }
                }
            }
        }

        const auto ratioDb = 10.0 * std::log10 (outSum / juce::jmax (1.0e-12, inSum));

        SF_CHECK_MSG (std::abs (ratioDb) < 0.3,
                      (std::to_string (numBands) + " bands changed broadband noise energy by "
                       + num (ratioDb) + " dB").c_str());
    }
}

SF_TEST (crossover_impulseReconstructsWithoutRingingAway)
{
    constexpr double sr = 48000.0;
    constexpr int block = 4096;

    Crossover crossover;
    crossover.prepare (sr, block, 1);
    crossover.setNumBands (4);

    const float defaults[3] = { 120.0f, 2500.0f, 8000.0f };
    crossover.setCrossovers (defaults, 3);
    crossover.updateSmoothing (block);

    juce::AudioBuffer<float> input { 1, block }, output { 1, block };
    auto bands = makeBands (1, block);

    input.clear();
    input.getWritePointer (0)[0] = 1.0f;

    splitAndSum (crossover, input, output, bands, 1, block);

    // An allpass network spreads an impulse in time but must preserve its energy.
    double energy = 0.0;

    for (int i = 0; i < block; ++i)
    {
        const auto v = (double) output.getReadPointer (0)[i];
        SF_CHECK_FINITE (v);
        energy += v * v;
    }

    SF_CHECK_MSG (std::abs (10.0 * std::log10 (juce::jmax (1.0e-12, energy))) < 0.5,
                  ("impulse energy through the split was " + num (10.0 * std::log10 (energy))
                   + " dB").c_str());
}

// ---------------------------------------------------------------------------
//  Ordering, spacing and movement
// ---------------------------------------------------------------------------

SF_TEST (crossover_enforcesOrderAndMinimumSpacing)
{
    constexpr int block = 512;
    Crossover crossover;
    crossover.prepare (48000.0, block, 2);
    crossover.setNumBands (4);

    // Deliberately out of order, coincident and out of range.
    const float nonsense[3] = { 9000.0f, 50.0f, 60.0f };
    crossover.setCrossovers (nonsense, 3);

    for (int i = 0; i < 400; ++i)
        crossover.updateSmoothing (block);

    const auto first = crossover.getCrossoverHz (0);
    const auto second = crossover.getCrossoverHz (1);
    const auto third = crossover.getCrossoverHz (2);

    SF_CHECK_MSG (first < second && second < third, "crossovers must come out ordered");
    SF_CHECK_MSG (second >= first * Crossover::kMinSpacing * 0.999f,
                  "crossovers must keep a minimum spacing");
    SF_CHECK_MSG (third >= second * Crossover::kMinSpacing * 0.999f,
                  "crossovers must keep a minimum spacing");

    // ...and nothing may sit above Nyquist.
    SF_CHECK (third < 48000.0f * 0.5f);
}

SF_TEST (crossover_rejectsNonFiniteTargets)
{
    constexpr int block = 512;
    Crossover crossover;
    crossover.prepare (48000.0, block, 2);
    crossover.setNumBands (4);

    const float nonsense[3] = { std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity() };
    crossover.setCrossovers (nonsense, 3);

    for (int i = 0; i < 100; ++i)
        crossover.updateSmoothing (block);

    for (int i = 0; i < sf::kMaxCrossovers; ++i)
    {
        SF_CHECK_FINITE (crossover.getCrossoverHz (i));
        SF_CHECK (crossover.getCrossoverHz (i) > 0.0f);
    }
}

SF_TEST (crossover_movementIsSmoothedRatherThanStepped)
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    Crossover crossover;
    crossover.prepare (sr, block, 1);
    crossover.setNumBands (2);

    float target[3] = { 200.0f, 2500.0f, 8000.0f };
    crossover.setCrossovers (target, 3);

    for (int i = 0; i < 200; ++i)
        crossover.updateSmoothing (block);

    SF_CHECK_NEAR (crossover.getCrossoverHz (0), 200.0, 1.0);

    // Slam it an octave and a half away in one go.
    target[0] = 2000.0f;
    crossover.setCrossovers (target, 3);

    SF_CHECK_MSG (crossover.isSmoothing(), "a new target should start a glide, not a jump");

    // The rate limit gives an exact bound on how far one block can move the
    // cutoff, so the test can assert the guarantee rather than a guess.
    const auto bound = crossover.maxRatioPerBlock (block);
    SF_CHECK_MSG (bound < 1.10f,
                  ("a block should not be able to move the cutoff by " + num (bound)).c_str());

    auto previous = crossover.getCrossoverHz (0);
    auto blocks = 0;

    while (crossover.isSmoothing() && blocks < 5000)
    {
        crossover.updateSmoothing (block);
        const auto now = crossover.getCrossoverHz (0);

        SF_CHECK_MSG (now / previous <= bound * 1.001f,
                      ("the cutoff moved by a factor of " + num (now / previous)
                       + " in one block, past the " + num (bound) + " limit").c_str());

        previous = now;
        ++blocks;
    }

    SF_CHECK_NEAR (crossover.getCrossoverHz (0), 2000.0, 5.0);
    SF_CHECK_MSG (blocks > 3, "the glide should take more than a couple of blocks");
}

SF_TEST (crossover_rapidMovementStaysFiniteAndBounded)
{
    constexpr double sr = 48000.0;
    constexpr int block = 128;

    Crossover crossover;
    crossover.prepare (sr, block, 2);
    crossover.setNumBands (4);

    juce::AudioBuffer<float> input { 2, block }, output { 2, block };
    auto bands = makeBands (2, block);

    std::mt19937 rng { 8080 };
    std::uniform_real_distribution<float> lowDist { 30.0f, 900.0f };
    std::uniform_real_distribution<float> midDist { 900.0f, 6000.0f };
    std::uniform_real_distribution<float> highDist { 6000.0f, 18000.0f };

    auto worstPeak = 0.0f;

    for (int b = 0; b < 800; ++b)
    {
        // A new crossover position every single block, which is far faster than
        // any host would automate one.
        const float target[3] = { lowDist (rng), midDist (rng), highDist (rng) };
        crossover.setCrossovers (target, 3);

        fillNoise (input, 0, 0.5f, (unsigned) b);
        fillNoise (input, 1, 0.5f, (unsigned) (b + 5000));

        splitAndSum (crossover, input, output, bands, 2, block);

        SF_CHECK_MSG (allFinite (output), "rapid crossover movement produced a non-finite sample");

        for (int ch = 0; ch < 2; ++ch)
            worstPeak = juce::jmax (worstPeak, output.getMagnitude (ch, 0, block));
    }

    SF_CHECK_MSG (worstPeak < 4.0f,
                  ("rapid crossover movement produced a peak of " + num (worstPeak)).c_str());
}

SF_TEST (crossover_silenceStaysSilent)
{
    constexpr int block = 512;

    for (int numBands = 1; numBands <= sf::kMaxMatrixBands; ++numBands)
    {
        Crossover crossover;
        crossover.prepare (48000.0, block, 2);
        crossover.setNumBands (numBands);

        juce::AudioBuffer<float> input { 2, block }, output { 2, block };
        auto bands = makeBands (2, block);
        input.clear();

        for (int b = 0; b < 20; ++b)
        {
            splitAndSum (crossover, input, output, bands, 2, block);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    SF_CHECK (std::abs (output.getReadPointer (ch)[i]) < 1.0e-9f);
        }
    }
}

SF_TEST (crossover_bandsActuallySplitTheSpectrum)
{
    constexpr double sr = 48000.0;
    constexpr int block = 1024;

    Crossover crossover;
    crossover.prepare (sr, block, 1);
    crossover.setNumBands (3);

    const float defaults[3] = { 120.0f, 2500.0f, 8000.0f };
    crossover.setCrossovers (defaults, 3);
    crossover.updateSmoothing (block);

    juce::AudioBuffer<float> input { 1, block };
    auto bands = makeBands (1, block);

    // A tone well inside each band should dominate that band and be strongly
    // rejected by the others.
    struct Probe { double hz; int expectedBand; };

    for (const auto& probe : { Probe { 40.0, 0 }, Probe { 700.0, 1 }, Probe { 10000.0, 2 } })
    {
        crossover.reset();

        double phase = 0.0;
        const auto inc = 2.0 * kPi * probe.hz / sr;
        float energy[sf::kMaxMatrixBands] { 0.0f, 0.0f, 0.0f, 0.0f };

        for (int b = 0; b < 24; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                input.getWritePointer (0)[i] = (float) std::sin (phase);
                phase += inc;
            }

            crossover.updateSmoothing (block);
            crossover.process (input, bands, 1, block);

            if (b >= 12)
                for (int band = 0; band < 3; ++band)
                    energy[band] = juce::jmax (energy[band], bands[(size_t) band].getMagnitude (0, 0, block));
        }

        const auto wanted = energy[probe.expectedBand];
        SF_CHECK_MSG (wanted > 0.7f,
                      (std::to_string ((int) probe.hz) + " Hz should land in band "
                       + std::to_string (probe.expectedBand) + ", peak was " + num (wanted)).c_str());

        for (int band = 0; band < 3; ++band)
        {
            if (band == probe.expectedBand)
                continue;

            SF_CHECK_MSG (energy[band] < wanted * 0.2f,
                          (std::to_string ((int) probe.hz) + " Hz leaked into band "
                           + std::to_string (band) + " at " + num (energy[band])).c_str());
        }
    }
}
