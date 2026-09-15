#include "../TestHelpers.h"
#include "Model/FactoryPresets.h"
#include "Parameters/BandMatrixParameters.h"
#include <array>
#include <limits>

using namespace sft;

namespace
{
/** Every per-band parameter ID, so a test can sweep all of them without
    re-typing the list each time. */
std::array<const char*, 25> bandIds (int band)
{
    const auto& ids = sf::pid::band[band];

    return { ids.mode, ids.enable, ids.bypass, ids.solo, ids.mute, ids.depth, ids.range,
             ids.threshold, ids.ratio, ids.knee, ids.attack, ids.hold, ids.release,
             ids.autoRelease, ids.detector, ids.lookahead, ids.makeup, ids.gain, ids.pan,
             ids.mix, ids.scSource, ids.scFilter, ids.scHighPass, ids.scLowPass,
             ids.scListen };
}

void setParameter (sf::SideForgeProcessor& p, const char* id, float value)
{
    auto* parameter = p.getAPVTS().getParameter (id);
    SF_CHECK_MSG (parameter != nullptr, std::string ("unknown parameter: ") + id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void setNormalised (sf::SideForgeProcessor& p, const char* id, float normalised)
{
    if (auto* parameter = p.getAPVTS().getParameter (id))
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
}

/** A tone at `toneHz` on the main bus and a kick-like pulse train on the sidechain. */
void fillBuffer (juce::AudioBuffer<float>& buffer, int mainChannels, int scChannels,
                 double toneHz, double sampleRate, int offset, float sidechainAmp = 0.9f)
{
    buffer.clear();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto n = offset + i;
        const auto tone = 0.4f * (float) std::sin (2.0 * kPi * toneHz * n / sampleRate);

        for (int ch = 0; ch < mainChannels && ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (ch)[i] = tone;

        const auto period = juce::jmax (1, (int) (sampleRate * 0.5));
        const auto length = juce::jmax (1, (int) (sampleRate * 0.02));
        const auto position = n % period;
        const auto pulse = position < length
                               ? sidechainAmp * (1.0f - (float) position / (float) length)
                               : 0.0f;

        for (int ch = 0; ch < scChannels && mainChannels + ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (mainChannels + ch)[i] = pulse;
    }
}

struct Measurement
{
    double rms = 0.0;      ///< across the whole settled region
    double quietest = 0.0; ///< the quietest short window in it
};

/**
    Runs the processor and measures the main output.

    Both figures matter and they answer different questions. A sidechain pulse
    ducks for a few tens of milliseconds out of every half second, so a long RMS
    barely moves even when the duck is total - that figure is the right one for
    "was this band left alone". The quietest short window is the right one for
    "did this band actually duck".
*/
Measurement runAndMeasure (sf::SideForgeProcessor& p, double toneHz, int blocks = 160,
                           int settleBlocks = 110, int block = 512, double sampleRate = 48000.0,
                           float sidechainAmp = 0.9f)
{
    constexpr int kWindow = 256;

    juce::AudioBuffer<float> buffer { p.getTotalNumInputChannels(), block };
    juce::MidiBuffer midi;

    double sum = 0.0;
    int counted = 0;
    auto quietest = std::numeric_limits<double>::max();

    for (int b = 0; b < blocks; ++b)
    {
        fillBuffer (buffer, 2, 2, toneHz, sampleRate, b * block, sidechainAmp);
        p.processBlock (buffer, midi);

        SF_CHECK_MSG (allFinite (buffer), "the processor emitted a non-finite sample");

        if (b < settleBlocks)
            continue;

        for (int i = 0; i < block; ++i)
        {
            const auto v = (double) buffer.getReadPointer (0)[i];
            sum += v * v;
            ++counted;
        }

        for (int start = 0; start + kWindow <= block; start += kWindow)
        {
            double windowSum = 0.0;

            for (int i = start; i < start + kWindow; ++i)
            {
                const auto v = (double) buffer.getReadPointer (0)[i];
                windowSum += v * v;
            }

            quietest = juce::jmin (quietest, std::sqrt (windowSum / kWindow));
        }
    }

    return { std::sqrt (sum / juce::jmax (1, counted)),
             quietest == std::numeric_limits<double>::max() ? 0.0 : quietest };
}

double changeDb (double with, double without)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-9, with) / juce::jmax (1.0e-9, without));
}

/** Captures the output of the processor for a fixed input, as samples. */
std::vector<float> capture (sf::SideForgeProcessor& p, int blocks = 40, int block = 512)
{
    juce::AudioBuffer<float> buffer { p.getTotalNumInputChannels(), block };
    juce::MidiBuffer midi;
    std::vector<float> output;

    for (int b = 0; b < blocks; ++b)
    {
        fillBuffer (buffer, 2, 2, 220.0, 48000.0, b * block);
        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                output.push_back (buffer.getReadPointer (ch)[i]);
    }

    return output;
}

/** Puts the matrix into a three-band "hard lows, light highs" configuration. */
void configureHardLowsLightHighs (sf::SideForgeProcessor& p)
{
    setParameter (p, sf::pid::routingMode, (float) sf::RoutingMode::BandMatrix);
    setParameter (p, sf::pid::bmBandCount, 2.0f);        // three bands
    setParameter (p, sf::pid::bmXover1, 120.0f);
    setParameter (p, sf::pid::bmXover2, 2500.0f);

    setParameter (p, sf::pid::band[0].mode, (float) sf::BandProcessorMode::Ducker);
    setParameter (p, sf::pid::band[0].depth, 100.0f);
    setParameter (p, sf::pid::band[0].range, 24.0f);
    setParameter (p, sf::pid::band[0].threshold, -40.0f);
    setParameter (p, sf::pid::band[0].attack, 0.5f);
    setParameter (p, sf::pid::band[0].release, 60.0f);

    setParameter (p, sf::pid::band[1].mode, (float) sf::BandProcessorMode::Off);
    setParameter (p, sf::pid::band[2].mode, (float) sf::BandProcessorMode::Off);
}
} // namespace

