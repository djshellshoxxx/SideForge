#include "TriggerEngine.h"
#include "../Parameters/Divisions.h"
#include <limits>

namespace sf
{
void TriggerEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedChannels = juce::jlimit (1, kMaxCh, numChannels);

    filtered.setSize (1, juce::jmax (1, maxBlockSize), false, true, true);

    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlockSize), 1 };

    highPass.prepare (spec);
    highPass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowPass.prepare (spec);
    lowPass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    weightHighPass.prepare (spec);
    weightShelf.prepare (spec);

    // ITU-R BS.1770 style weighting, approximated with the two stages it is
    // actually made of: a ~38 Hz high-pass and a +4 dB shelf at ~1.5 kHz.
    weightHighPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 38.0f, 0.5f);
    weightShelf.coefficients    = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 1500.0f, 0.707f,
                                                                                      juce::Decibels::decibelsToGain (4.0f));

    rmsCoeff  = timeConstantCoeff (10.0f, sr);
    fastCoeff = timeConstantCoeff (1.5f, sr);
    slowCoeff = timeConstantCoeff (120.0f, sr);

    setSettings (settings);
    reset();
}

void TriggerEngine::reset()
{
    highPass.reset();
    lowPass.reset();
    weightHighPass.reset();
    weightShelf.reset();
    filtered.clear();

    rmsState = fastState = slowState = 0.0f;
    gateOpen = false;
    samplesSinceTrigger = minIntervalSamples;   // ready to fire immediately
    transportPhase = 0.0;
    transportPrimed = false;
    manualPending.store (false, std::memory_order_relaxed);
    armed.store (false, std::memory_order_relaxed);
    levelDb.store (-100.0f, std::memory_order_relaxed);
}

void TriggerEngine::setSettings (const Settings& s)
{
    settings = s;
    settings.midiNote = juce::jlimit (0, 128, settings.midiNote);
    settings.transportDivisionIdx = juce::jlimit (0, kNumDivisions - 1, settings.transportDivisionIdx);

    const auto nyquistLimit = (float) (sr * 0.49);
    const auto hp = juce::jlimit (10.0f, nyquistLimit, settings.highPassHz);
    const auto lp = juce::jlimit (hp, nyquistLimit, settings.lowPassHz);

    highPass.setCutoffFrequency (hp);
    lowPass.setCutoffFrequency (lp);

    minIntervalSamples = (int) std::lround ((double) juce::jmax (0.0f, settings.minIntervalMs) * 0.001 * sr);
}

float TriggerEngine::detectorLevel (float x) noexcept
{
    const auto sq = x * x;

    rmsState  = flushDenormal (sq + rmsCoeff * (rmsState - sq));
    fastState = flushDenormal (sq + fastCoeff * (fastState - sq));
    slowState = flushDenormal (sq + slowCoeff * (slowState - sq));

    switch (settings.detect)
    {
        case TrigDetect::Rms:
            return std::sqrt (juce::jmax (0.0f, rmsState));

        case TrigDetect::Transient:
        {
            // The part of the fast envelope that the slow envelope does not
            // explain: silence and steady tones read near zero, onsets read high.
            const auto fast = std::sqrt (juce::jmax (0.0f, fastState));
            const auto slow = std::sqrt (juce::jmax (0.0f, slowState));
            return juce::jmax (0.0f, fast - slow);
        }

        case TrigDetect::Weighted:
        {
            const auto w = weightShelf.processSample (weightHighPass.processSample (x));
            return std::abs (flushDenormal (w));
        }

        case TrigDetect::Peak:
        default:
            return std::abs (x);
    }
}

void TriggerEngine::processAudioSource (int numSamples, TriggerEvents& events)
{
    const auto* data = filtered.getReadPointer (0);

    const auto openLevel  = juce::Decibels::decibelsToGain (settings.thresholdDb, -120.0f);
    const auto closeLevel = juce::Decibels::decibelsToGain (settings.thresholdDb
                                                            - juce::jmax (0.0f, settings.hysteresisDb),
                                                           -120.0f);

    float lastLevel = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto level = detectorLevel (data[i]);
        lastLevel = level;

        if (samplesSinceTrigger < std::numeric_limits<int>::max())
            ++samplesSinceTrigger;

        if (! gateOpen)
        {
            if (level >= openLevel && samplesSinceTrigger >= minIntervalSamples)
            {
                gateOpen = true;
                samplesSinceTrigger = 0;
                events.add (i);
            }
        }
        else if (level < closeLevel)
        {
            // Hysteresis: the level must fall back below threshold minus the
            // hysteresis window before another trigger can arm.
            gateOpen = false;
        }
    }

    armed.store (gateOpen, std::memory_order_relaxed);
    levelDb.store (juce::Decibels::gainToDecibels (lastLevel, -100.0f), std::memory_order_relaxed);
}

