#include "../TestHelpers.h"
#include "Model/FactoryPresets.h"

using namespace sft;

namespace
{
/** Fills the main bus with a signal and the sidechain bus with pulses. */
void fillProcessorBuffer (juce::AudioBuffer<float>& buffer, int mainChannels, int scChannels,
                          double sampleRate, int pulsePeriod, int offset = 0)
{
    for (int ch = 0; ch < mainChannels; ++ch)
        fillSine (buffer, ch, 220.0 + 110.0 * ch, sampleRate, 0.4f,
                  2.0 * kPi * 220.0 * offset / sampleRate);

    for (int ch = 0; ch < scChannels; ++ch)
        fillPulses (buffer, mainChannels + ch, pulsePeriod, (int) (0.01 * sampleRate), 0.9f, offset);
}

/** Runs the processor for `blocks` blocks, returning the peak output magnitude. */
float runBlocks (sf::SideForgeProcessor& p, int blocks, int blockSize, double sampleRate,
                 int mainChannels, int scChannels, bool* sawNonFinite = nullptr)
{
    juce::AudioBuffer<float> buffer { p.getTotalNumInputChannels(), blockSize };
    juce::MidiBuffer midi;
    float peak = 0.0f;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        fillProcessorBuffer (buffer, mainChannels, scChannels, sampleRate,
                             (int) (0.5 * sampleRate), b * blockSize);

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < mainChannels; ++ch)
        {
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, blockSize));

            if (sawNonFinite != nullptr)
                for (int i = 0; i < blockSize; ++i)
                    if (! std::isfinite (buffer.getReadPointer (ch)[i]))
                        *sawNonFinite = true;
        }
    }

    return peak;
}

void setChoice (sf::SideForgeProcessor& p, const char* id, int index)
{
    auto* param = p.getAPVTS().getParameter (id);
    SF_CHECK_MSG (param != nullptr, std::string ("unknown parameter: ") + id);
    param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
}
} // namespace

// ---------------------------------------------------------------------------
//  Basic contracts
// ---------------------------------------------------------------------------

SF_TEST (processor_silenceProducesSilence)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int mode = 0; mode < 4; ++mode)
    {
        setChoice (*p, sf::pid::mode, mode);
        setChoice (*p, sf::pid::quality, 1);

        for (int b = 0; b < 40; ++b)
        {
            buffer.clear();
            p->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const auto v = buffer.getReadPointer (ch)[i];
                    SF_CHECK_FINITE (v);
                    SF_CHECK_MSG (std::abs (v) < 1.0e-6f, "silence in must give silence out");
                }
        }
    }
}

SF_TEST (processor_passesAudioUnchangedAtDefaultSettingsWithNoSidechain)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::AudioBuffer<float> original { 2, 512 };
    juce::MidiBuffer midi;

    // Default mode is Compress with a -20 dB threshold; a silent sidechain means
    // the detector never opens, so the audio must come out untouched.
    for (int b = 0; b < 8; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            fillSine (buffer, ch, 440.0, 48000.0, 0.3f);
            original.copyFrom (ch, 0, buffer, ch, 0, 512);
        }

        p->processBlock (buffer, midi);
    }

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            SF_CHECK_NEAR (buffer.getReadPointer (ch)[i], original.getReadPointer (ch)[i], 1.0e-4);
}

SF_TEST (processor_survivesWithNoSidechainBus)
{
    auto p = makeProcessor (48000.0, 512, 2, 0);

    SF_CHECK_MSG (! p->isSidechainConnected(), "a disabled bus must report as not connected");

    bool nonFinite = false;

    for (int mode = 0; mode < 4; ++mode)
    {
        setChoice (*p, sf::pid::mode, mode);
        runBlocks (*p, 20, 512, 48000.0, 2, 0, &nonFinite);
    }

    SF_CHECK_MSG (! nonFinite, "output must stay finite with no sidechain bus at all");
}

SF_TEST (processor_reportsWhetherTheSidechainIsConnectedAndCarryingSignal)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);
    SF_CHECK (p->isSidechainConnected());

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    // Connected but silent.
    for (int b = 0; b < 4; ++b)
    {
        buffer.clear();
        p->processBlock (buffer, midi);
    }

    SF_CHECK_MSG (! p->isSidechainReceivingAudio(), "a silent sidechain is connected but not active");

    // Connected and carrying audio.
    for (int b = 0; b < 4; ++b)
    {
        buffer.clear();
        fillSine (buffer, 2, 100.0, 48000.0, 0.5f);
        fillSine (buffer, 3, 100.0, 48000.0, 0.5f);
        p->processBlock (buffer, midi);
    }

    SF_CHECK (p->isSidechainReceivingAudio());
}

