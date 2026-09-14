#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Model/FactoryPresets.h"
#include <cmath>

namespace sf
{
namespace
{
constexpr int kMinPreparedBlock = 2048;
constexpr float kSidechainSignalFloor = 1.0e-4f;   // ~ -80 dBFS

/** Replaces anything the host should never see with silence. */
void sanitiseBuffer (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }
}
} // namespace

SideForgeProcessor::SideForgeProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                                .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, PresetState::paramsTag, createParameterLayout())
{
    params.attach (apvts);
    bypassParameter = apvts.getParameter (pid::bypass);
    curveTables.publish (curveModel);
}

SideForgeProcessor::~SideForgeProcessor()
{
    cancelPendingUpdate();
}

bool SideForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);

        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

double SideForgeProcessor::getTailLengthSeconds() const
{
    return (double) latency.getTotalSamples() / juce::jmax (1.0, preparedSampleRate);
}

void SideForgeProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize  = juce::jmax (kMinPreparedBlock, maximumExpectedSamplesPerBlock);

    const auto mainChannels = juce::jmax (getMainBusNumInputChannels(), getMainBusNumOutputChannels());
    preparedChannels = juce::jlimit (1, kMaxChannels, mainChannels);

    // -- buffers --------------------------------------------------------------
    dryBuffer.setSize (preparedChannels, preparedBlockSize, false, true, true);
    wetBuffer.setSize (preparedChannels, preparedBlockSize, false, true, true);
    scBuffer.setSize (kMaxChannels, preparedBlockSize, false, true, true);
    gainBuffer.setSize (preparedChannels, preparedBlockSize, false, true, true);
    curveGainBuffer.setSize (1, preparedBlockSize, false, true, true);
    scAligned.setSize (kMaxChannels, preparedBlockSize, false, true, true);
    chunkMidi.ensureSize (4096);

    // -- engines --------------------------------------------------------------
    detector.prepare (preparedSampleRate, preparedBlockSize, kMaxChannels);
    compressor.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
    curveEngine.prepare (preparedSampleRate, preparedBlockSize);
    triggerEngine.prepare (preparedSampleRate, preparedBlockSize, kMaxChannels);
    multiband.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
    spectral.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
    latency.prepare (preparedSampleRate);
    meters.prepare (preparedSampleRate);
    analysis.prepare (preparedSampleRate);

    const auto maxLookahead = LatencyManager::maxLookaheadSamples (preparedSampleRate);
    dryDelay.prepare (preparedChannels, LatencyManager::maxTotalSamples (preparedSampleRate));
    lookaheadDelay.prepare (preparedChannels, maxLookahead);
    scDelay.prepare (kMaxChannels, maxLookahead);

    // -- smoothers ------------------------------------------------------------
    inputGain.prepare (preparedSampleRate);
    outputGain.prepare (preparedSampleRate);
    mixAmount.prepare (preparedSampleRate);
    makeupGain.prepare (preparedSampleRate, 0.1);
    bypassFade.prepare (preparedSampleRate);

    const auto initial = buildEngineSettings (params);
    inputGain.snapTo (juce::Decibels::decibelsToGain (initial.inputGainDb));
    outputGain.snapTo (juce::Decibels::decibelsToGain (initial.outputGainDb));
    mixAmount.snapTo (initial.mixUnit);
    makeupGain.snapTo (1.0f);
    bypassFade.snapActive (! initial.bypass);

    sidechainConnected.store (getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled());
    sidechainHasSignal.store (false);

    lastTransport = {};
    prepared = true;

    latency.update (initial.lookaheadMs, initial.quality, initial.spectralActive);
    latency.consumeChange();
    setLatencySamples (latency.getTotalSamples());
}

void SideForgeProcessor::releaseResources()
{
    prepared = false;
}

void SideForgeProcessor::handleAsyncUpdate()
{
    // setLatencySamples talks to the host and must not be called from the audio
    // thread, so the audio thread only raises a flag and this publishes the value
    // it settled on.
    setLatencySamples (latency.getTotalSamples());
}

void SideForgeProcessor::applySettings (const EngineSettings& s)
{
    detector.setFilters (s.scHighPassHz, s.scLowPassHz);
    detector.setGainDb (s.scGainDb);
    detector.setDetectorType (s.scDetector);
    detector.setStereoLink (s.scStereoLink);

    compressor.setSettings (s.comp);
    curveEngine.setSettings (s.curve);
    triggerEngine.setSettings (s.trigger);
    multiband.setSettings (s.bands);
    spectral.setSettings (s.spectral);

    const auto cfg = qualityConfig (s.quality);
    spectral.setConfig (cfg.fftOrder, cfg.overlap);
}

