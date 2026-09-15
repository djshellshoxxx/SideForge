#include "../TestHelpers.h"
#include "DSP/BandMatrix.h"

using namespace sft;

namespace
{
/** A matrix where every band is switched off: the split and the recombination,
    and nothing else. */
sf::BandMatrix::Settings passThroughSettings (int numBands = 3)
{
    sf::BandMatrix::Settings s;
    s.numBands = numBands;
    s.crossoverHz[0] = 120.0f;
    s.crossoverHz[1] = 2500.0f;
    s.crossoverHz[2] = 8000.0f;

    for (auto& band : s.bands)
    {
        band = {};
        band.mode = sf::BandProcessorMode::Off;
        band.enabled = true;
        band.bypass = false;
        band.depth = 1.0f;
        band.mix = 1.0f;
    }

    return s;
}

/** Turns one band into a hard, fast ducker keyed from the external sidechain. */
void makeDucker (sf::BandProcessor::Settings& band, float depth = 1.0f, float rangeDb = 24.0f)
{
    band.mode = sf::BandProcessorMode::Ducker;
    band.depth = depth;
    band.rangeDb = rangeDb;
    band.thresholdDb = -40.0f;
    band.attackMs = 0.5f;
    band.releaseMs = 50.0f;
    band.detector = sf::Detector::Peak;
    band.scSource = sf::BandScSource::External;
}

struct RunResult
{
    double outputRms = 0.0;
    double inputRms = 0.0;
    float peak = 0.0f;
    bool finite = true;

    double levelChangeDb() const
    {
        return 20.0 * std::log10 (juce::jmax (1.0e-12, outputRms) / juce::jmax (1.0e-12, inputRms));
    }
};

/** Runs a steady tone through the matrix against a steady sidechain tone, and
    reports how much the level changed once everything has settled. */
RunResult runMatrix (sf::BandMatrix& matrix, double toneHz, double sidechainHz, float sidechainAmp,
                     double sampleRate = 48000.0, int block = 512, int numChannels = 2,
                     int blocks = 140, int settleBlocks = 90)
{
    juce::AudioBuffer<float> audio { numChannels, block }, sidechain { numChannels, block };

    double tonePhase = 0.0, sidePhase = 0.0;
    const auto toneInc = 2.0 * kPi * toneHz / sampleRate;
    const auto sideInc = 2.0 * kPi * sidechainHz / sampleRate;

    RunResult result;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const auto tone = 0.5f * (float) std::sin (tonePhase);
            const auto side = sidechainAmp * (float) std::sin (sidePhase);
            tonePhase += toneInc;
            sidePhase += sideInc;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                audio.getWritePointer (ch)[i] = tone;
                sidechain.getWritePointer (ch)[i] = side;
            }
        }

        if (b >= settleBlocks)
            for (int i = 0; i < block; ++i)
            {
                const auto in = (double) audio.getReadPointer (0)[i];
                result.inputRms += in * in;
            }

        matrix.process (audio, sidechain, nullptr, numChannels, block);

        if (! allFinite (audio))
            result.finite = false;

        if (b >= settleBlocks)
        {
            for (int i = 0; i < block; ++i)
            {
                const auto out = (double) audio.getReadPointer (0)[i];
                result.outputRms += out * out;
            }

            result.peak = juce::jmax (result.peak, audio.getMagnitude (0, 0, block));
        }
    }

    return result;
}

std::unique_ptr<sf::BandMatrix> makeMatrix (const sf::BandMatrix::Settings& settings,
                                            double sampleRate = 48000.0, int block = 512,
                                            int numChannels = 2)
{
    auto matrix = std::make_unique<sf::BandMatrix>();
    matrix->prepare (sampleRate, block, numChannels);
    matrix->setSettings (settings);
    return matrix;
}
} // namespace

// ---------------------------------------------------------------------------
//  Reconstruction through the whole matrix
// ---------------------------------------------------------------------------

SF_TEST (bandMatrix_allBandsOffReconstructsTheInput)
{
    for (int numBands = 1; numBands <= sf::kMaxMatrixBands; ++numBands)
    {
        for (const double toneHz : { 50.0, 120.0, 600.0, 2500.0, 6000.0, 12000.0 })
        {
            auto matrix = makeMatrix (passThroughSettings (numBands));
            const auto result = runMatrix (*matrix, toneHz, 1000.0, 0.0f);

            SF_CHECK (result.finite);
            SF_CHECK_MSG (std::abs (result.levelChangeDb()) < 0.5,
                          (std::to_string (numBands) + " bands off changed "
                           + std::to_string ((int) toneHz) + " Hz by "
                           + num (result.levelChangeDb()) + " dB").c_str());
        }
    }
}

