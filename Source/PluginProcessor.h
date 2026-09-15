#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters/ParameterLayout.h"
#include "Parameters/MacroMapping.h"
#include "Parameters/MidiLearn.h"
#include "Parameters/BandMatrixParameters.h"
#include "DSP/BandMatrix.h"
#include "Model/CurveModel.h"
#include "Model/PresetState.h"
#include "DSP/AnalysisBus.h"
#include "DSP/CompressorEngine.h"
#include "DSP/CurveEngine.h"
#include "DSP/DelayLine.h"
#include "DSP/LatencyManager.h"
#include "DSP/Metering.h"
#include "DSP/MultibandProcessor.h"
#include "DSP/SidechainDetector.h"
#include "DSP/SpectralDucker.h"
#include "DSP/TransportInfo.h"
#include "DSP/TriggerEngine.h"
#include "Utilities/SmoothedValueHelpers.h"

namespace sf
{
class SideForgeProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    SideForgeProcessor();
    ~SideForgeProcessor() override;

    // -- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override             { return "SideForge"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // -- SideForge -----------------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    ParamRefs&   getParams() noexcept                       { return params; }
    CurveModel&  getCurveModel() noexcept                   { return curveModel; }
    EditorState& getEditorState() noexcept                  { return editorState; }
    juce::UndoManager& getUndoManager() noexcept            { return undoManager; }

    /** Message thread: re-renders the curve table the audio thread reads. */
    void curveChanged();

    /** True when the host actually connected something to the sidechain bus. */
    bool isSidechainConnected() const noexcept { return sidechainConnected.load(); }
    /** True when that bus is also carrying signal above the noise floor. */
    bool isSidechainReceivingAudio() const noexcept { return sidechainHasSignal.load(); }

    // -- things the editor reads ---------------------------------------------
    MeterBank&       getMeters() noexcept          { return meters; }
    AnalysisBus&     getAnalysisBus() noexcept     { return analysis; }
    SpectralDucker&  getSpectralDucker() noexcept  { return spectral; }
    CurveEngine&     getCurveEngine() noexcept     { return curveEngine; }
    TriggerEngine&   getTriggerEngine() noexcept   { return triggerEngine; }
    MultibandProcessor& getMultiband() noexcept    { return multiband; }
    BandMatrix&      getBandMatrix() noexcept      { return bandMatrix; }
    BandMatrixParamRefs& getBandMatrixParams() noexcept { return bandMatrixParams; }

    /** Which of the two top-level signal paths is running. */
    RoutingMode getRoutingMode() const noexcept { return readRoutingMode (bandMatrixParams); }

    /** Current reported latency, for the UI's routing panel. */
    int getReportedLatencySamples() const noexcept { return latency.getTotalSamples(); }

    /** Message thread: fires the GUI's test trigger. */
    void fireManualTrigger() { triggerEngine.fireManualTrigger(); }

    /** MIDI CC bindings, shared by every control's right-click menu. */
    MidiLearnManager& getMidiLearn() noexcept { return midiLearn; }

    // -- A/B comparison ------------------------------------------------------
    void storeToSlot (int slot);
    void recallFromSlot (int slot);
    bool hasSlot (int slot) const;
    int  getActiveSlot() const noexcept { return activeSlot; }
    void setActiveSlot (int slot);

    // -- presets -------------------------------------------------------------
    void loadFactoryPreset (int index);
    juce::ValueTree captureState (const juce::String& presetName = {});
    bool applyState (const juce::ValueTree& tree);

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void applySettings (const EngineSettings& s);
    void processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                       const TransportInfo& transport, double quartersThisBlock);

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    ParamRefs   params;
    BandMatrixParamRefs bandMatrixParams;
    CurveModel  curveModel;
    CurveTableExchange curveTables;
    EditorState editorState;
    juce::AudioProcessorParameter* bypassParameter = nullptr;
    MidiLearnManager midiLearn;

    // -- engines -------------------------------------------------------------
    SidechainDetector  detector;
    CompressorEngine   compressor;
    CurveEngine        curveEngine;
    TriggerEngine      triggerEngine;
    MultibandProcessor multiband;
    SpectralDucker     spectral;
    BandMatrix         bandMatrix;
    LatencyManager     latency;
    MeterBank          meters;
    AnalysisBus        analysis;

    DelayLine dryDelay;        // keeps mix / bypass phase-coherent
    DelayLine lookaheadDelay;  // the wet path's lookahead
    DelayLine scDelay;         // keeps the sidechain aligned with the delayed audio

    // -- preallocated working buffers ----------------------------------------
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> scBuffer;        // raw sidechain, pre-detector
    juce::AudioBuffer<float> gainBuffer;      // broadband gain, per channel
    juce::AudioBuffer<float> curveGainBuffer; // single-channel curve envelope
    juce::AudioBuffer<float> scAligned;       // filtered sidechain, lookahead-aligned
    juce::AudioBuffer<float> scMatrix;        // sidechain for the Band Matrix, gain-staged only

    ParamSmoother inputGain, outputGain, mixAmount, makeupGain;
    CrossfadeRamp bypassFade;

    TransportInfo lastTransport;
    TriggerEvents triggerEvents;
    EngineSettings settings;
    juce::MidiBuffer chunkMidi;

    double preparedSampleRate = 44100.0;
    int    preparedBlockSize = 512;
    int    preparedChannels = 2;
    bool   prepared = false;

    std::atomic<bool> sidechainConnected { false };
    std::atomic<bool> sidechainHasSignal { false };
    // A sparse trigger source is silent between hits, so the "receiving"
    // indicator holds for a moment rather than flickering off between kicks.
    int sidechainSignalHold = 0;

    int currentProgram = 0;
    int activeSlot = 0;
    juce::ValueTree abSlots[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideForgeProcessor)
};
} // namespace sf
