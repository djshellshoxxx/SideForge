#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sf
{
SideForgeProcessor::SideForgeProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                                .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, PresetState::paramsTag, createParameterLayout())
{
    params.attach (apvts);
    curveTables.publish (curveModel);
}

SideForgeProcessor::~SideForgeProcessor() = default;

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

void SideForgeProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::ignoreUnused (sampleRate, maximumExpectedSamplesPerBlock);

    sidechainConnected.store (getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled());
    setLatencySamples (0);
}

void SideForgeProcessor::releaseResources() {}

void SideForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    const auto totalIn  = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Phase 1: unity pass-through. Engines are attached in later phases.
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
int SideForgeProcessor::getNumPrograms()                                 { return 1; }
int SideForgeProcessor::getCurrentProgram()                              { return currentProgram; }
void SideForgeProcessor::setCurrentProgram (int index)                   { currentProgram = index; }
const juce::String SideForgeProcessor::getProgramName (int)              { return "Default"; }
void SideForgeProcessor::changeProgramName (int, const juce::String&)    {}

// -- state ------------------------------------------------------------------
void SideForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = PresetState::capture (apvts, curveModel, editorState);

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

    const auto tree = juce::ValueTree::fromXml (*xml);

    if (PresetState::restore (tree, apvts, curveModel, editorState))
        curveTables.publish (curveModel);
}
} // namespace sf

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sf::SideForgeProcessor();
}
