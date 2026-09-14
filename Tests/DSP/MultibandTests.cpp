#include "../TestHelpers.h"
#include "DSP/MultibandProcessor.h"

using namespace sft;

namespace
{
sf::MultibandProcessor::Settings unityBands (sf::BandMode bandMode,
                                             sf::ChannelMode channelMode = sf::ChannelMode::Stereo)
{
    sf::MultibandProcessor::Settings s;
    s.bandMode = bandMode;
    s.channelMode = channelMode;
    s.xover1Hz = 180.0f;
    s.xover2Hz = 2500.0f;
    s.channelLink = 1.0f;

    for (auto& b : s.bands)
    {
        b.enabled = true;
        b.depth = 1.0f;
        b.attackMs = 0.0f;     // no ballistics, so "gain 1" really means "gain 1"
        b.releaseMs = 0.0f;
    }

    return s;
}

/** Feeds a steady sine through the processor with unity gain and returns the
    output level relative to the input, in dB, measured after the filters settle.

    RMS rather than peak: at rational ratios like 8 kHz / 48 kHz a sine is sampled
    at only a handful of distinct phases, so the largest *sample* can sit well
    below the true peak and would read as a crossover error that is not there. */
double responseAtFrequency (sf::MultibandProcessor& mb, double freq, double sampleRate,
                            int numChannels = 2)
{
    constexpr int block = 512;
    const int settle = (int) (0.5 * sampleRate);
    const int measure = (int) (0.2 * sampleRate);

    juce::AudioBuffer<float> audio { numChannels, block }, gain { numChannels, block };

    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 1.0f, block);

    double phase = 0.0;
    const auto inc = 2.0 * kPi * freq / sampleRate;
    double sumSq = 0.0;
    int counted = 0;
    int produced = 0;

    while (produced < settle + measure)
    {
        for (int i = 0; i < block; ++i)
        {
            const auto v = (float) std::sin (phase);
            phase += inc;

            for (int ch = 0; ch < numChannels; ++ch)
                audio.getWritePointer (ch)[i] = v;
        }

        mb.process (audio, gain, numChannels, block);

        if (produced >= settle)
            for (int i = 0; i < block; ++i)
            {
                const auto v = (double) audio.getReadPointer (0)[i];
                sumSq += v * v;
                ++counted;
            }

        produced += block;
    }

    const auto rms = std::sqrt (sumSq / juce::jmax (1, counted));
    const auto referenceRms = 1.0 / std::sqrt (2.0);   // a unit sine

    return 20.0 * std::log10 (juce::jmax (1.0e-9, rms / referenceRms));
}
} // namespace

SF_TEST (multiband_singleBandIsBitTransparentAtUnityGain)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    sf::MultibandProcessor mb;
    mb.prepare (sr, block, 2);
    mb.setSettings (unityBands (sf::BandMode::Single));

    juce::AudioBuffer<float> audio { 2, block }, original { 2, block }, gain { 2, block };
    fillNoise (audio, 0, 0.7f, 11);
    fillNoise (audio, 1, 0.7f, 22);

    for (int ch = 0; ch < 2; ++ch)
    {
        original.copyFrom (ch, 0, audio, ch, 0, block);
        juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 1.0f, block);
    }

    mb.process (audio, gain, 2, block);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < block; ++i)
            SF_CHECK_NEAR (audio.getReadPointer (ch)[i], original.getReadPointer (ch)[i], 1.0e-6);
}

SF_TEST (multiband_threeBandsRecombineToUnity)
{
    constexpr double sr = 48000.0;

    sf::MultibandProcessor mb;
    mb.prepare (sr, 512, 2);
    mb.setSettings (unityBands (sf::BandMode::Three));

    // Across the whole audible range, including both crossover points, the summed
    // bands must stay within half a dB of the input.
    for (const double freq : { 30.0, 60.0, 120.0, 180.0, 300.0, 700.0, 1500.0,
                               2500.0, 4000.0, 8000.0, 15000.0 })
    {
        sf::MultibandProcessor fresh;
        fresh.prepare (sr, 512, 2);
        fresh.setSettings (unityBands (sf::BandMode::Three));

        const auto responseDb = responseAtFrequency (fresh, freq, sr);

        SF_CHECK_FINITE (responseDb);
        SF_CHECK_MSG (std::abs (responseDb) < 0.5,
                      ("crossover response at " + std::to_string ((int) freq) + " Hz was "
                       + num (responseDb) + " dB").c_str());
    }

    juce::ignoreUnused (mb);
}

