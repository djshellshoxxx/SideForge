#include "../TestHelpers.h"
#include "DSP/SpectralDucker.h"

using namespace sft;

namespace
{
sf::SpectralDucker::Settings transparentSettings()
{
    sf::SpectralDucker::Settings s;
    s.depthDb = 0.0f;            // no attenuation: a pure reconstruction test
    s.amount = 1.0f;
    s.guard = 0.0f;
    s.freqSmooth = 0.0f;
    s.timeSmooth = 0.0f;
    return s;
}

/** Runs `numSamples` of noise through the ducker and reports the worst absolute
    difference between the output and the input delayed by the reported latency. */
double reconstructionError (int fftOrder, int overlap, double sampleRate, int blockSize,
                            int numChannels = 2)
{
    sf::SpectralDucker ducker;
    ducker.prepare (sampleRate, blockSize, numChannels);
    ducker.setSettings (transparentSettings());
    ducker.setConfig (fftOrder, overlap);

    const int fftSize = 1 << fftOrder;
    const int latency = fftSize;
    const int total = fftSize * 12;

    std::vector<std::vector<float>> input ((size_t) numChannels,
                                           std::vector<float> ((size_t) total, 0.0f));
    std::vector<std::vector<float>> output ((size_t) numChannels,
                                            std::vector<float> ((size_t) total, 0.0f));

    std::mt19937 rng { 4242 };
    std::uniform_real_distribution<float> dist { -0.5f, 0.5f };

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < total; ++i)
            input[(size_t) ch][(size_t) i] = dist (rng);

    juce::AudioBuffer<float> audio { numChannels, blockSize };
    juce::AudioBuffer<float> sidechain { numChannels, blockSize };
    sidechain.clear();

    for (int offset = 0; offset + blockSize <= total; offset += blockSize)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (audio.getWritePointer (ch),
                                               input[(size_t) ch].data() + offset, blockSize);

        ducker.process (audio, sidechain, numChannels, blockSize);

        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (output[(size_t) ch].data() + offset,
                                               audio.getReadPointer (ch), blockSize);
    }

    // Skip the first few windows, which are the fade-in of the overlap-add.
    double worst = 0.0;

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = fftSize * 3; i + blockSize < total; ++i)
        {
            const auto expected = input[(size_t) ch][(size_t) (i - latency)];
            const auto actual = output[(size_t) ch][(size_t) i];

            if (! std::isfinite (actual))
                return 1.0e9;

            worst = juce::jmax (worst, (double) std::abs (actual - expected));
        }

    return worst;
}
} // namespace

SF_TEST (spectral_reconstructsItsInputExactlyAtEveryQualitySetting)
{
    // Every FFT size and overlap the quality modes can select.
    struct Config { int order; int overlap; const char* name; };

    for (const auto& cfg : { Config { 9, 4, "Live 512/4" },
                             Config { 10, 4, "Studio 1024/4" },
                             Config { 11, 4, "Spectral 2048/4" },
                             Config { 12, 8, "Render 4096/8" } })
    {
        const auto error = reconstructionError (cfg.order, cfg.overlap, 48000.0, 256);

        SF_CHECK_MSG (error < 1.0e-4,
                      (std::string (cfg.name) + " reconstruction error was " + num (error)).c_str());
    }
}

SF_TEST (spectral_reconstructionIsIndependentOfBlockSize)
{
    for (const int blockSize : { 32, 64, 128, 256, 512, 1024 })
    {
        const auto error = reconstructionError (10, 4, 48000.0, blockSize);

        SF_CHECK_MSG (error < 1.0e-4,
                      ("block size " + std::to_string (blockSize) + " gave error " + num (error)).c_str());
    }
}

SF_TEST (spectral_reconstructionHoldsAtEverySampleRate)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto error = reconstructionError (10, 4, sr, 256);

        SF_CHECK_MSG (error < 1.0e-4,
                      (num (sr) + " Hz gave reconstruction error " + num (error)).c_str());
    }
}