// ---------------------------------------------------------------------------
//  Full Range must be exactly as it was
// ---------------------------------------------------------------------------

SF_TEST (matrixProcessor_fullRangeIsTheDefault)
{
    auto p = makeProcessor();

    SF_CHECK_MSG (p->getRoutingMode() == sf::RoutingMode::FullRange,
                  "a fresh instance must come up in Full Range");
}

SF_TEST (matrixProcessor_fullRangeOutputIsUnaffectedByEveryMatrixParameter)
{
    auto reference = makeProcessor();
    const auto before = capture (*reference);

    auto modified = makeProcessor();

    // Move every single Band Matrix parameter to something other than default,
    // while leaving Routing on Full Range.
    std::mt19937 rng { 20260914 };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };

    setNormalised (*modified, sf::pid::bmBandCount, 1.0f);
    setNormalised (*modified, sf::pid::bmXover1, unit (rng));
    setNormalised (*modified, sf::pid::bmXover2, unit (rng));
    setNormalised (*modified, sf::pid::bmXover3, unit (rng));
    setNormalised (*modified, sf::pid::bmPunch, unit (rng));
    setNormalised (*modified, sf::pid::bmRecovery, unit (rng));

    for (int b = 0; b < sf::kMaxMatrixBands; ++b)
        for (const auto* id : bandIds (b))
            setNormalised (*modified, id, unit (rng));

    const auto after = capture (*modified);

    SF_CHECK (before.size() == after.size());

    for (size_t i = 0; i < before.size(); ++i)
        SF_CHECK_MSG (before[i] == after[i],
                      "a Band Matrix parameter changed the Full Range output");
}