// ---------------------------------------------------------------------------

void SideForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalIn  = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (! prepared || numSamples <= 0)
        return;

    settings = buildEngineSettings (params);
    applySettings (settings);

    auto transport = readTransport (getPlayHead());
    const auto quartersTotal = transport.ppqPerSample (preparedSampleRate) * (double) numSamples;

    if (transport.hasDiscontinuity (lastTransport, quartersTotal))
        curveEngine.reset();

    lastTransport = transport;
    lastTransport.ppqPosition = transport.ppqPosition + quartersTotal;

    // Hosts may hand us a larger block than they promised; slicing keeps every
    // preallocated buffer valid without ever resizing on the audio thread.

    if (numSamples <= preparedBlockSize)
    {
        processChunk (buffer, midi, transport, quartersTotal);
        return;
    }

    float* channelPointers[8] {};
    const auto ppqPerSample = transport.ppqPerSample (preparedSampleRate);

    for (int offset = 0; offset < numSamples; offset += preparedBlockSize)
    {
        const auto length = juce::jmin (preparedBlockSize, numSamples - offset);

        const auto sliceChannels = juce::jmin (buffer.getNumChannels(), 8);

        for (int ch = 0; ch < sliceChannels; ++ch)
            channelPointers[ch] = buffer.getWritePointer (ch) + offset;

        juce::AudioBuffer<float> slice (channelPointers, sliceChannels, length);

        chunkMidi.clear();
        chunkMidi.addEvents (midi, offset, length, -offset);

        auto sliceTransport = transport;
        sliceTransport.ppqPosition = transport.ppqPosition + ppqPerSample * (double) offset;

        processChunk (slice, chunkMidi, sliceTransport, ppqPerSample * (double) length);
    }
}

void SideForgeProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                       const TransportInfo& transport, double quartersThisBlock)
{
    const auto numSamples = buffer.getNumSamples();
    const auto mainChannels = juce::jlimit (1, preparedChannels, getMainBusNumOutputChannels());

    // ---- sidechain source ---------------------------------------------------
    const auto* scBus = getBus (true, 1);
    const auto scEnabled = scBus != nullptr && scBus->isEnabled();
    const auto scChannelCount = scEnabled ? juce::jmin (kMaxChannels, scBus->getNumberOfChannels()) : 0;
    const auto scStartChannel = scEnabled ? getChannelIndexInProcessBlockBuffer (true, 1, 0) : 0;

    sidechainConnected.store (scEnabled);

    const auto useExternal = (settings.scSource == ScSource::External) && scChannelCount > 0;
    const auto scSourceChannels = useExternal ? scChannelCount : mainChannels;
    const auto scSourceStart = useExternal ? scStartChannel : 0;

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        const auto src = scSourceStart + juce::jmin (ch, scSourceChannels - 1);

        if (src >= 0 && src < buffer.getNumChannels())
            scBuffer.copyFrom (ch, 0, buffer, src, 0, numSamples);
        else
            scBuffer.clear (ch, 0, numSamples);
    }

    {
        float magnitude = 0.0f;

        for (int ch = 0; ch < scChannelCount; ++ch)
        {
            const auto src = scStartChannel + ch;

            if (src >= 0 && src < buffer.getNumChannels())
                magnitude = juce::jmax (magnitude, buffer.getMagnitude (src, 0, numSamples));
        }

        sidechainHasSignal.store (scEnabled && magnitude > kSidechainSignalFloor);
    }

    // ---- input gain, then capture the dry path ------------------------------
    inputGain.setTarget (juce::Decibels::decibelsToGain (settings.inputGainDb));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = inputGain.next();

        for (int ch = 0; ch < mainChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    for (int ch = 0; ch < mainChannels; ++ch)
    {
        meters.input[juce::jmin (ch, MeterBank::kChannels - 1)]
            .processBlock (buffer.getReadPointer (ch), numSamples);

        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        wetBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // ---- detector + triggers ------------------------------------------------
    detector.process (scBuffer, 0, kMaxChannels, numSamples);

    for (int ch = 0; ch < MeterBank::kChannels; ++ch)
        meters.sidechain[ch].processBlock (detector.getFiltered().getReadPointer (ch), numSamples);

    {
        // The trigger listens to whatever its own source says, independently of the
        // compressor's detector, and applies its own filter.
        const auto internal = (settings.trigger.source == TrigSource::Internal);
        const auto& triggerSource = internal ? buffer : scBuffer;
        const auto triggerChannels = internal ? mainChannels : kMaxChannels;

        triggerEngine.process (triggerSource, 0, triggerChannels, numSamples,
                               midi, transport, quartersThisBlock, triggerEvents);
    }

    // ---- gain signals -------------------------------------------------------
    if (settings.useCompressor)
    {
        compressor.process (detector.getEnvelopeBuffer(), gainBuffer, mainChannels, numSamples);
    }
    else
    {
        for (int ch = 0; ch < mainChannels; ++ch)
            juce::FloatVectorOperations::fill (gainBuffer.getWritePointer (ch), 1.0f, numSamples);
    }

    if (settings.useCurve)
    {
        curveEngine.setTable (&curveTables.acquire());
        curveEngine.process (numSamples, transport, quartersThisBlock, triggerEvents,
                             curveGainBuffer.getWritePointer (0));

        const auto* curveGain = curveGainBuffer.getReadPointer (0);

        for (int ch = 0; ch < mainChannels; ++ch)
        {
            auto* g = gainBuffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
                g[i] *= curveGain[i];
        }
    }

    // Hybrid trades broadband ducking for spectral ducking.
    if (settings.broadbandBlend < 1.0f)
    {
        for (int ch = 0; ch < mainChannels; ++ch)
        {
            auto* g = gainBuffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
                g[i] = 1.0f - (1.0f - g[i]) * settings.broadbandBlend;
        }
    }

    // ---- latency ------------------------------------------------------------
    const auto totalLatency = latency.update (settings.lookaheadMs, settings.quality,
                                              settings.spectralActive);
    const auto lookaheadSamples = latency.getLookaheadSamples();

    lookaheadDelay.setDelay (lookaheadSamples);
    scDelay.setDelay (lookaheadSamples);
    dryDelay.setDelay (totalLatency);

    lookaheadDelay.process (wetBuffer, numSamples);

    // ---- broadband stage ----------------------------------------------------
    multiband.process (wetBuffer, gainBuffer, mainChannels, numSamples);

    // ---- spectral stage -----------------------------------------------------
    if (settings.spectralActive)
    {
        // The audio has been delayed by the lookahead, so the sidechain the
        // collision map sees has to be delayed by the same amount to stay aligned.
        const auto& filtered = detector.getFiltered();

        for (int ch = 0; ch < kMaxChannels; ++ch)
            scAligned.copyFrom (ch, 0, filtered, juce::jmin (ch, filtered.getNumChannels() - 1),
                                0, numSamples);

        scDelay.process (scAligned, numSamples);
        spectral.process (wetBuffer, scAligned, mainChannels, numSamples);
    }

    dryDelay.process (dryBuffer, numSamples);

    // ---- monitoring ---------------------------------------------------------
    // Priority runs from the most specific listen to the least: the trigger's own
    // filtered signal, then the detector's, then the monitor selector.
    if (settings.trigAudition)
    {
        const auto* audition = triggerEngine.getAuditionSignal().getReadPointer (0);

        for (int ch = 0; ch < mainChannels; ++ch)
            juce::FloatVectorOperations::copy (wetBuffer.getWritePointer (ch), audition, numSamples);
    }
    else if (settings.scAudition)
    {
        for (int ch = 0; ch < mainChannels; ++ch)
            wetBuffer.copyFrom (ch, 0, detector.getFiltered(),
                                juce::jmin (ch, detector.getFiltered().getNumChannels() - 1),
                                0, numSamples);
    }
    else if (settings.monitor == Monitor::SidechainListen)
    {
        for (int ch = 0; ch < mainChannels; ++ch)
            wetBuffer.copyFrom (ch, 0, scBuffer, juce::jmin (ch, kMaxChannels - 1), 0, numSamples);
    }
    else if (settings.monitor == Monitor::Delta)
    {
        for (int ch = 0; ch < mainChannels; ++ch)
        {
            auto* wet = wetBuffer.getWritePointer (ch);
            const auto* dry = dryBuffer.getReadPointer (ch);

            for (int i = 0; i < numSamples; ++i)
                wet[i] = dry[i] - wet[i];
        }
    }

    // ---- auto gain, mix, output gain, bypass --------------------------------
    const auto makeupTarget = (settings.autoGain && settings.useCompressor)
                                  ? compressor.computeAutoMakeupGain() : 1.0f;
    makeupGain.setTarget (makeupTarget);
    mixAmount.setTarget (settings.mixUnit);
    outputGain.setTarget (juce::Decibels::decibelsToGain (settings.outputGainDb));
    bypassFade.setActive (! settings.bypass);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto makeup = makeupGain.next();
        const auto mix    = mixAmount.next();
        const auto out    = outputGain.next();
        const auto active = bypassFade.next();

        for (int ch = 0; ch < mainChannels; ++ch)
        {
            const auto dry = dryBuffer.getReadPointer (ch)[i];
            const auto wet = wetBuffer.getWritePointer (ch)[i] * makeup;

            const auto processed = (dry + (wet - dry) * mix) * out;

            // Bypass crossfades to the latency-matched dry signal, so engaging it
            // is click-free and stays sample-aligned with the host.
            buffer.getWritePointer (ch)[i] = dry + (processed - dry) * active;
        }
    }

    sanitiseBuffer (buffer, mainChannels, numSamples);

    // ---- metering + visualisation -------------------------------------------
    for (int ch = 0; ch < mainChannels; ++ch)
        meters.output[juce::jmin (ch, MeterBank::kChannels - 1)]
            .processBlock (buffer.getReadPointer (ch), numSamples);

    const auto minGain = settings.spectralActive
                             ? juce::jmin (multiband.getLastMinGain(), spectral.getLastMinGain())
                             : multiband.getLastMinGain();
    meters.gainReduction.pushBlock (minGain, numSamples);

    if (settings.vizEnabled)
    {
        const auto* mainSrc = dryBuffer.getReadPointer (0);
        const auto* sideSrc = detector.getFiltered().getReadPointer (0);
        const auto* outSrc  = buffer.getReadPointer (0);
        const auto* gainSrc = gainBuffer.getReadPointer (0);

        int nextTrigger = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            auto triggered = false;

            while (nextTrigger < triggerEvents.count && triggerEvents.positions[nextTrigger] == i)
            {
                triggered = true;
                ++nextTrigger;
            }

            // The visualiser is on the message thread's side of the fence; giving
            // it a NaN would poison a drawing path that has no business defending
            // itself against one.
            analysis.pushSample (sanitise (mainSrc[i]), sanitise (sideSrc[i]),
                                 sanitise (outSrc[i]),
                                 juce::jlimit (0.0f, 1.0f, sanitise (gainSrc[i])), triggered);
        }
    }

    juce::ignoreUnused (totalLatency);

    if (latency.consumeChange())
        triggerAsyncUpdate();
}

void SideForgeProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (buffer, midi);
    jassertfalse; // double precision is not advertised
}

juce::AudioProcessorEditor* SideForgeProcessor::createEditor()
{
    return new SideForgeEditor (*this);
}

void SideForgeProcessor::curveChanged()
{
    curveTables.publish (curveModel);
}

// -- programs ---------------------------------------------------------------
int SideForgeProcessor::getNumPrograms()                { return FactoryPresets::getNumPresets(); }
int SideForgeProcessor::getCurrentProgram()             { return currentProgram; }

void SideForgeProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, FactoryPresets::getNumPresets()))
        return;

    currentProgram = index;
    loadFactoryPreset (index);
}

const juce::String SideForgeProcessor::getProgramName (int index)
{
    return FactoryPresets::getName (index);
}

void SideForgeProcessor::changeProgramName (int, const juce::String&) {}

void SideForgeProcessor::loadFactoryPreset (int index)
{
    FactoryPresets::apply (index, apvts, curveModel);
    curveChanged();
}

// -- A/B --------------------------------------------------------------------
void SideForgeProcessor::storeToSlot (int slot)
{
    if (juce::isPositiveAndBelow (slot, 2))
        abSlots[slot] = captureState();
}

void SideForgeProcessor::recallFromSlot (int slot)
{
    if (juce::isPositiveAndBelow (slot, 2) && abSlots[slot].isValid())
        applyState (abSlots[slot]);
}

bool SideForgeProcessor::hasSlot (int slot) const
{
    return juce::isPositiveAndBelow (slot, 2) && abSlots[slot].isValid();
}

void SideForgeProcessor::setActiveSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);

    if (slot == activeSlot)
        return;

    // Remember what the current slot looks like before switching away from it, so
    // A/B always compares the two states the user actually built.
    abSlots[activeSlot] = captureState();
    activeSlot = slot;

    if (abSlots[activeSlot].isValid())
        applyState (abSlots[activeSlot]);
    else
        abSlots[activeSlot] = captureState();
}

// -- state ------------------------------------------------------------------
juce::ValueTree SideForgeProcessor::captureState (const juce::String& presetName)
{
    return PresetState::capture (apvts, curveModel, editorState, presetName);
}

bool SideForgeProcessor::applyState (const juce::ValueTree& tree)
{
    if (! PresetState::restore (tree, apvts, curveModel, editorState))
        return false;

    curveTables.publish (curveModel);
    return true;
}

void SideForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = captureState();

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SideForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    applyState (juce::ValueTree::fromXml (*xml));
}
} // namespace sf

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sf::SideForgeProcessor();
}