SF_TEST (spectral_reportedLatencyMatchesTheActualDelay)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    constexpr int fftOrder = 10;
    constexpr int overlap = 4;
    constexpr int fftSize = 1 << fftOrder;

    sf::SpectralDucker ducker;
    ducker.prepare (sr, blockSize, 1);
    ducker.setSettings (transparentSettings());
    ducker.setConfig (fftOrder, overlap);

    SF_CHECK (ducker.getPendingLatencySamples() == fftSize);
    juce::ignoreUnused (overlap);

    // Send a single impulse and find where it comes out.
    const int total = fftSize * 10;
    std::vector<float> output ((size_t) total, 0.0f);

    juce::AudioBuffer<float> audio { 1, blockSize }, sidechain { 1, blockSize };
    sidechain.clear();

    const int impulseAt = fftSize * 4;

    for (int offset = 0; offset + blockSize <= total; offset += blockSize)
    {
        audio.clear();

        if (impulseAt >= offset && impulseAt < offset + blockSize)
            audio.getWritePointer (0)[impulseAt - offset] = 1.0f;

        ducker.process (audio, sidechain, 1, blockSize);

        juce::FloatVectorOperations::copy (output.data() + offset, audio.getReadPointer (0), blockSize);
    }

    int peakIndex = 0;
    float peak = 0.0f;

    for (int i = 0; i < total; ++i)
        if (std::abs (output[(size_t) i]) > peak)
        {
            peak = std::abs (output[(size_t) i]);
            peakIndex = i;
        }

    SF_CHECK_MSG (peakIndex - impulseAt == ducker.getLatencySamples(),
                  ("impulse emerged " + std::to_string (peakIndex - impulseAt)
                   + " samples late, reported latency is "
                   + std::to_string (ducker.getLatencySamples())).c_str());
    SF_CHECK_NEAR (peak, 1.0, 1.0e-3);
}

SF_TEST (spectral_collidingSidechainAttenuatesTheDestination)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    auto runWithSidechain = [&] (double sidechainFreq)
    {
        sf::SpectralDucker ducker;
        ducker.prepare (sr, blockSize, 1);

        sf::SpectralDucker::Settings s;
        s.depthDb = 24.0f;
        s.lowFreqHz = 20.0f;
        s.highFreqHz = 20000.0f;
        s.attackMs = 1.0f;
        s.releaseMs = 10.0f;
        s.freqSmooth = 0.1f;
        s.timeSmooth = 0.0f;
        s.preserveBody = 0.0f;
        s.guard = 0.0f;
        s.tonal = 1.0f;
        ducker.setSettings (s);
        ducker.setConfig (11, 4);

        juce::AudioBuffer<float> audio { 1, blockSize }, sidechain { 1, blockSize };

        double mainPhase = 0.0, sidePhase = 0.0;
        const auto mainInc = 2.0 * kPi * 1000.0 / sr;
        const auto sideInc = 2.0 * kPi * sidechainFreq / sr;

        float peak = 0.0f;
        const int blocks = 120;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                audio.getWritePointer (0)[i] = 0.5f * (float) std::sin (mainPhase);
                sidechain.getWritePointer (0)[i] = 0.5f * (float) std::sin (sidePhase);
                mainPhase += mainInc;
                sidePhase += sideInc;
            }

            ducker.process (audio, sidechain, 1, blockSize);

            if (b > blocks - 10)   // measure once everything has settled
                peak = juce::jmax (peak, audio.getMagnitude (0, 0, blockSize));
        }

        return peak;
    };

    // A sidechain sitting on the same 1 kHz as the destination collides, so the
    // destination must be pulled down substantially.
    const auto colliding = runWithSidechain (1000.0);
    // A sidechain three octaves away does not collide, so the destination survives.
    const auto distant = runWithSidechain (125.0);

    SF_CHECK_FINITE (colliding);
    SF_CHECK_FINITE (distant);

    SF_CHECK_MSG (distant > 0.4f,
                  ("a non-colliding sidechain should barely touch the destination, peak was "
                   + num (distant)).c_str());
    SF_CHECK_MSG (colliding < distant * 0.5f,
                  ("a colliding sidechain should duck much harder: colliding " + num (colliding)
                   + " vs distant " + num (distant)).c_str());
}

