#include "../TestHelpers.h"
#include "DSP/TriggerEngine.h"

using namespace sft;

namespace
{
/** A 1 kHz tone gated on and off, which gives the RMS detector a smooth, entirely
    predictable level to work with. */
void fillBurstTone (juce::AudioBuffer<float>& b, int channel, double sampleRate,
                    int periodSamples, int onSamples, float amp)
{
    auto* d = b.getWritePointer (channel);
    const auto inc = 2.0 * kPi * 1000.0 / sampleRate;

    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const auto phaseInPeriod = i % juce::jmax (1, periodSamples);
        d[i] = phaseInPeriod < onSamples ? amp * (float) std::sin (inc * i) : 0.0f;
    }
}

/** Runs the engine over a buffer in fixed blocks and returns every trigger
    position, expressed in samples from the start of the buffer. */
std::vector<int> collectTriggers (sf::TriggerEngine& engine, const juce::AudioBuffer<float>& source,
                                  int blockSize, double sampleRate,
                                  const juce::MidiBuffer& midi = {})
{
    std::vector<int> positions;
    sf::TransportInfo transport;
    sf::TriggerEvents events;
    juce::MidiBuffer blockMidi;

    const auto total = source.getNumSamples();

    for (int offset = 0; offset < total; offset += blockSize)
    {
        const auto n = juce::jmin (blockSize, total - offset);

        float* pointers[2] {};
        juce::AudioBuffer<float> slice;
        slice.setSize (source.getNumChannels(), n);

        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            slice.copyFrom (ch, 0, source, ch, offset, n);

        juce::ignoreUnused (pointers);

        blockMidi.clear();
        blockMidi.addEvents (midi, offset, n, -offset);

        engine.process (slice, 0, source.getNumChannels(), n, blockMidi, transport, 0.0, events);

        for (int i = 0; i < events.count; ++i)
            positions.push_back (offset + events.positions[i]);
    }

    juce::ignoreUnused (sampleRate);
    return positions;
}
} // namespace

SF_TEST (trigger_firesOncePerBurst)
{
    constexpr double sr = 48000.0;
    const int period = (int) (0.25 * sr);    // a burst every 250 ms
    const int on     = (int) (0.05 * sr);

    juce::AudioBuffer<float> source { 1, period * 6 };
    fillBurstTone (source, 0, sr, period, on, 0.8f);

    sf::TriggerEngine engine;
    engine.prepare (sr, 512, 1);

    sf::TriggerEngine::Settings s;
    s.source = sf::TrigSource::Sidechain;
    s.detect = sf::TrigDetect::Rms;
    s.thresholdDb = -20.0f;
    s.hysteresisDb = 6.0f;
    s.minIntervalMs = 20.0f;
    engine.setSettings (s);

    const auto triggers = collectTriggers (engine, source, 256, sr);

    SF_CHECK_MSG (triggers.size() == 6,
                  ("expected 6 triggers, got " + std::to_string (triggers.size())).c_str());

    // Each one lands shortly after its burst begins — the RMS window is 10 ms.
    for (size_t i = 0; i < triggers.size(); ++i)
    {
        const auto offsetIntoBurst = triggers[i] - (int) i * period;
        SF_CHECK_MSG (offsetIntoBurst >= 0 && offsetIntoBurst < (int) (0.02 * sr),
                      ("trigger " + std::to_string (i) + " landed at +"
                       + std::to_string (offsetIntoBurst) + " samples").c_str());
    }
}

SF_TEST (trigger_hysteresisPreventsChatterOnAWaveringLevel)
{
    constexpr double sr = 48000.0;
    const int total = (int) (2.0 * sr);

    // A tone whose level dithers by 3 dB around -14 dBFS: it crosses the -20 dB
    // threshold once and then never falls back through the hysteresis window.
    juce::AudioBuffer<float> source { 1, total };
    auto* d = source.getWritePointer (0);
    const auto inc = 2.0 * kPi * 1000.0 / sr;

    for (int i = 0; i < total; ++i)
    {
        const auto wobble = 0.2f + 0.05f * (float) std::sin (2.0 * kPi * 7.0 * i / sr);
        d[i] = (i < 4800) ? 0.0f : wobble * (float) std::sin (inc * i);
    }

    sf::TriggerEngine engine;
    engine.prepare (sr, 512, 1);

    sf::TriggerEngine::Settings s;
    s.detect = sf::TrigDetect::Rms;
    s.thresholdDb = -20.0f;
    s.hysteresisDb = 6.0f;
    s.minIntervalMs = 0.0f;
    engine.setSettings (s);

    const auto triggers = collectTriggers (engine, source, 128, sr);

    SF_CHECK_MSG (triggers.size() == 1,
                  ("hysteresis should have allowed exactly one trigger, got "
                   + std::to_string (triggers.size())).c_str());
}