SF_TEST (bandMatrix_bypassedBandsPassTheirRegionUnchanged)
{
    auto settings = passThroughSettings (3);

    // Every band configured to duck hard, but all three bypassed.
    for (auto& band : settings.bands)
    {
        makeDucker (band);
        band.bypass = true;
    }

    for (const double toneHz : { 50.0, 600.0, 9000.0 })
    {
        auto matrix = makeMatrix (settings);
        const auto result = runMatrix (*matrix, toneHz, 200.0, 0.9f);

        SF_CHECK_MSG (std::abs (result.levelChangeDb()) < 0.5,
                      ("a bypassed band changed " + std::to_string ((int) toneHz) + " Hz by "
                       + num (result.levelChangeDb()) + " dB").c_str());
    }
}

SF_TEST (bandMatrix_disabledBandsBehaveLikeBypassed)
{
    auto settings = passThroughSettings (3);

    for (auto& band : settings.bands)
    {
        makeDucker (band);
        band.enabled = false;
    }

    auto matrix = makeMatrix (settings);
    const auto result = runMatrix (*matrix, 60.0, 200.0, 0.9f);

    SF_CHECK_MSG (std::abs (result.levelChangeDb()) < 0.5,
                  ("a disabled band changed the level by " + num (result.levelChangeDb())
                   + " dB").c_str());
    SF_CHECK_MSG (matrix->getLatencySamples() == 0,
                  "a matrix of disabled bands should report no latency");
}

// ---------------------------------------------------------------------------
//  Frequency-selective processing — the whole point of the feature
// ---------------------------------------------------------------------------

SF_TEST (bandMatrix_lowOnlyDuckingLeavesTheHighBandAlone)
{
    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[0], 1.0f, 24.0f);   // low band ducks hard

    // A 40 Hz tone sits in the low band; a 9 kHz tone sits in the high band.
    // The same loud sidechain is present for both.
    auto lowMatrix = makeMatrix (settings);
    const auto low = runMatrix (*lowMatrix, 40.0, 60.0, 0.9f);

    auto highMatrix = makeMatrix (settings);
    const auto high = runMatrix (*highMatrix, 9000.0, 60.0, 0.9f);

    SF_CHECK_MSG (low.levelChangeDb() < -12.0,
                  ("the low band should have been ducked hard, got " + num (low.levelChangeDb())
                   + " dB").c_str());
    SF_CHECK_MSG (std::abs (high.levelChangeDb()) < 0.5,
                  ("the high band should have been left alone, got " + num (high.levelChangeDb())
                   + " dB").c_str());
}

SF_TEST (bandMatrix_highOnlyDuckingLeavesTheLowBandAlone)
{
    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[2], 1.0f, 24.0f);   // only the high band ducks

    auto lowMatrix = makeMatrix (settings);
    const auto low = runMatrix (*lowMatrix, 40.0, 60.0, 0.9f);

    auto highMatrix = makeMatrix (settings);
    const auto high = runMatrix (*highMatrix, 9000.0, 60.0, 0.9f);

    SF_CHECK_MSG (std::abs (low.levelChangeDb()) < 0.5,
                  ("the low band should have been left alone, got " + num (low.levelChangeDb())
                   + " dB").c_str());
    SF_CHECK_MSG (high.levelChangeDb() < -12.0,
                  ("the high band should have been ducked, got " + num (high.levelChangeDb())
                   + " dB").c_str());
}

SF_TEST (bandMatrix_midOnlyCompressionLeavesItsNeighboursAlone)
{
    auto settings = passThroughSettings (3);

    auto& mid = settings.bands[1];
    mid.mode = sf::BandProcessorMode::Compressor;
    mid.depth = 1.0f;
    mid.rangeDb = 24.0f;
    mid.ratio = 8.0f;
    mid.thresholdDb = -40.0f;
    mid.attackMs = 1.0f;
    mid.releaseMs = 60.0f;
    mid.scSource = sf::BandScSource::External;

    auto lowMatrix = makeMatrix (settings);
    const auto low = runMatrix (*lowMatrix, 40.0, 600.0, 0.9f);

    auto midMatrix = makeMatrix (settings);
    const auto midResult = runMatrix (*midMatrix, 600.0, 600.0, 0.9f);

    auto highMatrix = makeMatrix (settings);
    const auto high = runMatrix (*highMatrix, 9000.0, 600.0, 0.9f);

    SF_CHECK_MSG (midResult.levelChangeDb() < -6.0,
                  ("the mid band should have been compressed, got " + num (midResult.levelChangeDb())
                   + " dB").c_str());
    SF_CHECK_MSG (std::abs (low.levelChangeDb()) < 0.5, "the low band should be untouched");
    SF_CHECK_MSG (std::abs (high.levelChangeDb()) < 0.5, "the high band should be untouched");
}

