#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters/ParameterLayout.h"
#include "Model/CurveModel.h"
#include "Model/PresetState.h"

namespace sf
{
class SideForgeProcessor : public juce::AudioProcessor
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
    double getTailLengthSeconds() const override            { return 0.0; }

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

    /** Called by the editor after it edits the curve, so the audio thread picks
        up a freshly rendered table. Message thread only. */
    void curveChanged();

    /** True when the host has actually connected something to the sidechain bus. */
    bool isSidechainConnected() const noexcept { return sidechainConnected.load(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    ParamRefs   params;
    CurveModel  curveModel;
    CurveTableExchange curveTables;
    EditorState editorState;

    std::atomic<bool> sidechainConnected { false };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideForgeProcessor)
};
} // namespace sf