SF_TEST (trigger_zeroHysteresisStillRearmsAfterTheSignalStops)
{
    constexpr double sr = 48000.0;
    const int period = (int) (0.2 * sr);
    const int on     = (int) (0.05 * sr);

    juce::AudioBuffer<float> source { 1, period * 4 };
    fillBurstTone (source, 0, sr, period, on, 0.8f);

    sf::TriggerEngine engine;
    engine.prepare (sr, 512, 1);

    sf::TriggerEngine::Settings s;
    s.detect = sf::TrigDetect::Rms;
    s.thresholdDb = -20.0f;
    s.hysteresisDb = 0.0f;
    s.minIntervalMs = 10.0f;
    engine.setSettings (s);

    const auto triggers = collectTriggers (engine, source, 256, sr);
    SF_CHECK_MSG (triggers.size() == 4,
                  ("expected 4 triggers, got " + std::to_string (triggers.size())).c_str());
}

SF_TEST (trigger_minimumIntervalSuppressesFastRepeats)
{
    constexpr double sr = 48000.0;
    const int period = (int) (0.03 * sr);     // bursts every 30 ms
    const int on     = (int) (0.008 * sr);

    juce::AudioBuffer<float> source { 1, period * 20 };
    fillBurstTone (source, 0, sr, period, on, 0.9f);

    auto countWithInterval = [&] (float intervalMs)
    {
        sf::TriggerEngine engine;
        engine.prepare (sr, 512, 1);

        // Transient detection, because at this burst density an RMS window never
        // falls back through the hysteresis between bursts — which is correct
        // behaviour for RMS, but would test nothing here.
        sf::TriggerEngine::Settings s;
        s.detect = sf::TrigDetect::Transient;
        s.thresholdDb = -24.0f;
        s.hysteresisDb = 3.0f;
        s.minIntervalMs = intervalMs;
        engine.setSettings (s);

        return collectTriggers (engine, source, 128, sr).size();
    };

    const auto unrestricted = countWithInterval (0.0f);
    const auto restricted   = countWithInterval (100.0f);

    SF_CHECK_MSG (unrestricted >= 15,
                  ("expected roughly one trigger per burst, got " + std::to_string (unrestricted)).c_str());
    SF_CHECK_MSG (restricted < unrestricted / 2,
                  ("a 100 ms minimum gap should have suppressed most of the "
                   + std::to_string (unrestricted) + " triggers, got "
                   + std::to_string (restricted)).c_str());
}

SF_TEST (trigger_thresholdIsRespected)
{
    constexpr double sr = 48000.0;
    const int total = (int) (1.0 * sr);

    juce::AudioBuffer<float> source { 1, total };
    fillSine (source, 0, 1000.0, sr, 0.1f);     // -20 dBFS peak, about -23 dBFS RMS

    auto countAtThreshold = [&] (float thresholdDb)
    {
        sf::TriggerEngine engine;
        engine.prepare (sr, 512, 1);

        sf::TriggerEngine::Settings s;
        s.detect = sf::TrigDetect::Rms;
        s.thresholdDb = thresholdDb;
        s.hysteresisDb = 3.0f;
        s.minIntervalMs = 0.0f;
        engine.setSettings (s);

        return collectTriggers (engine, source, 256, sr).size();
    };

    SF_CHECK_MSG (countAtThreshold (-40.0f) == 1, "a level well above the threshold must trigger");
    SF_CHECK_MSG (countAtThreshold (-6.0f) == 0, "a level below the threshold must not trigger");
}

SF_TEST (trigger_midiSourceFiresOnMatchingNotesOnly)
{
    constexpr double sr = 48000.0;
    juce::AudioBuffer<float> source { 1, 4096 };
    source.clear();

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 100);
    midi.addEvent (juce::MidiMessage::noteOn (1, 38, 1.0f), 900);
    midi.addEvent (juce::MidiMessage::noteOff (1, 36), 1200);
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 2500);

    {
        sf::TriggerEngine engine;
        engine.prepare (sr, 4096, 1);

        sf::TriggerEngine::Settings s;
        s.source = sf::TrigSource::Midi;
        s.midiNote = 36;
        s.minIntervalMs = 0.0f;
        engine.setSettings (s);

        const auto triggers = collectTriggers (engine, source, 4096, sr, midi);
        SF_CHECK_MSG (triggers.size() == 2, "only note 36's two note-ons should trigger");
        SF_CHECK (triggers[0] == 100);
        SF_CHECK (triggers[1] == 2500);
    }

    {
        // 128 means "any note".
        sf::TriggerEngine engine;
        engine.prepare (sr, 4096, 1);

        sf::TriggerEngine::Settings s;
        s.source = sf::TrigSource::Midi;
        s.midiNote = 128;
        s.minIntervalMs = 0.0f;
        engine.setSettings (s);

        const auto triggers = collectTriggers (engine, source, 4096, sr, midi);
        SF_CHECK_MSG (triggers.size() == 3, "every note-on should trigger when the note is Any");
    }
}