SF_TEST (matrixProcessor_fullRangeReportsNoExtraLatency)
{
    auto p = makeProcessor();

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int b = 0; b < 4; ++b)
    {
        buffer.clear();
        p->processBlock (buffer, midi);
    }

    SF_CHECK_MSG (p->getReportedLatencySamples() == 0,
                  "Full Range with no lookahead should still report zero latency");
}

// ---------------------------------------------------------------------------
//  Band Matrix through the whole processor
// ---------------------------------------------------------------------------

SF_TEST (matrixProcessor_switchingToBandMatrixWithEveryBandOffIsTransparent)
{
    for (int bandCount = 0; bandCount < sf::kMaxMatrixBands; ++bandCount)
    {
        auto reference = makeProcessor();
        const auto fullRange = runAndMeasure (*reference, 600.0, 160, 110, 512, 48000.0, 0.0f);

        auto matrix = makeProcessor();
        setParameter (*matrix, sf::pid::routingMode, (float) sf::RoutingMode::BandMatrix);
        setParameter (*matrix, sf::pid::bmBandCount, (float) bandCount);

        for (int b = 0; b < sf::kMaxMatrixBands; ++b)
            setParameter (*matrix, sf::pid::band[b].mode, (float) sf::BandProcessorMode::Off);

        const auto through = runAndMeasure (*matrix, 600.0, 160, 110, 512, 48000.0, 0.0f);

        const auto change = changeDb (through.rms, fullRange.rms);

        SF_CHECK_MSG (std::abs (change) < 0.5,
                      (std::to_string (bandCount + 1) + " unprocessed bands changed the level by "
                       + num (change) + " dB").c_str());
    }
}

SF_TEST (matrixProcessor_hardLowsLeaveTheHighsAlone)
{
    auto lowProbe = makeProcessor();
    configureHardLowsLightHighs (*lowProbe);
    const auto lowWith = runAndMeasure (*lowProbe, 50.0);

    auto lowReference = makeProcessor();
    configureHardLowsLightHighs (*lowReference);
    const auto lowWithout = runAndMeasure (*lowReference, 50.0, 160, 110, 512, 48000.0, 0.0f);

    auto highProbe = makeProcessor();
    configureHardLowsLightHighs (*highProbe);
    const auto highWith = runAndMeasure (*highProbe, 8000.0);

    auto highReference = makeProcessor();
    configureHardLowsLightHighs (*highReference);
    const auto highWithout = runAndMeasure (*highReference, 8000.0, 160, 110, 512, 48000.0, 0.0f);

    // The low band is judged while the duck is happening; a half-second average
    // would hide a total duck that only lasts 80 ms.
    const auto lowDuck = changeDb (lowWith.quietest, lowWithout.quietest);

    // The high band is judged on the whole window *and* on its quietest moment,
    // because "untouched" has to mean untouched at the instant the kick lands,
    // not just on average.
    const auto highAverage = changeDb (highWith.rms, highWithout.rms);
    const auto highWorst = changeDb (highWith.quietest, highWithout.quietest);

    SF_CHECK_MSG (lowDuck < -12.0,
                  ("the low band should duck hard through the processor, got " + num (lowDuck)
                   + " dB").c_str());
    SF_CHECK_MSG (std::abs (highAverage) < 0.6,
                  ("the high band should be untouched, got " + num (highAverage) + " dB").c_str());
    SF_CHECK_MSG (std::abs (highWorst) < 0.6,
                  ("the high band dipped when the kick landed, by " + num (highWorst)
                   + " dB").c_str());
}