SF_TEST (bandMatrix_depthScalesTheReduction)
{
    double previous = 0.0;

    for (const float depth : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        auto settings = passThroughSettings (3);
        makeDucker (settings.bands[0], depth, 24.0f);

        auto matrix = makeMatrix (settings);
        const auto result = runMatrix (*matrix, 40.0, 60.0, 0.9f);

        if (depth == 0.0f)
            SF_CHECK_MSG (std::abs (result.levelChangeDb()) < 0.3,
                          ("depth 0 should do nothing, got " + num (result.levelChangeDb())
                           + " dB").c_str());
        else
            SF_CHECK_MSG (result.levelChangeDb() < previous - 1.0,
                          ("depth " + num (depth) + " should duck deeper than the step below it: "
                           + num (result.levelChangeDb()) + " vs " + num (previous)).c_str());

        previous = result.levelChangeDb();
    }
}

SF_TEST (bandMatrix_bandsCanUseDifferentDepthsAtOnce)
{
    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[0], 1.0f, 24.0f);    // hard lows
    makeDucker (settings.bands[1], 0.45f, 24.0f);   // moderate mids
    makeDucker (settings.bands[2], 0.12f, 24.0f);   // light highs

    auto lowMatrix = makeMatrix (settings);
    const auto low = runMatrix (*lowMatrix, 40.0, 60.0, 0.9f);

    auto midMatrix = makeMatrix (settings);
    const auto mid = runMatrix (*midMatrix, 600.0, 60.0, 0.9f);

    auto highMatrix = makeMatrix (settings);
    const auto high = runMatrix (*highMatrix, 9000.0, 60.0, 0.9f);

    SF_CHECK_MSG (low.levelChangeDb() < mid.levelChangeDb() - 2.0,
                  ("lows should duck deeper than mids: " + num (low.levelChangeDb()) + " vs "
                   + num (mid.levelChangeDb())).c_str());
    SF_CHECK_MSG (mid.levelChangeDb() < high.levelChangeDb() - 1.0,
                  ("mids should duck deeper than highs: " + num (mid.levelChangeDb()) + " vs "
                   + num (high.levelChangeDb())).c_str());
    SF_CHECK_MSG (high.levelChangeDb() < -0.3,
                  "the high band should still move a little");
}

SF_TEST (bandMatrix_limiterModeHoldsTheBandDown)
{
    auto settings = passThroughSettings (2);
    settings.crossoverHz[0] = 200.0f;

    auto& low = settings.bands[0];
    low.mode = sf::BandProcessorMode::Limiter;
    low.depth = 1.0f;
    low.rangeDb = 30.0f;
    low.thresholdDb = -30.0f;
    low.attackMs = 0.2f;
    low.releaseMs = 40.0f;
    low.scSource = sf::BandScSource::External;

    auto quiet = makeMatrix (settings);
    const auto withQuietSidechain = runMatrix (*quiet, 60.0, 80.0, 0.02f);

    auto loud = makeMatrix (settings);
    const auto withLoudSidechain = runMatrix (*loud, 60.0, 80.0, 0.9f);

    SF_CHECK_MSG (std::abs (withQuietSidechain.levelChangeDb()) < 1.5,
                  ("a sidechain under the threshold should barely limit, got "
                   + num (withQuietSidechain.levelChangeDb()) + " dB").c_str());
    SF_CHECK_MSG (withLoudSidechain.levelChangeDb() < -15.0,
                  ("a loud sidechain should limit hard, got "
                   + num (withLoudSidechain.levelChangeDb()) + " dB").c_str());
}