SF_TEST (trigger_transportSourceFiresOnDivisionBoundaries)
{
    constexpr double sr = 48000.0;

    sf::TriggerEngine engine;
    engine.prepare (sr, 512, 1);

    sf::TriggerEngine::Settings s;
    s.source = sf::TrigSource::Transport;
    s.transportDivisionIdx = 13;    // 1/4
    s.minIntervalMs = 0.0f;
    engine.setSettings (s);

    sf::TransportInfo transport;
    transport.valid = true;
    transport.isPlaying = true;
    transport.bpm = 120.0;          // a quarter note is 0.5 s = 24000 samples
    transport.ppqPosition = 0.0;

    juce::AudioBuffer<float> block { 1, 512 };
    block.clear();
    juce::MidiBuffer midi;
    sf::TriggerEvents events;

    int total = 0;
    const int blocks = (int) ((4.0 * sr) / 512.0);   // four seconds = eight quarter notes

    for (int b = 0; b < blocks; ++b)
    {
        const auto quarters = transport.ppqPerSample (sr) * 512.0;
        engine.process (block, 0, 1, 512, midi, transport, quarters, events);
        total += events.count;
        transport.ppqPosition += quarters;
    }

    SF_CHECK_MSG (total >= 7 && total <= 8,
                  ("expected about 8 quarter-note triggers in 4 s, got " + std::to_string (total)).c_str());
}

SF_TEST (trigger_manualTriggerAlwaysFires)
{
    constexpr double sr = 48000.0;

    sf::TriggerEngine engine;
    engine.prepare (sr, 512, 1);

    sf::TriggerEngine::Settings s;
    s.source = sf::TrigSource::Sidechain;
    s.thresholdDb = 0.0f;           // nothing in the audio could ever reach this
    engine.setSettings (s);

    juce::AudioBuffer<float> block { 1, 512 };
    block.clear();
    juce::MidiBuffer midi;
    sf::TransportInfo transport;
    sf::TriggerEvents events;

    engine.process (block, 0, 1, 512, midi, transport, 0.0, events);
    SF_CHECK (events.count == 0);

    engine.fireManualTrigger();
    engine.process (block, 0, 1, 512, midi, transport, 0.0, events);
    SF_CHECK_MSG (events.count == 1, "the GUI test trigger must fire regardless of the source");
    SF_CHECK (events.positions[0] == 0);

    // ...and it is consumed, not sticky.
    engine.process (block, 0, 1, 512, midi, transport, 0.0, events);
    SF_CHECK (events.count == 0);
}

SF_TEST (trigger_everyDetectorHandlesSilenceAndExtremes)
{
    constexpr double sr = 48000.0;

    for (int detect = 0; detect < 4; ++detect)
    {
        sf::TriggerEngine engine;
        engine.prepare (sr, 512, 2);

        sf::TriggerEngine::Settings s;
        s.detect = (sf::TrigDetect) detect;
        s.thresholdDb = -30.0f;
        engine.setSettings (s);

        juce::AudioBuffer<float> block { 2, 512 };
        juce::MidiBuffer midi;
        sf::TransportInfo transport;
        sf::TriggerEvents events;

        // Silence.
        block.clear();
        engine.process (block, 0, 2, 512, midi, transport, 0.0, events);
        SF_CHECK_MSG (events.count == 0, "silence must not trigger");

        // Extremely loud.
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (block.getWritePointer (ch), 500.0f, 512);

        engine.process (block, 0, 2, 512, midi, transport, 0.0, events);
        SF_CHECK_FINITE (engine.getLevelDb());

        // Non-finite input.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = block.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                d[i] = (i % 2) ? std::numeric_limits<float>::quiet_NaN()
                               : std::numeric_limits<float>::infinity();
        }

        engine.process (block, 0, 2, 512, midi, transport, 0.0, events);
        SF_CHECK_FINITE (engine.getLevelDb());

        for (int i = 0; i < engine.getAuditionSignal().getNumSamples(); ++i)
            SF_CHECK_FINITE (engine.getAuditionSignal().getReadPointer (0)[i]);
    }
}

SF_TEST (trigger_resultIsIndependentOfBlockSize)
{
    constexpr double sr = 48000.0;
    const int period = (int) (0.2 * sr);
    const int on     = (int) (0.04 * sr);

    juce::AudioBuffer<float> source { 1, period * 5 };
    fillBurstTone (source, 0, sr, period, on, 0.7f);

    std::vector<int> reference;

    for (const int blockSize : { 32, 64, 128, 480, 512, 1024 })
    {
        sf::TriggerEngine engine;
        engine.prepare (sr, 1024, 1);

        sf::TriggerEngine::Settings s;
        s.detect = sf::TrigDetect::Rms;
        s.thresholdDb = -20.0f;
        s.hysteresisDb = 6.0f;
        s.minIntervalMs = 20.0f;
        engine.setSettings (s);

        const auto triggers = collectTriggers (engine, source, blockSize, sr);

        if (reference.empty())
            reference = triggers;
        else
        {
            SF_CHECK_MSG (triggers.size() == reference.size(),
                          "trigger count changed with the block size");

            for (size_t i = 0; i < triggers.size(); ++i)
                SF_CHECK_MSG (std::abs (triggers[i] - reference[i]) <= 1,
                              "trigger position moved with the block size");
        }
    }
}