SF_TEST (matrixProcessor_reportsAndCompensatesBandLatency)
{
    auto p = makeProcessor();
    setParameter (*p, sf::pid::routingMode, (float) sf::RoutingMode::BandMatrix);
    setParameter (*p, sf::pid::bmBandCount, 2.0f);

    for (int b = 0; b < sf::kMaxMatrixBands; ++b)
        setParameter (*p, sf::pid::band[b].mode, (float) sf::BandProcessorMode::Off);

    // One band with lookahead sets the whole matrix's latency.
    setParameter (*p, sf::pid::band[0].mode, (float) sf::BandProcessorMode::Ducker);
    setParameter (*p, sf::pid::band[0].lookahead, 5.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int b = 0; b < 8; ++b)
    {
        buffer.clear();
        p->processBlock (buffer, midi);
    }

    const auto expected = (int) std::lround (5.0 * 0.001 * 48000.0);
    SF_CHECK_MSG (p->getReportedLatencySamples() == expected,
                  ("the processor should report the matrix latency, got "
                   + std::to_string (p->getReportedLatencySamples()) + " expected "
                   + std::to_string (expected)).c_str());

    // With bypass engaged the output is the input delayed by exactly that, which
    // is what proves the dry path is aligned to the matrix and not to the old
    // full-range latency.
    setParameter (*p, sf::pid::bypass, 1.0f);

    for (int b = 0; b < 40; ++b)
    {
        fillBuffer (buffer, 2, 2, 200.0, 48000.0, b * 512);
        p->processBlock (buffer, midi);
    }

    std::vector<float> input, output;

    for (int b = 0; b < 8; ++b)
    {
        fillBuffer (buffer, 2, 2, 200.0, 48000.0, (40 + b) * 512);

        for (int i = 0; i < 512; ++i)
            input.push_back (buffer.getReadPointer (0)[i]);

        p->processBlock (buffer, midi);

        for (int i = 0; i < 512; ++i)
            output.push_back (buffer.getReadPointer (0)[i]);
    }

    for (size_t i = (size_t) expected; i < output.size(); ++i)
        SF_CHECK_NEAR (output[i], input[i - (size_t) expected], 1.0e-4);
}

SF_TEST (matrixProcessor_deltaShowsWhatTheMatrixRemoved)
{
    auto p = makeProcessor();
    configureHardLowsLightHighs (*p);
    setParameter (*p, sf::pid::monitor, (float) sf::Monitor::Delta);

    const auto delta = runAndMeasure (*p, 50.0);

    SF_CHECK_MSG (delta.rms > 0.02,
                  ("delta should expose the removed low end, got " + num (delta.rms)).c_str());
}

SF_TEST (matrixProcessor_worksWithoutASidechainBus)
{
    auto p = makeProcessor (48000.0, 512, 2, 0);
    configureHardLowsLightHighs (*p);

    // Internal detection, since there is no sidechain to listen to.
    setParameter (*p, sf::pid::band[0].scSource, (float) sf::BandScSource::InternalBand);
    setParameter (*p, sf::pid::band[0].threshold, -50.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int b = 0; b < 80; ++b)
    {
        fillBuffer (buffer, 2, 0, 50.0, 48000.0, b * 512);
        p->processBlock (buffer, midi);
        SF_CHECK_MSG (allFinite (buffer), "the matrix must survive a missing sidechain bus");
    }
}

SF_TEST (matrixProcessor_handlesMonoAndEverySampleRate)
{
    for (const int mainChannels : { 1, 2 })
    {
        for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            for (const int block : { 32, 256, 1024 })
            {
                auto p = makeProcessor (sr, block, mainChannels, mainChannels);
                configureHardLowsLightHighs (*p);

                juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), block };
                juce::MidiBuffer midi;

                for (int b = 0; b < 30; ++b)
                {
                    fillBuffer (buffer, mainChannels, mainChannels, 50.0, sr, b * block);
                    p->processBlock (buffer, midi);

                    SF_CHECK_MSG (allFinite (buffer),
                                  (std::to_string (mainChannels) + "ch " + num (sr) + " Hz block "
                                   + std::to_string (block) + " gave non-finite output").c_str());
                }
            }
        }
    }
}