SF_TEST (bandMatrix_differentTimingPerBandActuallyDiffers)
{
    // Two bands, same depth, same sidechain — but the low band recovers slowly
    // and the high band quickly. After the sidechain stops, the low band should
    // still be held down while the high band has already come back.
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    auto settings = passThroughSettings (2);
    settings.crossoverHz[0] = 1000.0f;

    makeDucker (settings.bands[0], 1.0f, 24.0f);
    settings.bands[0].releaseMs = 1500.0f;

    makeDucker (settings.bands[1], 1.0f, 24.0f);
    settings.bands[1].releaseMs = 15.0f;

    auto matrix = makeMatrix (settings, sr, block);

    juce::AudioBuffer<float> audio { 2, block }, sidechain { 2, block };
    double lowPhase = 0.0, highPhase = 0.0;

    // Drive the sidechain hard for a while, then cut it and watch the recovery.
    for (int b = 0; b < 200; ++b)
    {
        const auto sidechainOn = (b < 120);

        for (int i = 0; i < block; ++i)
        {
            const auto lowTone = 0.4f * (float) std::sin (lowPhase);
            const auto highTone = 0.4f * (float) std::sin (highPhase);
            lowPhase += 2.0 * kPi * 100.0 / sr;
            highPhase += 2.0 * kPi * 6000.0 / sr;

            for (int ch = 0; ch < 2; ++ch)
            {
                audio.getWritePointer (ch)[i] = lowTone + highTone;
                sidechain.getWritePointer (ch)[i] = sidechainOn ? 0.9f * (float) std::sin (lowPhase * 0.5) : 0.0f;
            }
        }

        matrix->process (audio, sidechain, nullptr, 2, block);

        // The meter reports the *deepest* reduction within a block, so this has to
        // be sampled far enough past the cut that the whole block is recovered:
        // 80 ms is five time constants for the fast band and a twentieth of one
        // for the slow band.
        if (b == 135)
        {
            const auto slow = matrix->getBand (0).getGainReductionDb();
            const auto fast = matrix->getBand (1).getGainReductionDb();

            // Not quite zero: gating a tone off abruptly leaves a decaying tail in
            // the detector's 10 Hz high-pass that sits near the threshold for a
            // while. A couple of dB of residual is the test signal, not the band.
            SF_CHECK_MSG (fast < 4.0f,
                          ("the fast band should have recovered by now, still at " + num (fast)
                           + " dB").c_str());
            SF_CHECK_MSG (slow > fast + 8.0f,
                          ("the slow band should still be held down: slow " + num (slow)
                           + " dB vs fast " + num (fast) + " dB").c_str());
        }
    }
}

// ---------------------------------------------------------------------------
//  Detector routing
// ---------------------------------------------------------------------------

SF_TEST (bandMatrix_matchingBandDetectionIgnoresOtherFrequencies)
{
    // The low band listens only to the *low band of the sidechain*. A sidechain
    // made entirely of high-frequency content must therefore not duck it.
    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[0], 1.0f, 24.0f);
    settings.bands[0].scSource = sf::BandScSource::ExternalBand;

    auto lowSidechain = makeMatrix (settings);
    const auto withLow = runMatrix (*lowSidechain, 40.0, 50.0, 0.9f);

    auto highSidechain = makeMatrix (settings);
    const auto withHigh = runMatrix (*highSidechain, 40.0, 9000.0, 0.9f);

    SF_CHECK_MSG (withLow.levelChangeDb() < -12.0,
                  ("a low sidechain should duck the low band, got " + num (withLow.levelChangeDb())
                   + " dB").c_str());
    SF_CHECK_MSG (std::abs (withHigh.levelChangeDb()) < 1.0,
                  ("a high-only sidechain should not reach the low band's detector, got "
                   + num (withHigh.levelChangeDb()) + " dB").c_str());
}

SF_TEST (bandMatrix_fullSidechainDetectionSeesEverything)
{
    // The same band listening to the *whole* sidechain does react to high content.
    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[0], 1.0f, 24.0f);
    settings.bands[0].scSource = sf::BandScSource::External;

    auto matrix = makeMatrix (settings);
    const auto result = runMatrix (*matrix, 40.0, 9000.0, 0.9f);

    SF_CHECK_MSG (result.levelChangeDb() < -12.0,
                  ("full-sidechain detection should duck on high content, got "
                   + num (result.levelChangeDb()) + " dB").c_str());
}