void TriggerEngine::processMidiSource (const juce::MidiBuffer& midi, int numSamples,
                                       TriggerEvents& events)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (! message.isNoteOn())
            continue;

        if (settings.midiNote < 128 && message.getNoteNumber() != settings.midiNote)
            continue;

        const auto position = juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition);

        if (samplesSinceTrigger + position >= minIntervalSamples)
        {
            events.add (position);
            samplesSinceTrigger = -position;   // becomes 0 at the trigger sample
        }
    }

    samplesSinceTrigger = juce::jmax (0, samplesSinceTrigger + numSamples);
    armed.store (events.count > 0, std::memory_order_relaxed);
}

void TriggerEngine::processTransportSource (const TransportInfo& transport, double quartersThisBlock,
                                            int numSamples, TriggerEvents& events)
{
    const auto cycleQuarters = divisionInQuarters (settings.transportDivisionIdx,
                                                   transport.timeSigNumerator,
                                                   transport.timeSigDenominator);

    if (cycleQuarters <= 0.0 || numSamples <= 0)
        return;

    if (! transport.valid || ! transport.isPlaying)
    {
        // Stopped: hold the phase so that resuming lines up with the host again.
        transportPrimed = false;
        armed.store (false, std::memory_order_relaxed);
        return;
    }

    const auto startPhase = transport.ppqPosition / cycleQuarters;
    const auto perSample  = (quartersThisBlock / (double) numSamples) / cycleQuarters;

    // Every crossing of an integer multiple of the division inside this block is a
    // trigger, which is what makes playback starting mid-bar land correctly.
    auto phase = startPhase;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto next = phase + perSample;

        if (! transportPrimed)
        {
            transportPrimed = true;
        }
        else if (std::floor (next) > std::floor (phase))
        {
            events.add (i);
        }

        phase = next;
    }

    transportPhase = phase - std::floor (phase);
    armed.store (true, std::memory_order_relaxed);
}

void TriggerEngine::process (const juce::AudioBuffer<float>& source, int startChannel, int numChannels,
                             int numSamples, const juce::MidiBuffer& midi,
                             const TransportInfo& transport, double quartersThisBlock,
                             TriggerEvents& events)
{
    events.clear();
    numSamples = juce::jlimit (0, filtered.getNumSamples(), numSamples);

    if (numSamples == 0)
        return;

    // --- mono-sum the chosen source, then apply the trigger's own filter -----
    auto* dest = filtered.getWritePointer (0);
    const auto usable = juce::jmax (0, juce::jmin (numChannels, source.getNumChannels() - startChannel));

    if (usable <= 0)
    {
        juce::FloatVectorOperations::clear (dest, numSamples);
    }
    else
    {
        juce::FloatVectorOperations::copy (dest, source.getReadPointer (startChannel), numSamples);

        for (int ch = 1; ch < usable; ++ch)
            juce::FloatVectorOperations::add (dest, source.getReadPointer (startChannel + ch), numSamples);

        if (usable > 1)
            juce::FloatVectorOperations::multiply (dest, 1.0f / (float) usable, numSamples);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        auto v = sanitise (dest[i]);
        v = highPass.processSample (0, v);
        v = lowPass.processSample (0, v);
        dest[i] = flushDenormal (v);
    }

    // A manual trigger from the GUI always fires, whatever the selected source is:
    // it is a test button, and it would be useless if the routing could swallow it.
    // It is added before the routed events so `events.positions` stays ascending.
    if (manualPending.exchange (false, std::memory_order_acq_rel))
    {
        events.add (0);
        samplesSinceTrigger = 0;
    }

    // --- route ---------------------------------------------------------------
    switch (settings.source)
    {
        case TrigSource::Midi:
            processMidiSource (midi, numSamples, events);
            break;

        case TrigSource::Transport:
            processTransportSource (transport, quartersThisBlock, numSamples, events);
            break;

        case TrigSource::Manual:
            samplesSinceTrigger = juce::jmax (0, samplesSinceTrigger + numSamples);
            armed.store (false, std::memory_order_relaxed);
            break;

        case TrigSource::Sidechain:
        case TrigSource::Internal:
        default:
            processAudioSource (numSamples, events);
            break;
    }
}
} // namespace sf