SF_TEST (spectral_silentSidechainLeavesTheDestinationAlone)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    sf::SpectralDucker ducker;
    ducker.prepare (sr, blockSize, 2);

    sf::SpectralDucker::Settings s;
    s.depthDb = 36.0f;          // as aggressive as the plugin allows
    s.preserveBody = 0.0f;
    s.guard = 0.0f;
    ducker.setSettings (s);
    ducker.setConfig (10, 4);

    juce::AudioBuffer<float> audio { 2, blockSize }, sidechain { 2, blockSize };
    sidechain.clear();

    float settledPeak = 0.0f;

    for (int b = 0; b < 80; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            fillNoise (audio, ch, 0.5f, (unsigned) (b * 2 + ch));

        ducker.process (audio, sidechain, 2, blockSize);

        if (b > 40)
            settledPeak = juce::jmax (settledPeak, audio.getMagnitude (0, 0, blockSize));
    }

    SF_CHECK_MSG (settledPeak > 0.3f,
                  ("a silent sidechain must not carve the destination, peak fell to "
                   + num (settledPeak)).c_str());
    SF_CHECK_NEAR (ducker.getLastMinGain(), 1.0, 0.02);
}

SF_TEST (spectral_outputStaysFiniteUnderEverySettingCombination)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    std::mt19937 rng { 31337 };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };

    for (int iteration = 0; iteration < 24; ++iteration)
    {
        sf::SpectralDucker ducker;
        ducker.prepare (sr, blockSize, 2);

        sf::SpectralDucker::Settings s;
        s.depthDb      = unit (rng) * 60.0f;
        s.lowFreqHz    = 20.0f + unit (rng) * 2000.0f;
        s.highFreqHz   = 500.0f + unit (rng) * 19000.0f;
        s.freqSmooth   = unit (rng);
        s.timeSmooth   = unit (rng);
        s.attackMs     = 0.5f + unit (rng) * 200.0f;
        s.releaseMs    = 5.0f + unit (rng) * 1000.0f;
        s.preserveBody = unit (rng);
        s.tonal        = unit (rng);
        s.guard        = unit (rng);
        s.tiltDbPerOct = unit (rng) * 12.0f - 6.0f;
        s.amount       = unit (rng);
        ducker.setSettings (s);
        ducker.setConfig (9 + (iteration % 4), (iteration % 2) ? 8 : 4);

        juce::AudioBuffer<float> audio { 2, blockSize }, sidechain { 2, blockSize };

        for (int b = 0; b < 40; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                fillNoise (audio, ch, 0.9f, (unsigned) (iteration * 100 + b * 2 + ch));
                fillNoise (sidechain, ch, 0.9f, (unsigned) (iteration * 200 + b * 2 + ch));
            }

            ducker.process (audio, sidechain, 2, blockSize);

            SF_CHECK_MSG (allFinite (audio), "spectral output must always be finite");
        }
    }
}

SF_TEST (spectral_recoversFromNonFiniteInput)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    sf::SpectralDucker ducker;
    ducker.prepare (sr, blockSize, 2);
    ducker.setSettings (transparentSettings());
    ducker.setConfig (10, 4);

    juce::AudioBuffer<float> audio { 2, blockSize }, sidechain { 2, blockSize };
    sidechain.clear();

    for (int b = 0; b < 10; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = audio.getWritePointer (ch);

            for (int i = 0; i < blockSize; ++i)
                d[i] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN()
                                    : std::numeric_limits<float>::infinity();
        }

        ducker.process (audio, sidechain, 2, blockSize);
        SF_CHECK_MSG (allFinite (audio), "non-finite input must not escape the spectral stage");
    }

    for (int b = 0; b < 30; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            fillSine (audio, ch, 1000.0, sr, 0.5f);

        ducker.process (audio, sidechain, 2, blockSize);
    }

    SF_CHECK_MSG (allFinite (audio) && maxAbs (audio, 0) > 0.1f,
                  "the spectral stage must recover once the input is clean again");
}