SF_TEST (bandMatrix_detectorFilterChangesDetectionNotTheAudio)
{
    // A band whose detector filter rejects the sidechain must pass its audio
    // untouched — and crucially the *audio* must not be filtered by it.
    auto settings = passThroughSettings (2);
    settings.crossoverHz[0] = 1000.0f;

    makeDucker (settings.bands[0], 1.0f, 24.0f);
    settings.bands[0].scFilterEnable = true;
    settings.bands[0].scHighPassHz = 4000.0f;     // rejects the 60 Hz sidechain
    settings.bands[0].scLowPassHz = 20000.0f;

    auto matrix = makeMatrix (settings);
    const auto result = runMatrix (*matrix, 100.0, 60.0, 0.9f);

    SF_CHECK_MSG (std::abs (result.levelChangeDb()) < 0.6,
                  ("the detector filter should have rejected the sidechain and left the "
                   "audio alone, got " + num (result.levelChangeDb()) + " dB").c_str());

    // With the filter passing the sidechain, the same band ducks.
    settings.bands[0].scHighPassHz = 20.0f;
    auto open = makeMatrix (settings);
    const auto ducked = runMatrix (*open, 100.0, 60.0, 0.9f);

    SF_CHECK_MSG (ducked.levelChangeDb() < -12.0,
                  ("with the filter open the band should duck, got " + num (ducked.levelChangeDb())
                   + " dB").c_str());
}

SF_TEST (bandMatrix_internalDetectionWorksWithoutASidechain)
{
    auto settings = passThroughSettings (2);
    settings.crossoverHz[0] = 1000.0f;

    auto& low = settings.bands[0];
    low.mode = sf::BandProcessorMode::Compressor;
    low.depth = 1.0f;
    low.rangeDb = 24.0f;
    low.ratio = 8.0f;
    low.thresholdDb = -30.0f;
    low.attackMs = 1.0f;
    low.releaseMs = 60.0f;
    low.scSource = sf::BandScSource::InternalBand;

    auto matrix = makeMatrix (settings);
    const auto result = runMatrix (*matrix, 100.0, 1000.0, 0.0f);   // no sidechain at all

    SF_CHECK_MSG (result.levelChangeDb() < -4.0,
                  ("internal detection should compress from the signal itself, got "
                   + num (result.levelChangeDb()) + " dB").c_str());
}

// ---------------------------------------------------------------------------
//  Solo, mute and listen
// ---------------------------------------------------------------------------

SF_TEST (bandMatrix_muteRemovesOnlyThatBand)
{
    auto settings = passThroughSettings (3);
    settings.mute[0] = true;

    auto lowMatrix = makeMatrix (settings);
    const auto low = runMatrix (*lowMatrix, 40.0, 1000.0, 0.0f);

    auto highMatrix = makeMatrix (settings);
    const auto high = runMatrix (*highMatrix, 9000.0, 1000.0, 0.0f);

    SF_CHECK_MSG (low.levelChangeDb() < -30.0,
                  ("a muted low band should be gone, got " + num (low.levelChangeDb()) + " dB").c_str());
    SF_CHECK_MSG (std::abs (high.levelChangeDb()) < 0.5,
                  "muting the low band must not touch the high band");
}

SF_TEST (bandMatrix_soloSilencesEverythingElse)
{
    auto settings = passThroughSettings (3);
    settings.solo[2] = true;   // high band only

    auto lowMatrix = makeMatrix (settings);
    const auto low = runMatrix (*lowMatrix, 40.0, 1000.0, 0.0f);

    auto highMatrix = makeMatrix (settings);
    const auto high = runMatrix (*highMatrix, 9000.0, 1000.0, 0.0f);

    SF_CHECK_MSG (low.levelChangeDb() < -30.0, "a non-soloed band should be silent");
    SF_CHECK_MSG (std::abs (high.levelChangeDb()) < 0.5, "the soloed band should be untouched");
}

SF_TEST (bandMatrix_soloIsAdditiveAcrossBands)
{
    auto settings = passThroughSettings (3);
    settings.solo[0] = true;
    settings.solo[2] = true;   // both ends soloed, mid excluded

    auto lowMatrix = makeMatrix (settings);
    const auto low = runMatrix (*lowMatrix, 40.0, 1000.0, 0.0f);

    auto midMatrix = makeMatrix (settings);
    const auto mid = runMatrix (*midMatrix, 600.0, 1000.0, 0.0f);

    auto highMatrix = makeMatrix (settings);
    const auto high = runMatrix (*highMatrix, 9000.0, 1000.0, 0.0f);

    SF_CHECK_MSG (std::abs (low.levelChangeDb()) < 0.5, "a soloed band should be heard");
    SF_CHECK_MSG (std::abs (high.levelChangeDb()) < 0.5, "a second soloed band should also be heard");
    SF_CHECK_MSG (mid.levelChangeDb() < -30.0, "an unsoloed band should be silent");
}