SF_TEST (processor_bypassIsTransparentAndClickFree)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    setChoice (*p, sf::pid::mode, (int) sf::Mode::Curve);
    setParam (*p, sf::pid::curveDepth, 100.0f);
    setParam (*p, sf::pid::bypass, 1.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::AudioBuffer<float> original { 2, 512 };
    juce::MidiBuffer midi;

    // Let the bypass crossfade settle.
    for (int b = 0; b < 20; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            fillSine (buffer, ch, 440.0, 48000.0, 0.4f);
        p->processBlock (buffer, midi);
    }

    // Now it must be sample-exact.
    for (int b = 0; b < 8; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            fillSine (buffer, ch, 440.0, 48000.0, 0.4f);
            original.copyFrom (ch, 0, buffer, ch, 0, 512);
        }

        p->processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                SF_CHECK_NEAR (buffer.getReadPointer (ch)[i], original.getReadPointer (ch)[i], 1.0e-5);
    }
}

SF_TEST (processor_togglingBypassDoesNotClick)
{
    auto p = makeProcessor (48000.0, 256, 2, 2);

    setChoice (*p, sf::pid::mode, (int) sf::Mode::Curve);
    setParam (*p, sf::pid::curveDepth, 100.0f);
    setParam (*p, sf::pid::curveSmooth, 0.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 256 };
    juce::MidiBuffer midi;

    float previous = 0.0f;
    bool first = true;
    float worstJump = 0.0f;

    for (int b = 0; b < 200; ++b)
    {
        if (b % 20 == 0)
            setParam (*p, sf::pid::bypass, (b / 20) % 2 == 0 ? 1.0f : 0.0f);

        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 256);

        p->processBlock (buffer, midi);

        for (int i = 0; i < 256; ++i)
        {
            const auto v = buffer.getReadPointer (0)[i];
            SF_CHECK_FINITE (v);

            if (! first)
                worstJump = juce::jmax (worstJump, std::abs (v - previous));

            previous = v;
            first = false;
        }
    }

    // A DC input through a smoothed bypass should never step more than a fraction
    // of full scale between adjacent samples.
    SF_CHECK_MSG (worstJump < 0.02f,
                  ("largest sample-to-sample jump was " + num (worstJump)).c_str());
}

// ---------------------------------------------------------------------------
//  Layouts
// ---------------------------------------------------------------------------

SF_TEST (processor_handlesEverySupportedLayout)
{
    struct Layout { int main; int sc; };

    for (const auto& layout : { Layout { 1, 0 }, Layout { 1, 1 }, Layout { 1, 2 },
                                Layout { 2, 0 }, Layout { 2, 1 }, Layout { 2, 2 } })
    {
        auto p = makeProcessor (48000.0, 512, layout.main, layout.sc);
        bool nonFinite = false;

        for (int mode = 0; mode < 4; ++mode)
        {
            setChoice (*p, sf::pid::mode, mode);
            runBlocks (*p, 12, 512, 48000.0, layout.main, layout.sc, &nonFinite);
        }

        SF_CHECK_MSG (! nonFinite,
                      ("layout " + std::to_string (layout.main) + "/" + std::to_string (layout.sc)
                       + " produced non-finite output").c_str());
    }
}

SF_TEST (processor_handlesEverySampleRateAndBlockSize)
{
    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (const int blockSize : { 16, 64, 128, 441, 512, 1024, 4096 })
        {
            auto p = makeProcessor (sr, blockSize, 2, 2);
            setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);
            setChoice (*p, sf::pid::quality, (int) sf::Quality::Studio);

            bool nonFinite = false;
            runBlocks (*p, 8, blockSize, sr, 2, 2, &nonFinite);

            SF_CHECK_MSG (! nonFinite,
                          (num (sr) + " Hz / " + std::to_string (blockSize)
                           + " samples produced non-finite output").c_str());
        }
    }
}