SF_TEST (multiband_threeBandRmsMatchesTheInputOnNoise)
{
    constexpr double sr = 48000.0;
    constexpr int block = 1024;
    constexpr int blocks = 64;

    sf::MultibandProcessor mb;
    mb.prepare (sr, block, 2);
    mb.setSettings (unityBands (sf::BandMode::Three));

    juce::AudioBuffer<float> audio { 2, block }, gain { 2, block };

    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 1.0f, block);

    double inSum = 0.0, outSum = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        fillNoise (audio, 0, 0.5f, (unsigned) (100 + b));
        fillNoise (audio, 1, 0.5f, (unsigned) (900 + b));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const auto v = audio.getReadPointer (ch)[i];
                inSum += (double) v * v;
            }

        mb.process (audio, gain, 2, block);

        if (b >= 4)   // let the filters settle
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const auto v = audio.getReadPointer (ch)[i];
                    SF_CHECK_FINITE (v);
                    outSum += (double) v * v;
                }
    }

    // Compare only the settled portion against the same proportion of the input.
    const auto inSettled = inSum * (double) (blocks - 4) / (double) blocks;
    const auto ratioDb = 10.0 * std::log10 (outSum / juce::jmax (1.0e-12, inSettled));

    SF_CHECK_MSG (std::abs (ratioDb) < 0.5,
                  ("summed band energy differed from the input by " + num (ratioDb) + " dB").c_str());
}

SF_TEST (multiband_midSideRoundTripIsExact)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    sf::MultibandProcessor mb;
    mb.prepare (sr, block, 2);
    mb.setSettings (unityBands (sf::BandMode::Single, sf::ChannelMode::MidSide));

    juce::AudioBuffer<float> audio { 2, block }, original { 2, block }, gain { 2, block };
    fillNoise (audio, 0, 0.6f, 5);
    fillNoise (audio, 1, 0.6f, 6);

    for (int ch = 0; ch < 2; ++ch)
    {
        original.copyFrom (ch, 0, audio, ch, 0, block);
        juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 1.0f, block);
    }

    mb.process (audio, gain, 2, block);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < block; ++i)
            SF_CHECK_NEAR (audio.getReadPointer (ch)[i], original.getReadPointer (ch)[i], 1.0e-5);
}

SF_TEST (multiband_midSideDucksTheCentreAndLeavesTheSidesAlone)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto settings = unityBands (sf::BandMode::Single, sf::ChannelMode::MidSide);
    settings.channelLink = 0.0f;   // ducking applies to Mid only

    sf::MultibandProcessor mb;
    mb.prepare (sr, block, 2);
    mb.setSettings (settings);

    // A pure centre signal and a pure side signal, processed with a 0.5 gain.
    juce::AudioBuffer<float> gain { 2, block };

    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 0.5f, block);

    juce::AudioBuffer<float> centre { 2, block };
    fillSine (centre, 0, 1000.0, sr, 0.5f);
    centre.copyFrom (1, 0, centre, 0, 0, block);          // L == R -> pure mid
    const auto centreBefore = maxAbs (centre, 0);
    mb.process (centre, gain, 2, block);
    SF_CHECK_NEAR (maxAbs (centre, 0), centreBefore * 0.5f, 0.01f);

    mb.reset();

    juce::AudioBuffer<float> sides { 2, block };
    fillSine (sides, 0, 1000.0, sr, 0.5f);
    for (int i = 0; i < block; ++i)
        sides.getWritePointer (1)[i] = -sides.getReadPointer (0)[i];   // L == -R -> pure side

    const auto sidesBefore = maxAbs (sides, 0);
    mb.process (sides, gain, 2, block);
    SF_CHECK_NEAR (maxAbs (sides, 0), sidesBefore, 0.01f);
}

SF_TEST (multiband_bandDepthScalesTheReduction)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    for (const float depth : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        auto settings = unityBands (sf::BandMode::Single);
        settings.bands[0].depth = depth;

        sf::MultibandProcessor mb;
        mb.prepare (sr, block, 2);
        mb.setSettings (settings);

        juce::AudioBuffer<float> audio { 2, block }, gain { 2, block };

        for (int ch = 0; ch < 2; ++ch)
        {
            juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 1.0f, block);
            juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 0.25f, block);
        }

        mb.process (audio, gain, 2, block);

        // gain 0.25 at depth d  ->  1 - d * 0.75
        const auto expected = 1.0f - depth * 0.75f;
        SF_CHECK_NEAR (audio.getReadPointer (0)[block - 1], expected, 1.0e-4);
    }
}

SF_TEST (multiband_disabledBandPassesThrough)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto settings = unityBands (sf::BandMode::Single);
    settings.bands[0].enabled = false;

    sf::MultibandProcessor mb;
    mb.prepare (sr, block, 2);
    mb.setSettings (settings);

    juce::AudioBuffer<float> audio { 2, block }, gain { 2, block };

    for (int ch = 0; ch < 2; ++ch)
    {
        juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 1.0f, block);
        juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 0.1f, block);
    }

    mb.process (audio, gain, 2, block);

    SF_CHECK_NEAR (audio.getReadPointer (0)[block - 1], 1.0, 1.0e-5);
}