SF_TEST (bandMatrix_soloOverridesMute)
{
    auto settings = passThroughSettings (3);
    settings.solo[0] = true;
    settings.mute[0] = true;   // contradictory: solo wins

    auto matrix = makeMatrix (settings);
    const auto low = runMatrix (*matrix, 40.0, 1000.0, 0.0f);

    SF_CHECK_MSG (std::abs (low.levelChangeDb()) < 0.5,
                  "a band that is both soloed and muted should be heard");
}

// ---------------------------------------------------------------------------
//  Latency alignment
// ---------------------------------------------------------------------------

SF_TEST (bandMatrix_latencyIsTheDeepestBandAndEveryBandIsPaddedToIt)
{
    constexpr double sr = 48000.0;

    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[0]);
    settings.bands[0].lookaheadMs = 5.0f;         // 240 samples
    makeDucker (settings.bands[1]);
    settings.bands[1].lookaheadMs = 0.0f;
    makeDucker (settings.bands[2]);
    settings.bands[2].lookaheadMs = 10.0f;        // 480 samples

    auto matrix = makeMatrix (settings, sr);

    const auto expected = (int) std::lround (10.0 * 0.001 * sr);
    SF_CHECK_MSG (matrix->getLatencySamples() == expected,
                  ("the matrix should report the deepest band's latency, got "
                   + std::to_string (matrix->getLatencySamples()) + " expected "
                   + std::to_string (expected)).c_str());

    // An impulse must come out of every band at the same instant. Feeding a
    // broadband impulse and finding the output peak proves the bands did not
    // smear apart.
    constexpr int block = 2048;
    auto aligned = makeMatrix (settings, sr, block, 1);

    juce::AudioBuffer<float> audio { 1, block }, sidechain { 1, block };
    sidechain.clear();

    std::vector<float> output;

    for (int b = 0; b < 4; ++b)
    {
        audio.clear();

        if (b == 1)
            audio.getWritePointer (0)[0] = 1.0f;

        aligned->process (audio, sidechain, nullptr, 1, block);

        for (int i = 0; i < block; ++i)
            output.push_back (audio.getReadPointer (0)[i]);
    }

    int peakIndex = 0;
    float peak = 0.0f;

    for (int i = 0; i < (int) output.size(); ++i)
        if (std::abs (output[(size_t) i]) > peak)
        {
            peak = std::abs (output[(size_t) i]);
            peakIndex = i;
        }

    const auto impulseAt = block;
    SF_CHECK_MSG (std::abs ((peakIndex - impulseAt) - expected) <= 2,
                  ("the impulse emerged " + std::to_string (peakIndex - impulseAt)
                   + " samples late; the matrix reports " + std::to_string (expected)).c_str());
}

SF_TEST (bandMatrix_switchingABandModeDoesNotShiftItAgainstTheOthers)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    // One band with lookahead sets the matrix latency. Changing another band's
    // mode must not move anything, because every band is padded to the same total.
    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[2]);
    settings.bands[2].lookaheadMs = 8.0f;

    auto matrix = makeMatrix (settings, sr, block);
    const auto before = matrix->getLatencySamples();

    for (const auto mode : { sf::BandProcessorMode::Off, sf::BandProcessorMode::Ducker,
                             sf::BandProcessorMode::Compressor, sf::BandProcessorMode::Limiter,
                             sf::BandProcessorMode::Curve })
    {
        settings.bands[0].mode = mode;
        matrix->setSettings (settings);

        SF_CHECK_MSG (matrix->getLatencySamples() == before,
                      "changing a band's mode must not change the matrix latency while "
                      "another band sets it");
    }
}

SF_TEST (bandMatrix_spectralBandRaisesTheAlignmentForEveryone)
{
    constexpr double sr = 48000.0;

    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[0]);
    settings.bands[0].lookaheadMs = 2.0f;

    settings.bands[1].mode = sf::BandProcessorMode::Spectral;
    settings.bands[1].depth = 1.0f;
    settings.bands[1].spectralFftOrder = 11;      // 2048
    settings.bands[1].spectralOverlap = 4;

    auto matrix = makeMatrix (settings, sr);

    // The spectral band's own latency is a full FFT frame, which is far deeper
    // than the other band's lookahead, so it sets the alignment.
    SF_CHECK_MSG (matrix->getLatencySamples() == 2048,
                  ("a 2048-point spectral band should set the alignment, got "
                   + std::to_string (matrix->getLatencySamples())).c_str());
}

// ---------------------------------------------------------------------------
//  Robustness
// ---------------------------------------------------------------------------