SF_TEST (processor_handlesABlockLargerThanItWasPreparedFor)
{
    auto p = makeProcessor (48000.0, 256, 2, 2);
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);

    // Eight times the promised block size: the processor slices internally.
    constexpr int oversized = 8192;
    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), oversized };
    juce::MidiBuffer midi;

    for (int b = 0; b < 4; ++b)
    {
        buffer.clear();
        fillProcessorBuffer (buffer, 2, 2, 48000.0, 24000, b * oversized);
        p->processBlock (buffer, midi);

        SF_CHECK_MSG (allFinite (buffer), "an oversized block must still produce finite output");
    }
}

SF_TEST (processor_handlesVaryingBlockSizesWithinOneRun)
{
    auto p = makeProcessor (48000.0, 2048, 2, 2);
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);

    juce::MidiBuffer midi;
    std::mt19937 rng { 77 };
    std::uniform_int_distribution<int> sizeDist { 1, 2048 };

    for (int b = 0; b < 120; ++b)
    {
        const auto n = sizeDist (rng);
        juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), n };
        buffer.clear();
        fillProcessorBuffer (buffer, 2, 2, 48000.0, 24000, b * 512);

        p->processBlock (buffer, midi);
        SF_CHECK (allFinite (buffer));
    }
}

// ---------------------------------------------------------------------------
//  Engines
// ---------------------------------------------------------------------------

SF_TEST (processor_everyModeActuallyProcessesAudio)
{
    struct Case { sf::Mode mode; sf::Quality quality; const char* name; };

    for (const auto& c : { Case { sf::Mode::Compress, sf::Quality::Studio,   "Compress" },
                           Case { sf::Mode::Curve,    sf::Quality::Live,     "Curve" },
                           Case { sf::Mode::Spectral, sf::Quality::Spectral, "Spectral" },
                           Case { sf::Mode::Hybrid,   sf::Quality::Spectral, "Hybrid" } })
    {
        auto p = makeProcessor (48000.0, 512, 2, 2);

        setChoice (*p, sf::pid::mode, (int) c.mode);
        setChoice (*p, sf::pid::quality, (int) c.quality);
        setParam (*p, sf::pid::compThreshold, -45.0f);
        setParam (*p, sf::pid::compRatio, 10.0f);
        setParam (*p, sf::pid::curveDepth, 100.0f);
        setParam (*p, sf::pid::specDepth, 30.0f);
        setParam (*p, sf::pid::specBlend, 100.0f);
        setParam (*p, sf::pid::scHighPass, 20.0f);
        setParam (*p, sf::pid::scLowPass, 20000.0f);

        // The default curve is flat, so Curve mode would correctly do nothing.
        p->getCurveModel().setPresetShape (1);   // Classic Pump
        p->curveChanged();

        juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
        juce::MidiBuffer midi;

        float minPeak = 1.0e9f, maxPeak = 0.0f;

        for (int b = 0; b < 200; ++b)
        {
            buffer.clear();

            for (int ch = 0; ch < 2; ++ch)
                fillSine (buffer, ch, 500.0, 48000.0, 0.5f);

            // A steady, loud sidechain on the same frequency.
            for (int ch = 0; ch < 2; ++ch)
                fillSine (buffer, 2 + ch, 500.0, 48000.0, 0.8f);

            p->processBlock (buffer, midi);

            if (b > 100)
            {
                const auto peak = buffer.getMagnitude (0, 0, 512);
                minPeak = juce::jmin (minPeak, peak);
                maxPeak = juce::jmax (maxPeak, peak);
            }
        }

        SF_CHECK_FINITE (minPeak);
        SF_CHECK_MSG (minPeak < 0.45f,
                      (std::string (c.name) + " mode did not attenuate anything (min peak "
                       + num (minPeak) + ")").c_str());
    }
}