SF_TEST (multiband_channelLinkBlendsBetweenIndependentAndLinked)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto run = [&] (sf::ChannelMode mode, float link)
    {
        auto settings = unityBands (sf::BandMode::Single, mode);
        settings.channelLink = link;

        sf::MultibandProcessor mb;
        mb.prepare (sr, block, 2);
        mb.setSettings (settings);

        juce::AudioBuffer<float> audio { 2, block }, gain { 2, block };

        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 1.0f, block);

        // Left asks for heavy reduction, right for none.
        juce::FloatVectorOperations::fill (gain.getWritePointer (0), 0.2f, block);
        juce::FloatVectorOperations::fill (gain.getWritePointer (1), 1.0f, block);

        mb.process (audio, gain, 2, block);

        return std::pair { audio.getReadPointer (0)[block - 1], audio.getReadPointer (1)[block - 1] };
    };

    // Fully independent: each channel gets exactly what it asked for.
    const auto independent = run (sf::ChannelMode::LeftRight, 0.0f);
    SF_CHECK_NEAR (independent.first, 0.2, 1.0e-4);
    SF_CHECK_NEAR (independent.second, 1.0, 1.0e-4);

    // Fully linked: both follow the deeper reduction.
    const auto linked = run (sf::ChannelMode::LeftRight, 1.0f);
    SF_CHECK_NEAR (linked.first, 0.2, 1.0e-4);
    SF_CHECK_NEAR (linked.second, 0.2, 1.0e-4);

    // Halfway.
    const auto half = run (sf::ChannelMode::LeftRight, 0.5f);
    SF_CHECK_NEAR (half.second, 0.6, 1.0e-3);

    // Stereo mode ignores the link control and is always fully linked.
    const auto stereo = run (sf::ChannelMode::Stereo, 0.0f);
    SF_CHECK_NEAR (stereo.second, 0.2, 1.0e-4);
}

SF_TEST (multiband_monoLowSumsTheBottomEnd)
{
    constexpr double sr = 48000.0;
    constexpr int block = 2048;

    auto settings = unityBands (sf::BandMode::Single);
    settings.monoLowEnable = true;
    settings.monoLowHz = 150.0f;

    sf::MultibandProcessor mb;
    mb.prepare (sr, block, 2);
    mb.setSettings (settings);

    juce::AudioBuffer<float> audio { 2, block }, gain { 2, block };

    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 1.0f, block);

    // 50 Hz out of phase between the channels: with a mono low stage it must
    // cancel to near silence in the low band. The input is refilled every block,
    // because feeding the previous output back in would not measure anything.
    for (int b = 0; b < 8; ++b)   // let the crossover settle
    {
        fillSine (audio, 0, 50.0, sr, 0.5f, 2.0 * kPi * 50.0 * b * block / sr);
        for (int i = 0; i < block; ++i)
            audio.getWritePointer (1)[i] = -audio.getReadPointer (0)[i];

        mb.process (audio, gain, 2, block);
    }

    SF_CHECK_MSG (maxAbs (audio, 0) < 0.08f,
                  ("out-of-phase low content should have been summed away, peak was "
                   + num (maxAbs (audio, 0))).c_str());

    // A 5 kHz out-of-phase pair must be left alone.
    mb.reset();

    for (int b = 0; b < 8; ++b)
    {
        fillSine (audio, 0, 5000.0, sr, 0.5f, 2.0 * kPi * 5000.0 * b * block / sr);
        for (int i = 0; i < block; ++i)
            audio.getWritePointer (1)[i] = -audio.getReadPointer (0)[i];

        mb.process (audio, gain, 2, block);
    }

    SF_CHECK_MSG (maxAbs (audio, 0) > 0.4f, "high content must not be mono-summed");
}

SF_TEST (multiband_survivesExtremeAndNonFiniteInput)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    sf::MultibandProcessor mb;
    mb.prepare (sr, block, 2);
    mb.setSettings (unityBands (sf::BandMode::Three));

    juce::AudioBuffer<float> audio { 2, block }, gain { 2, block };

    for (int b = 0; b < 20; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = audio.getWritePointer (ch);

            for (int i = 0; i < block; ++i)
                d[i] = (i % 5 == 0) ? std::numeric_limits<float>::quiet_NaN()
                     : (i % 5 == 1) ? std::numeric_limits<float>::infinity()
                     : (i % 5 == 2) ? 1.0e12f
                                    : 0.5f;

            juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 0.5f, block);
        }

        mb.process (audio, gain, 2, block);
    }

    // The band stage sanitises its input, so once fed clean audio again it must
    // recover rather than stay poisoned.
    for (int b = 0; b < 10; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 0.25f, block);
            juce::FloatVectorOperations::fill (gain.getWritePointer (ch), 1.0f, block);
        }

        mb.process (audio, gain, 2, block);
    }

    SF_CHECK_MSG (allFinite (audio), "the band stage must recover after non-finite input");
}