SF_TEST (spectral_configChangeIsSafeMidStream)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    sf::SpectralDucker ducker;
    ducker.prepare (sr, blockSize, 2);

    sf::SpectralDucker::Settings s;
    s.depthDb = 12.0f;
    ducker.setSettings (s);

    juce::AudioBuffer<float> audio { 2, blockSize }, sidechain { 2, blockSize };

    const int configs[][2] = { { 9, 4 }, { 10, 4 }, { 11, 4 }, { 12, 8 }, { 9, 4 }, { 12, 8 } };

    for (int c = 0; c < 6; ++c)
    {
        ducker.setConfig (configs[c][0], configs[c][1]);

        for (int b = 0; b < 30; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                fillNoise (audio, ch, 0.5f, (unsigned) (c * 50 + b));
                fillNoise (sidechain, ch, 0.5f, (unsigned) (c * 70 + b));
            }

            ducker.process (audio, sidechain, 2, blockSize);
            SF_CHECK (allFinite (audio));
        }

        const auto expected = 1 << configs[c][0];
        SF_CHECK_MSG (ducker.getLatencySamples() == expected,
                      "latency must follow the applied configuration");
    }
}

SF_TEST (spectral_publishedCollisionMapDescribesTheActualCollision)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;
    constexpr int fftOrder = 11;

    sf::SpectralDucker ducker;
    ducker.prepare (sr, blockSize, 1);

    sf::SpectralDucker::Settings s;
    s.depthDb = 24.0f;
    s.lowFreqHz = 20.0f;
    s.highFreqHz = 20000.0f;
    s.freqSmooth = 0.1f;
    s.timeSmooth = 0.0f;
    s.preserveBody = 0.0f;
    s.tonal = 1.0f;
    s.guard = 0.6f;          // on, because this is what used to corrupt the map
    ducker.setSettings (s);
    ducker.setConfig (fftOrder, 4);

    juce::AudioBuffer<float> audio { 1, blockSize }, sidechain { 1, blockSize };

    // The destination holds 400 Hz and 4 kHz; the sidechain only occupies 400 Hz.
    double mainLowPhase = 0.0, mainHighPhase = 0.0, sidePhase = 0.0;
    const auto mainLowInc  = 2.0 * kPi * 400.0 / sr;
    const auto mainHighInc = 2.0 * kPi * 4000.0 / sr;
    const auto sideInc     = 2.0 * kPi * 400.0 / sr;

    for (int b = 0; b < 160; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            audio.getWritePointer (0)[i] = 0.35f * (float) (std::sin (mainLowPhase)
                                                            + std::sin (mainHighPhase));
            sidechain.getWritePointer (0)[i] = 0.7f * (float) std::sin (sidePhase);

            mainLowPhase += mainLowInc;
            mainHighPhase += mainHighInc;
            sidePhase += sideInc;
        }

        ducker.process (audio, sidechain, 1, blockSize);
    }

    const auto& snapshot = ducker.readSnapshot();
    SF_CHECK_MSG (snapshot.numBins > 1, "the ducker should have published a snapshot");

    const auto binHz = snapshot.sampleRate / (float) snapshot.fftSize;
    auto binFor = [&] (float hz) { return juce::jlimit (0, snapshot.numBins - 1,
                                                        (int) std::round (hz / binHz)); };

    auto peakNear = [&] (const std::array<float, sf::kMaxBins>& values, float hz, int radius)
    {
        const auto centre = binFor (hz);
        auto highest = values[(size_t) centre];

        for (int k = juce::jmax (0, centre - radius);
             k <= juce::jmin (snapshot.numBins - 1, centre + radius); ++k)
            highest = juce::jmax (highest, values[(size_t) k]);

        return highest;
    };

    const auto collisionAt400  = peakNear (snapshot.collision, 400.0f, 3);
    const auto collisionAt4000 = peakNear (snapshot.collision, 4000.0f, 3);

    for (int k = 0; k < snapshot.numBins; ++k)
    {
        SF_CHECK_FINITE (snapshot.collision[(size_t) k]);
        SF_CHECK (snapshot.collision[(size_t) k] >= 0.0f && snapshot.collision[(size_t) k] <= 1.0f);
    }

    // The whole point of a collision map: high where the two sources compete,
    // low where only the destination is playing.
    SF_CHECK_MSG (collisionAt400 > 0.3f,
                  ("the shared 400 Hz region should read as a collision, got "
                   + num (collisionAt400)).c_str());
    SF_CHECK_MSG (collisionAt4000 < collisionAt400 * 0.5f,
                  ("4 kHz has no sidechain content and should not read as a collision: "
                   + num (collisionAt4000) + " against " + num (collisionAt400)).c_str());

    // ...and the published attenuation should follow it.
    const auto gainAt400  = peakNear (snapshot.gainDb, 400.0f, 3);
    const auto gainAt4000 = peakNear (snapshot.gainDb, 4000.0f, 3);

    SF_CHECK_MSG (gainAt400 < -1.0f,
                  ("the colliding region should be attenuated, got " + num (gainAt400) + " dB").c_str());
    SF_CHECK_MSG (gainAt4000 > gainAt400,
                  "the non-colliding region should be attenuated less than the colliding one");

    // The sidechain spectrum should show the tone that is actually there.
    SF_CHECK_MSG (peakNear (snapshot.sideDb, 400.0f, 3) > peakNear (snapshot.sideDb, 4000.0f, 3) + 12.0f,
                  "the published sidechain spectrum should peak where the sidechain actually is");
}