SF_TEST (processor_compressorReducesGainWhenTheSidechainIsLoud)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    setChoice (*p, sf::pid::mode, (int) sf::Mode::Compress);
    setChoice (*p, sf::pid::compDetector, (int) sf::Detector::Peak);
    setParam (*p, sf::pid::compThreshold, -40.0f);
    setParam (*p, sf::pid::compRatio, 20.0f);
    setParam (*p, sf::pid::compRange, 60.0f);
    setParam (*p, sf::pid::compAttack, 0.1f);
    setParam (*p, sf::pid::compRelease, 10.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    float withSilentSidechain = 0.0f, withLoudSidechain = 0.0f;

    for (int b = 0; b < 60; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            fillSine (buffer, ch, 440.0, 48000.0, 0.5f);

        p->processBlock (buffer, midi);
        withSilentSidechain = buffer.getMagnitude (0, 0, 512);
    }

    for (int b = 0; b < 60; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            fillSine (buffer, ch, 440.0, 48000.0, 0.5f);
            fillSine (buffer, 2 + ch, 100.0, 48000.0, 0.9f);
        }

        p->processBlock (buffer, midi);
        withLoudSidechain = buffer.getMagnitude (0, 0, 512);
    }

    SF_CHECK_NEAR (withSilentSidechain, 0.5, 0.02);
    SF_CHECK_MSG (withLoudSidechain < withSilentSidechain * 0.2f,
                  ("a loud sidechain should duck hard: " + num (withLoudSidechain)
                   + " vs " + num (withSilentSidechain)).c_str());
    SF_CHECK_MSG (p->getMeters().gainReduction.getHeldDb() > 6.0f,
                  "the gain-reduction meter should register the ducking");
}

SF_TEST (processor_curveModeDucksWithoutAnySidechain)
{
    auto p = makeProcessor (48000.0, 512, 2, 0);

    setChoice (*p, sf::pid::mode, (int) sf::Mode::Curve);
    setChoice (*p, sf::pid::curveSyncMode, (int) sf::SyncMode::Free);
    setChoice (*p, sf::pid::curveTrigMode, (int) sf::TrigMode::Loop);
    setParam (*p, sf::pid::curveRateHz, 4.0f);
    setParam (*p, sf::pid::curveDepth, 100.0f);

    p->getCurveModel().setPresetShape (1);   // Classic Pump
    p->curveChanged();

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    float lowest = 1.0e9f, highest = 0.0f;

    for (int b = 0; b < 100; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 512);

        p->processBlock (buffer, midi);

        for (int i = 0; i < 512; ++i)
        {
            const auto v = std::abs (buffer.getReadPointer (0)[i]);
            lowest = juce::jmin (lowest, v);
            highest = juce::jmax (highest, v);
        }
    }

    SF_CHECK_MSG (lowest < 0.05f, "the pump curve should reach near-silence");
    SF_CHECK_MSG (highest > 0.45f, "the pump curve should return to unity");
}

// ---------------------------------------------------------------------------
//  Monitoring
// ---------------------------------------------------------------------------

SF_TEST (processor_monitorModesRouteTheRightSignal)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Compress);
    setParam (*p, sf::pid::compThreshold, -40.0f);
    setParam (*p, sf::pid::compRatio, 20.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    auto run = [&] (int monitorIndex)
    {
        setChoice (*p, sf::pid::monitor, monitorIndex);

        float peak = 0.0f;

        for (int b = 0; b < 40; ++b)
        {
            buffer.clear();
            for (int ch = 0; ch < 2; ++ch)
            {
                fillSine (buffer, ch, 1000.0, 48000.0, 0.5f);
                fillSine (buffer, 2 + ch, 60.0, 48000.0, 0.7f);
            }

            p->processBlock (buffer, midi);
            peak = buffer.getMagnitude (0, 0, 512);
        }

        return peak;
    };

    const auto normal = run ((int) sf::Monitor::Normal);
    const auto listen = run ((int) sf::Monitor::SidechainListen);
    const auto delta  = run ((int) sf::Monitor::Delta);

    SF_CHECK_FINITE (normal);
    SF_CHECK_MSG (std::abs (listen - 0.7f) < 0.05f,
                  ("sidechain listen should output the sidechain at 0.7, got " + num (listen)).c_str());
    SF_CHECK_MSG (delta > 0.05f, "delta should expose the removed content");
    SF_CHECK_MSG (delta < 0.55f, "delta must not exceed the source material");
}