SF_TEST (bandMatrix_silenceStaysSilent)
{
    for (int mode = 0; mode < sf::kNumBandModes; ++mode)
    {
        auto settings = passThroughSettings (3);

        for (auto& band : settings.bands)
        {
            makeDucker (band);
            band.mode = (sf::BandProcessorMode) mode;
        }

        auto matrix = makeMatrix (settings);

        juce::AudioBuffer<float> audio { 2, 512 }, sidechain { 2, 512 };

        for (int b = 0; b < 30; ++b)
        {
            audio.clear();
            sidechain.clear();
            matrix->process (audio, sidechain, nullptr, 2, 512);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    SF_CHECK_MSG (std::abs (audio.getReadPointer (ch)[i]) < 1.0e-6f,
                                  "silence in must give silence out in every band mode");
        }
    }
}

SF_TEST (bandMatrix_survivesEveryModeAtEveryBandCount)
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    std::mt19937 rng { 4242 };

    for (int numBands = 1; numBands <= sf::kMaxMatrixBands; ++numBands)
    {
        for (int mode = 0; mode < sf::kNumBandModes; ++mode)
        {
            for (int scSource = 0; scSource < sf::kNumBandScSources; ++scSource)
            {
                auto settings = passThroughSettings (numBands);

                for (auto& band : settings.bands)
                {
                    makeDucker (band);
                    band.mode = (sf::BandProcessorMode) mode;
                    band.scSource = (sf::BandScSource) scSource;
                }

                auto matrix = makeMatrix (settings, sr, block);

                juce::AudioBuffer<float> audio { 2, block }, sidechain { 2, block };

                for (int b = 0; b < 24; ++b)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        fillNoise (audio, ch, 0.5f, (unsigned) rng());
                        fillNoise (sidechain, ch, 0.5f, (unsigned) rng());
                    }

                    matrix->process (audio, sidechain, nullptr, 2, block);

                    SF_CHECK_MSG (allFinite (audio),
                                  ("non-finite output: bands=" + std::to_string (numBands)
                                   + " mode=" + std::to_string (mode)
                                   + " source=" + std::to_string (scSource)).c_str());

                    for (int ch = 0; ch < 2; ++ch)
                        SF_CHECK_MSG (audio.getMagnitude (ch, 0, block) < 8.0f,
                                      "a band mode produced a runaway level");
                }
            }
        }
    }
}

SF_TEST (bandMatrix_handlesMonoAndStereo)
{
    for (const int channels : { 1, 2 })
    {
        auto settings = passThroughSettings (3);
        makeDucker (settings.bands[0]);

        auto matrix = makeMatrix (settings, 48000.0, 512, channels);
        const auto result = runMatrix (*matrix, 40.0, 60.0, 0.9f, 48000.0, 512, channels);

        SF_CHECK (result.finite);
        SF_CHECK_MSG (result.levelChangeDb() < -10.0,
                      (std::to_string (channels) + " channel(s) should still duck").c_str());
    }
}

SF_TEST (bandMatrix_handlesEverySampleRateAndBlockSize)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const int block : { 16, 64, 256, 1024 })
        {
            auto settings = passThroughSettings (3);
            makeDucker (settings.bands[0]);
            settings.bands[1].mode = sf::BandProcessorMode::Compressor;

            auto matrix = makeMatrix (settings, sr, block);

            juce::AudioBuffer<float> audio { 2, block }, sidechain { 2, block };

            for (int b = 0; b < 40; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    fillNoise (audio, ch, 0.4f, (unsigned) (b * 4 + ch));
                    fillNoise (sidechain, ch, 0.7f, (unsigned) (b * 4 + ch + 2));
                }

                matrix->process (audio, sidechain, nullptr, 2, block);

                SF_CHECK_MSG (allFinite (audio),
                              (num (sr) + " Hz / " + std::to_string (block)
                               + " samples produced non-finite output").c_str());
            }
        }
    }
}