SF_TEST (matrixProcessor_switchingRoutingMidStreamIsStable)
{
    auto p = makeProcessor();
    configureHardLowsLightHighs (*p);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 256 };
    juce::MidiBuffer midi;

    auto worstJump = 0.0f;
    auto previous = 0.0f;
    auto first = true;

    for (int b = 0; b < 400; ++b)
    {
        // Flip between the two signal paths every 20 blocks.
        if (b % 20 == 0)
            setParameter (*p, sf::pid::routingMode,
                          (float) ((b / 20) % 2 == 0 ? sf::RoutingMode::FullRange
                                                     : sf::RoutingMode::BandMatrix));

        buffer.clear();

        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.3f, 256);

        p->processBlock (buffer, midi);
        SF_CHECK (allFinite (buffer));

        for (int i = 0; i < 256; ++i)
        {
            const auto v = buffer.getReadPointer (0)[i];

            if (! first)
                worstJump = juce::jmax (worstJump, std::abs (v - previous));

            previous = v;
            first = false;
        }
    }

    // A routing change switches signal paths, so it is not sample-continuous the
    // way a smoothed parameter is — but it must not produce a full-scale step.
    SF_CHECK_MSG (worstJump < 0.6f,
                  ("switching routing produced a jump of " + num (worstJump)).c_str());
}

// ---------------------------------------------------------------------------
//  State and presets
// ---------------------------------------------------------------------------

SF_TEST (matrixProcessor_stateRoundTripsEveryMatrixParameter)
{
    auto source = makeProcessor();
    std::mt19937 rng { 77 };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };

    setNormalised (*source, sf::pid::routingMode, 1.0f);

    for (int b = 0; b < sf::kMaxMatrixBands; ++b)
        for (const auto* id : bandIds (b))
            setNormalised (*source, id, unit (rng));

    setNormalised (*source, sf::pid::bmXover1, unit (rng));
    setNormalised (*source, sf::pid::bmXover2, unit (rng));
    setNormalised (*source, sf::pid::bmXover3, unit (rng));
    setNormalised (*source, sf::pid::bmPunch, unit (rng));
    setNormalised (*source, sf::pid::bmRecovery, unit (rng));

    juce::MemoryBlock state;
    source->getStateInformation (state);

    auto restored = makeProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());

    for (auto* parameter : source->getAPVTS().processor.getParameters())
    {
        auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);

        if (withID == nullptr)
            continue;

        auto* other = restored->getAPVTS().getParameter (withID->paramID);
        SF_CHECK_MSG (other != nullptr, "every parameter must survive a state round trip");

        // The *value* is what has to survive, not the normalised float behind
        // it: a bool parameter set to 0.03 reads back as 0.0, which is the same
        // "Off" and a faithful round trip.
        SF_CHECK_MSG (other->getCurrentValueAsText() == parameter->getCurrentValueAsText(),
                      (withID->paramID.toStdString() + " came back as "
                       + other->getCurrentValueAsText().toStdString() + " instead of "
                       + parameter->getCurrentValueAsText().toStdString()).c_str());
    }

    SF_CHECK (restored->getRoutingMode() == sf::RoutingMode::BandMatrix);
}

SF_TEST (matrixProcessor_stateFromBeforeTheBandMatrixLoadsInFullRange)
{
    // A version-1 tree: the wrapper, the APVTS child and a curve, with no Band
    // Matrix entries at all — exactly what an older session contains.
    auto source = makeProcessor();
    setParameter (*source, sf::pid::compThreshold, -17.0f);
    setParameter (*source, sf::pid::mode, (float) sf::Mode::Curve);

    auto tree = source->captureState();
    tree.setProperty (sf::PresetState::versionProp, 1, nullptr);

    // Strip every Band Matrix parameter from the saved APVTS child, so the tree
    // genuinely looks like one written before the feature existed.
    auto params = tree.getChildWithName (sf::PresetState::paramsTag);
    SF_CHECK (params.isValid());

    int removed = 0;

    for (int i = params.getNumChildren(); --i >= 0;)
    {
        const auto id = params.getChild (i).getProperty ("id").toString();

        if (id.startsWith ("bm") || id == sf::pid::routingMode)
        {
            params.removeChild (i, nullptr);
            ++removed;
        }
    }

    SF_CHECK_MSG (removed > 100, "the test should have stripped the whole Band Matrix");

    auto restored = makeProcessor();
    SF_CHECK (restored->applyState (tree));

    SF_CHECK_MSG (restored->getRoutingMode() == sf::RoutingMode::FullRange,
                  "an old session must come back in Full Range");
    SF_CHECK_NEAR (getParam (*restored, sf::pid::compThreshold), -17.0, 0.01);
    SF_CHECK_NEAR (getParam (*restored, sf::pid::mode), (float) sf::Mode::Curve, 0.01);

    // ...and it still processes.
    juce::AudioBuffer<float> buffer { restored->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int b = 0; b < 20; ++b)
    {
        fillBuffer (buffer, 2, 2, 220.0, 48000.0, b * 512);
        restored->processBlock (buffer, midi);
        SF_CHECK (allFinite (buffer));
    }
}