SF_TEST (processor_auditionOutputsTheFilteredDetectorSignal)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    // A tight band around 1 kHz: a 60 Hz sidechain must come out heavily attenuated.
    setParam (*p, sf::pid::scHighPass, 800.0f);
    setParam (*p, sf::pid::scLowPass, 1200.0f);
    setParam (*p, sf::pid::scAudition, 1.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    float peak = 0.0f;

    for (int b = 0; b < 40; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            fillSine (buffer, ch, 1000.0, 48000.0, 0.5f);
            fillSine (buffer, 2 + ch, 60.0, 48000.0, 0.8f);
        }

        p->processBlock (buffer, midi);
        peak = buffer.getMagnitude (0, 0, 512);
    }

    SF_CHECK_MSG (peak < 0.2f,
                  ("the detector filter should have rejected the 60 Hz sidechain, peak was "
                   + num (peak)).c_str());
}

// ---------------------------------------------------------------------------
//  Latency
// ---------------------------------------------------------------------------

SF_TEST (processor_reportsLatencyForLookaheadAndSpectral)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    auto settle = [&]
    {
        for (int b = 0; b < 4; ++b)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
        }
    };

    // Live quality, no lookahead: no latency at all.
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Compress);
    setChoice (*p, sf::pid::quality, (int) sf::Quality::Live);
    setParam (*p, sf::pid::compLookahead, 0.0f);
    settle();
    SF_CHECK (p->getReportedLatencySamples() == 0);

    // Studio quality caps lookahead at 5 ms.
    setChoice (*p, sf::pid::quality, (int) sf::Quality::Studio);
    setParam (*p, sf::pid::compLookahead, 5.0f);
    settle();
    SF_CHECK_NEAR (p->getReportedLatencySamples(), 240, 1);

    // Spectral mode adds the STFT's own latency on top.
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Spectral);
    setChoice (*p, sf::pid::quality, (int) sf::Quality::Spectral);
    setParam (*p, sf::pid::compLookahead, 0.0f);
    settle();
    SF_CHECK (p->getReportedLatencySamples() == 2048);

    // ...and gives it back when the spectral stage leaves the path.
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Compress);
    settle();
    SF_CHECK (p->getReportedLatencySamples() == 0);
}

SF_TEST (processor_dryPathStaysAlignedWhenLatencyIsPresent)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    setChoice (*p, sf::pid::mode, (int) sf::Mode::Compress);
    setChoice (*p, sf::pid::quality, (int) sf::Quality::Studio);
    setParam (*p, sf::pid::compLookahead, 5.0f);
    setParam (*p, sf::pid::bypass, 1.0f);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    // Settle the bypass fade and the delay line.
    for (int b = 0; b < 40; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            fillSine (buffer, ch, 200.0, 48000.0, 0.4f, 2.0 * kPi * 200.0 * b * 512 / 48000.0);
        p->processBlock (buffer, midi);
    }

    // With bypass engaged, the output is the input delayed by exactly the reported
    // latency — which is what makes latency compensation in the host correct.
    const auto latency = p->getReportedLatencySamples();
    SF_CHECK (latency > 0);

    std::vector<float> input, output;

    for (int b = 0; b < 8; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            fillSine (buffer, ch, 200.0, 48000.0, 0.4f,
                      2.0 * kPi * 200.0 * (40 + b) * 512 / 48000.0);

        for (int i = 0; i < 512; ++i)
            input.push_back (buffer.getReadPointer (0)[i]);

        p->processBlock (buffer, midi);

        for (int i = 0; i < 512; ++i)
            output.push_back (buffer.getReadPointer (0)[i]);
    }

    for (size_t i = (size_t) latency; i < output.size(); ++i)
        SF_CHECK_NEAR (output[i], input[i - (size_t) latency], 1.0e-4);
}

// ---------------------------------------------------------------------------
//  Robustness
// ---------------------------------------------------------------------------

SF_TEST (processor_parameterBoundaryValuesAreSafe)
{
    auto p = makeProcessor (48000.0, 256, 2, 2);

    for (auto* parameter : p->getAPVTS().processor.getParameters())
    {
        for (const float normalised : { 0.0f, 0.5f, 1.0f })
        {
            parameter->setValueNotifyingHost (normalised);

            bool nonFinite = false;
            runBlocks (*p, 4, 256, 48000.0, 2, 2, &nonFinite);

            SF_CHECK_MSG (! nonFinite,
                          ("non-finite output after setting a parameter to "
                           + num (normalised)).c_str());
        }

        parameter->setValueNotifyingHost (parameter->getDefaultValue());
    }
}