SF_TEST (bandMatrix_survivesNonFiniteAndExtremeInput)
{
    auto settings = passThroughSettings (3);

    for (auto& band : settings.bands)
        makeDucker (band);

    auto matrix = makeMatrix (settings);

    juce::AudioBuffer<float> audio { 2, 512 }, sidechain { 2, 512 };

    const float extremes[] = { std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(),
                               1.0e12f, 1.0e-30f };

    for (const auto value : extremes)
    {
        for (int b = 0; b < 12; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::FloatVectorOperations::fill (audio.getWritePointer (ch), value, 512);
                juce::FloatVectorOperations::fill (sidechain.getWritePointer (ch), value, 512);
            }

            matrix->process (audio, sidechain, nullptr, 2, 512);
            SF_CHECK_MSG (allFinite (audio), "the matrix must never emit a non-finite sample");
        }
    }

    // ...and it recovers.
    for (int b = 0; b < 80; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fillSine (audio, ch, 40.0, 48000.0, 0.5f);
            sidechain.clear (ch, 0, 512);
        }

        matrix->process (audio, sidechain, nullptr, 2, 512);
    }

    SF_CHECK_MSG (allFinite (audio) && audio.getMagnitude (0, 0, 512) > 0.2f,
                  "the matrix must recover after extreme input");
}

SF_TEST (bandMatrix_rapidSettingsChangesStayStable)
{
    constexpr int block = 128;
    std::mt19937 rng { 31337 };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };

    auto settings = passThroughSettings (3);
    auto matrix = makeMatrix (settings, 48000.0, block);

    juce::AudioBuffer<float> audio { 2, block }, sidechain { 2, block };

    for (int b = 0; b < 600; ++b)
    {
        // Everything moving at once, every block.
        settings.numBands = 1 + (int) (unit (rng) * 3.99f);
        settings.crossoverHz[0] = 30.0f + unit (rng) * 800.0f;
        settings.crossoverHz[1] = 900.0f + unit (rng) * 5000.0f;
        settings.crossoverHz[2] = 6000.0f + unit (rng) * 10000.0f;

        for (int i = 0; i < sf::kMaxMatrixBands; ++i)
        {
            auto& band = settings.bands[i];
            band.mode = (sf::BandProcessorMode) ((int) (unit (rng) * (sf::kNumBandModes - 0.01f)));
            band.scSource = (sf::BandScSource) ((int) (unit (rng) * (sf::kNumBandScSources - 0.01f)));
            band.depth = unit (rng);
            band.rangeDb = unit (rng) * 60.0f;
            band.thresholdDb = -60.0f + unit (rng) * 60.0f;
            band.ratio = 1.0f + unit (rng) * 39.0f;
            band.attackMs = 0.05f + unit (rng) * 200.0f;
            band.releaseMs = 5.0f + unit (rng) * 2000.0f;
            band.lookaheadMs = unit (rng) * 20.0f;
            band.gainDb = -24.0f + unit (rng) * 48.0f;
            band.pan = unit (rng) * 2.0f - 1.0f;
            band.mix = unit (rng);
            band.bypass = unit (rng) < 0.2f;
            band.enabled = unit (rng) > 0.1f;
            settings.solo[i] = unit (rng) < 0.1f;
            settings.mute[i] = unit (rng) < 0.1f;
        }

        matrix->setSettings (settings);

        for (int ch = 0; ch < 2; ++ch)
        {
            fillNoise (audio, ch, 0.4f, (unsigned) rng());
            fillNoise (sidechain, ch, 0.6f, (unsigned) rng());
        }

        matrix->process (audio, sidechain, nullptr, 2, block);

        SF_CHECK_MSG (allFinite (audio), "rapid matrix automation produced a non-finite sample");

        for (int ch = 0; ch < 2; ++ch)
            SF_CHECK_MSG (audio.getMagnitude (ch, 0, block) < 64.0f,
                          "rapid matrix automation produced a runaway level");
    }
}

SF_TEST (bandMatrix_longRunIsStable)
{
    constexpr int block = 512;

    auto settings = passThroughSettings (3);
    makeDucker (settings.bands[0]);
    settings.bands[1].mode = sf::BandProcessorMode::Compressor;
    settings.bands[2].mode = sf::BandProcessorMode::Limiter;

    auto matrix = makeMatrix (settings, 48000.0, block);

    juce::AudioBuffer<float> audio { 2, block }, sidechain { 2, block };
    auto peak = 0.0f;

    // Roughly 30 seconds.
    for (int b = 0; b < 2800; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fillNoise (audio, ch, 0.4f, (unsigned) (b * 2 + ch));
            fillPulses (sidechain, ch, 24000, 480, 0.9f, b * block);
        }

        matrix->process (audio, sidechain, nullptr, 2, block);

        SF_CHECK (allFinite (audio));
        peak = juce::jmax (peak, audio.getMagnitude (0, 0, block));
    }

    SF_CHECK_MSG (peak < 4.0f, ("a long run built up to " + num (peak)).c_str());
    SF_CHECK_MSG (peak > 0.01f, "a long run decayed into silence");
}