SF_TEST (matrixProcessor_malformedMatrixStateIsClampedNotTrusted)
{
    auto p = makeProcessor();

    auto tree = p->captureState();
    auto params = tree.getChildWithName (sf::PresetState::paramsTag);

    // Shove nonsense into the crossovers and a band mode.
    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        auto child = params.getChild (i);
        const auto id = child.getProperty ("id").toString();

        if (id == sf::pid::bmXover1 || id == sf::pid::bmXover2 || id == sf::pid::bmXover3)
            child.setProperty ("value", 1.0e9, nullptr);
        else if (id == sf::pid::band[0].mode)
            child.setProperty ("value", 999.0, nullptr);
        else if (id == sf::pid::routingMode)
            child.setProperty ("value", (double) (int) sf::RoutingMode::BandMatrix, nullptr);
    }

    SF_CHECK (p->applyState (tree));

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int b = 0; b < 40; ++b)
    {
        fillBuffer (buffer, 2, 2, 220.0, 48000.0, b * 512);
        p->processBlock (buffer, midi);
        SF_CHECK_MSG (allFinite (buffer), "malformed matrix state must not destabilise anything");
    }

    // The crossovers came back inside their ranges and in order.
    auto& matrix = p->getBandMatrix();

    for (int i = 0; i < sf::kMaxCrossovers; ++i)
    {
        SF_CHECK_FINITE (matrix.getCrossoverHz (i));
        SF_CHECK (matrix.getCrossoverHz (i) > 0.0f && matrix.getCrossoverHz (i) < 48000.0f * 0.5f);
    }
}

SF_TEST (matrixProcessor_everyPresetIncludingTheMatrixOnesLoadsAndProcesses)
{
    SF_CHECK_MSG (sf::FactoryPresets::getNumPresets() >= 23,
                  "the Band Matrix presets should have been added");

    auto matrixPresets = 0;

    for (int i = 0; i < sf::FactoryPresets::getNumPresets(); ++i)
    {
        auto p = makeProcessor();
        p->setCurrentProgram (i);

        if (p->getRoutingMode() == sf::RoutingMode::BandMatrix)
            ++matrixPresets;

        juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
        juce::MidiBuffer midi;

        for (int b = 0; b < 40; ++b)
        {
            fillBuffer (buffer, 2, 2, 220.0, 48000.0, b * 512);
            p->processBlock (buffer, midi);

            SF_CHECK_MSG (allFinite (buffer),
                          ("preset '" + sf::FactoryPresets::getName (i).toStdString()
                           + "' produced non-finite output").c_str());

            for (int ch = 0; ch < 2; ++ch)
                SF_CHECK_MSG (buffer.getMagnitude (ch, 0, 512) < 8.0f,
                              ("preset '" + sf::FactoryPresets::getName (i).toStdString()
                               + "' produced a runaway level").c_str());
        }
    }

    SF_CHECK_MSG (matrixPresets >= 13,
                  ("expected the Band Matrix presets to select Band Matrix routing, got "
                   + std::to_string (matrixPresets)).c_str());
}