SF_TEST (processor_rapidAutomationStaysStable)
{
    auto p = makeProcessor (48000.0, 128, 2, 2);
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);

    // Every parameter except the two pure gain trims, which can legitimately add
    // 48 dB between them and would make the level bound below meaningless.
    juce::Array<juce::AudioProcessorParameter*> parameters;

    for (auto* parameter : p->getAPVTS().processor.getParameters())
    {
        const auto* withID = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);

        if (withID != nullptr
            && (withID->paramID == sf::pid::inputGain || withID->paramID == sf::pid::outputGain))
            continue;

        parameters.add (parameter);
    }

    std::mt19937 rng { 2024 };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };
    std::uniform_int_distribution<int> pick { 0, parameters.size() - 1 };

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 128 };
    juce::MidiBuffer midi;

    for (int b = 0; b < 600; ++b)
    {
        // Slam several parameters every single block.
        for (int k = 0; k < 6; ++k)
            parameters[pick (rng)]->setValueNotifyingHost (unit (rng));

        buffer.clear();
        fillProcessorBuffer (buffer, 2, 2, 48000.0, 12000, b * 128);

        p->processBlock (buffer, midi);

        SF_CHECK_MSG (allFinite (buffer), "rapid automation produced non-finite output");

        for (int ch = 0; ch < 2; ++ch)
            SF_CHECK_MSG (buffer.getMagnitude (ch, 0, 128) < 16.0f,
                          "rapid automation produced a runaway level");
    }
}

SF_TEST (processor_extremeInputCannotDestabiliseIt)
{
    auto p = makeProcessor (48000.0, 256, 2, 2);
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);
    setChoice (*p, sf::pid::quality, (int) sf::Quality::Spectral);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 256 };
    juce::MidiBuffer midi;

    const float extremes[] = { 0.0f, 1.0e-30f, 1.0f, 1000.0f, 1.0e9f,
                               std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity() };

    for (const float value : extremes)
    {
        for (int b = 0; b < 12; ++b)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), value, 256);

            p->processBlock (buffer, midi);

            // Only the main output channels are the processor's to produce; the
            // sidechain input channels still hold whatever the host put there.
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    SF_CHECK_MSG (std::isfinite (buffer.getReadPointer (ch)[i]),
                                  "the processor must never emit a non-finite sample");
        }
    }

    // ...and it recovers to normal behaviour afterwards. One second is ample: the
    // detector filters settle in milliseconds and the compressor's default 120 ms
    // release needs a few time constants to climb back from a pinned -60 dB.
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Compress);

    for (int b = 0; b < 200; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            fillSine (buffer, ch, 440.0, 48000.0, 0.4f);
        p->processBlock (buffer, midi);
    }

    SF_CHECK_MSG (buffer.getMagnitude (0, 0, 256) > 0.35f,
                  ("the processor must recover after extreme input; level was "
                   + num (buffer.getMagnitude (0, 0, 256))).c_str());
}

SF_TEST (processor_longRunIsStable)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);
    setChoice (*p, sf::pid::quality, (int) sf::Quality::Spectral);
    setParam (*p, sf::pid::specDepth, 24.0f);

    bool nonFinite = false;
    // Roughly 30 seconds of audio.
    const auto peak = runBlocks (*p, 2800, 512, 48000.0, 2, 2, &nonFinite);

    SF_CHECK_MSG (! nonFinite, "a long run must stay finite throughout");
    SF_CHECK_MSG (peak < 4.0f, "a long run must not build up level");
    SF_CHECK_MSG (peak > 0.01f, "a long run must not decay into silence");
}

SF_TEST (processor_sampleRateChangeIsHandled)
{
    auto p = makeProcessor (44100.0, 512, 2, 2);
    setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);

    bool nonFinite = false;
    runBlocks (*p, 20, 512, 44100.0, 2, 2, &nonFinite);

    for (const double sr : { 48000.0, 96000.0, 44100.0 })
    {
        p->setRateAndBufferSizeDetails (sr, 512);
        p->prepareToPlay (sr, 512);
        runBlocks (*p, 20, 512, sr, 2, 2, &nonFinite);
    }

    SF_CHECK_MSG (! nonFinite, "changing sample rate must not destabilise the processor");
}

// ---------------------------------------------------------------------------
//  Editor
// ---------------------------------------------------------------------------