SF_TEST (spectral_quietSidechainDoesNotDuckTheEmptyPartsOfTheSpectrum)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    // The case that matters in a real mix: the sidechain is a kick with silence
    // between the hits, and the destination has nothing much above a few hundred
    // hertz. Comparing two bins of numerical noise must not read as a collision,
    // or the top end gets ducked between every kick.
    sf::SpectralDucker ducker;
    ducker.prepare (sr, blockSize, 1);

    sf::SpectralDucker::Settings s;
    s.depthDb = 30.0f;
    s.lowFreqHz = 20.0f;
    s.highFreqHz = 20000.0f;
    s.preserveBody = 0.0f;
    s.guard = 0.0f;
    s.timeSmooth = 0.0f;
    s.tonal = 1.0f;
    ducker.setSettings (s);
    ducker.setConfig (11, 4);

    juce::AudioBuffer<float> audio { 1, blockSize }, sidechain { 1, blockSize };

    double mainPhase = 0.0, sidePhase = 0.0;
    const auto mainInc = 2.0 * kPi * 200.0 / sr;
    const auto sideInc = 2.0 * kPi * 60.0 / sr;

    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            audio.getWritePointer (0)[i] = 0.4f * (float) std::sin (mainPhase);

            // A kick every half second, silent in between.
            const auto n = b * blockSize + i;
            const auto position = n % 24000;
            sidechain.getWritePointer (0)[i] =
                position < 2400 ? 0.8f * (float) std::sin (sidePhase)
                                       * (1.0f - (float) position / 2400.0f)
                                : 0.0f;

            mainPhase += mainInc;
            sidePhase += sideInc;
        }

        ducker.process (audio, sidechain, 1, blockSize);
    }

    const auto& snapshot = ducker.readSnapshot();
    SF_CHECK (snapshot.numBins > 1);

    const auto binHz = snapshot.sampleRate / (float) snapshot.fftSize;
    const auto firstHighBin = juce::jlimit (0, snapshot.numBins - 1, (int) std::round (2000.0f / binHz));

    auto worstCollision = 0.0f;
    auto worstAttenuation = 0.0f;

    for (int k = firstHighBin; k < snapshot.numBins; ++k)
    {
        worstCollision = juce::jmax (worstCollision, snapshot.collision[(size_t) k]);
        worstAttenuation = juce::jmin (worstAttenuation, snapshot.gainDb[(size_t) k]);
    }

    SF_CHECK_MSG (worstCollision < 0.15f,
                  ("empty high bins should not read as a collision, worst was "
                   + num (worstCollision)).c_str());
    SF_CHECK_MSG (worstAttenuation > -2.0f,
                  ("empty high bins should barely be touched, worst was "
                   + num (worstAttenuation) + " dB").c_str());
}
