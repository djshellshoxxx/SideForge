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
    bandMatrixParams.attach (apvts);
    bypassParameter = apvts.getParameter (pid::bypass);
    midiLearn.attach (apvts);
    curveTables.publish (curveModel);

    // Applies learned CC movements on the message thread, so MIDI control keeps
    // working when the editor is closed.
    startTimerHz (30);
}

SideForgeProcessor::~SideForgeProcessor()
{
    stopTimer();
    cancelPendingUpdate();
}

void SideForgeProcessor::timerCallback()
{
    midiLearn.applyPendingChanges();
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
    scMatrix.setSize (kMaxChannels, preparedBlockSize, false, true, true);
    chunkMidi.ensureSize (4096);

    // -- engines --------------------------------------------------------------
    detector.prepare (preparedSampleRate, preparedBlockSize, kMaxChannels);
    compressor.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
    curveEngine.prepare (preparedSampleRate, preparedBlockSize);
    triggerEngine.prepare (preparedSampleRate, preparedBlockSize, kMaxChannels);
    multiband.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
    spectral.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
    bandMatrix.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
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
    sidechainSignalHold = 0;

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

    midiLearn.processBlock (midi);

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

        if (scEnabled && magnitude > kSidechainSignalFloor)
            sidechainSignalHold = (int) std::lround (preparedSampleRate * 0.5);
        else
            sidechainSignalHold = juce::jmax (0, sidechainSignalHold - numSamples);

        sidechainHasSignal.store (scEnabled && sidechainSignalHold > 0);
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

    // The *raw* sidechain, not the detector's filtered copy: the meter should
    // read what the user is actually sending, and a steep detector filter can
    // overshoot full scale on a transient and light the clip indicator for a
    // signal that never clipped. The filtered view is available on Audition.
    for (int ch = 0; ch < MeterBank::kChannels; ++ch)
        meters.sidechain[ch].processBlock (scBuffer.getReadPointer (ch), numSamples);

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

    // The curve engine also feeds any Band Matrix band set to Curve mode, so it
    // runs whenever something needs it rather than only in full-range Curve mode.
    const auto matrixActive = (readRoutingMode (bandMatrixParams) == RoutingMode::BandMatrix);
    auto matrixWantsCurve = false;

    if (matrixActive)
        for (int b = 0; b < kMaxMatrixBands; ++b)
            matrixWantsCurve = matrixWantsCurve
                            || readEnum<BandProcessorMode> (bandMatrixParams.bands[b].mode) == BandProcessorMode::Curve;

    const float* curveGain = nullptr;

    if (settings.useCurve || matrixWantsCurve)
    {
        curveEngine.setTable (&curveTables.acquire());
        curveEngine.process (numSamples, transport, quartersThisBlock, triggerEvents,
                             curveGainBuffer.getWritePointer (0));
        curveGain = curveGainBuffer.getReadPointer (0);
    }

    if (settings.useCurve && curveGain != nullptr)
    {
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

    // ---- routing: Full Range, or the Band Matrix ---------------------------
    int totalLatency = 0;

    if (matrixActive)
    {
        // The matrix does its own splitting, detection, dynamics and latency
        // alignment, so the full-range broadband and spectral stages sit this
        // one out entirely.
        const auto cfg = qualityConfig (settings.quality);
        const auto depthScale = readBool (params.macrosEnabled)
                                    ? juce::jlimit (0.0f, 2.0f, readFloat (params.macroDepth) / 50.0f)
                                    : 1.0f;

        // The global Timing macro has already been folded into the full-range
        // compressor times, so its effect is recovered as a ratio and applied to
        // the band times too. One macro, both routes.
        const auto timingScale = settings.comp.releaseMs
                                     / juce::jmax (1.0e-3f, readFloat (params.compRelease));

        auto matrixSettings = buildBandMatrixSettings (bandMatrixParams, settings.spectral,
                                                       cfg.fftOrder, cfg.overlap,
                                                       depthScale, timingScale);
        bandMatrix.setSettings (matrixSettings);

        // Gain-staged but not filtered: each band owns its own detector filter,
        // and filtering here as well would apply it twice.
        const auto scGain = juce::Decibels::decibelsToGain (settings.scGainDb);

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            const auto* src = scBuffer.getReadPointer (ch);
            auto* dst = scMatrix.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
                dst[i] = src[i] * scGain;
        }

        bandMatrix.process (wetBuffer, scMatrix, curveGain, mainChannels, numSamples);

        totalLatency = latency.updateExplicit (bandMatrix.getLatencySamples());

        // The full-range lookahead line is idle here; zeroing it means switching
        // back does not replay whatever it was holding.
        lookaheadDelay.setDelay (0);
        scDelay.setDelay (0);

        // The visualiser draws one gain trace; in matrix mode that is the deepest
        // reduction anywhere in the matrix.
        for (int ch = 0; ch < mainChannels; ++ch)
            juce::FloatVectorOperations::fill (gainBuffer.getWritePointer (ch),
                                               bandMatrix.getLastMinGain(), numSamples);
    }
    else
    {
        // ---- latency --------------------------------------------------------
        totalLatency = latency.update (settings.lookaheadMs, settings.quality,
                                       settings.spectralActive);
        const auto lookaheadSamples = latency.getLookaheadSamples();

        lookaheadDelay.setDelay (lookaheadSamples);
        scDelay.setDelay (lookaheadSamples);

        lookaheadDelay.process (wetBuffer, numSamples);

        // ---- broadband stage ------------------------------------------------
        multiband.process (wetBuffer, gainBuffer, mainChannels, numSamples);

        // ---- spectral stage -------------------------------------------------
        if (settings.spectralActive)
        {
            // The audio has been delayed by the lookahead, so the sidechain the
            // collision map sees has to be delayed by the same amount to stay
            // aligned.
            const auto& filtered = detector.getFiltered();

            for (int ch = 0; ch < kMaxChannels; ++ch)
                scAligned.copyFrom (ch, 0, filtered,
                                    juce::jmin (ch, filtered.getNumChannels() - 1), 0, numSamples);

            scDelay.process (scAligned, numSamples);
            spectral.process (wetBuffer, scAligned, mainChannels, numSamples);
        }
    }

    // The dry path is delayed by whatever the active route reported, so mix,
    // bypass and delta stay sample-aligned in both modes.
    dryDelay.setDelay (totalLatency);
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

    const auto minGain = matrixActive
                             ? bandMatrix.getLastMinGain()
                             : (settings.spectralActive
                                    ? juce::jmin (multiband.getLastMinGain(), spectral.getLastMinGain())
                                    : multiband.getLastMinGain());
    meters.gainReduction.pushBlock (minGain, numSamples);

    // Nothing is watching during an offline bounce, so the analysis bus is not
    // worth filling; skipping it also keeps renders bit-identical run to run.
    if (settings.vizEnabled && ! isNonRealtime())
    {
        const auto* mainSrc = dryBuffer.getReadPointer (0);
        const auto* sideSrc = scBuffer.getReadPointer (0);
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

            // Raw samples for the Band Matrix display, which does its own FFT on
            // the message thread. The dry signal goes along too, so the display
            // can draw the original as a faint reference behind the processed
            // one and the carve is visible rather than implied.
            analysis.getRawTap().push (sanitise (mainSrc[i]), sanitise (outSrc[i]),
                                       sanitise (sideSrc[i]));
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
    auto tree = PresetState::capture (apvts, curveModel, editorState, presetName);
    tree.appendChild (midiLearn.toValueTree(), nullptr);
    return tree;
}

bool SideForgeProcessor::applyState (const juce::ValueTree& tree)
{
    if (! PresetState::restore (tree, apvts, curveModel, editorState))
        return false;

    // Absent in an older session, which simply means no controllers were learned.
    if (auto midiTree = tree.getChildWithName (MidiLearnManager::stateTag); midiTree.isValid())
        midiLearn.fromValueTree (midiTree);

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