SF_TEST (processor_worksIdenticallyWithTheEditorOpenAndClosed)
{
    auto closed = makeProcessor (48000.0, 512, 2, 2);
    auto open   = makeProcessor (48000.0, 512, 2, 2);

    for (auto* p : { closed.get(), open.get() })
    {
        setChoice (*p, sf::pid::mode, (int) sf::Mode::Hybrid);
        setChoice (*p, sf::pid::quality, (int) sf::Quality::Studio);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor { open->createEditor() };
    SF_CHECK_MSG (editor != nullptr, "the processor must be able to create an editor");
    editor->setSize (1120, 720);

    juce::AudioBuffer<float> a { closed->getTotalNumInputChannels(), 512 };
    juce::AudioBuffer<float> b { open->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int block = 0; block < 60; ++block)
    {
        a.clear();
        b.clear();
        fillProcessorBuffer (a, 2, 2, 48000.0, 24000, block * 512);
        fillProcessorBuffer (b, 2, 2, 48000.0, 24000, block * 512);

        closed->processBlock (a, midi);
        open->processBlock (b, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                SF_CHECK_NEAR (b.getReadPointer (ch)[i], a.getReadPointer (ch)[i], 1.0e-6);
    }

    // Closing the editor mid-stream must not disturb processing either.
    editor.reset();

    for (int block = 0; block < 20; ++block)
    {
        b.clear();
        fillProcessorBuffer (b, 2, 2, 48000.0, 24000, block * 512);
        open->processBlock (b, midi);
        SF_CHECK (allFinite (b));
    }
}

// ---------------------------------------------------------------------------
//  Presets and A/B
// ---------------------------------------------------------------------------

SF_TEST (processor_everyFactoryPresetLoadsAndProcesses)
{
    SF_CHECK (sf::FactoryPresets::getNumPresets() == 8);

    for (int i = 0; i < sf::FactoryPresets::getNumPresets(); ++i)
    {
        auto p = makeProcessor (48000.0, 512, 2, 2);
        p->setCurrentProgram (i);

        SF_CHECK_MSG (sf::FactoryPresets::getName (i).isNotEmpty(), "presets must be named");
        SF_CHECK_MSG (p->getProgramName (i) == sf::FactoryPresets::getName (i),
                      "the host's program name must match the preset name");

        bool nonFinite = false;
        runBlocks (*p, 40, 512, 48000.0, 2, 2, &nonFinite);

        SF_CHECK_MSG (! nonFinite,
                      ("preset '" + sf::FactoryPresets::getName (i).toStdString()
                       + "' produced non-finite output").c_str());
    }
}

SF_TEST (processor_factoryPresetsRoundTripThroughState)
{
    for (int i = 0; i < sf::FactoryPresets::getNumPresets(); ++i)
    {
        auto source = makeProcessor (48000.0, 512, 2, 2);
        source->setCurrentProgram (i);

        juce::MemoryBlock state;
        source->getStateInformation (state);

        auto restored = makeProcessor (48000.0, 512, 2, 2);
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (auto* parameter : source->getAPVTS().processor.getParameters())
        {
            auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);

            if (withID == nullptr)
                continue;

            auto* other = restored->getAPVTS().getParameter (withID->paramID);
            SF_CHECK_MSG (other != nullptr, "every parameter must survive a state round trip");
            SF_CHECK_NEAR (other->getValue(), parameter->getValue(), 1.0e-5);
        }

        SF_CHECK (restored->getCurveModel().getNumNodes() == source->getCurveModel().getNumNodes());
    }
}

SF_TEST (processor_abSlotsStoreAndRecallIndependentStates)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    setParam (*p, sf::pid::compThreshold, -12.0f);
    p->setActiveSlot (0);

    setParam (*p, sf::pid::compThreshold, -48.0f);
    p->setActiveSlot (1);
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), -48.0, 0.01);

    setParam (*p, sf::pid::compThreshold, -6.0f);

    p->setActiveSlot (0);
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), -48.0, 0.01);

    p->setActiveSlot (1);
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), -6.0, 0.01);
}

SF_TEST (processor_exposesABypassParameterToTheHost)
{
    auto p = makeProcessor (48000.0, 512, 2, 2);

    auto* bypass = p->getBypassParameter();
    SF_CHECK_MSG (bypass != nullptr, "hosts need a bypass parameter for latency-correct bypass");

    auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (bypass);
    SF_CHECK (withID != nullptr && withID->paramID == sf::pid::bypass);
}
