#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace sf
{
SideForgeEditor::SideForgeEditor (SideForgeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setResizable (true, true);
    setResizeLimits (820, 560, 4096, 2600);
    setSize (proc.getEditorState().width, proc.getEditorState().height);
}

SideForgeEditor::~SideForgeEditor() = default;

void SideForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d1117));
    g.setColour (juce::Colour (0xff58e6ff));
    g.setFont (juce::FontOptions (28.0f));
    g.drawText ("SideForge", getLocalBounds(), juce::Justification::centred);
}

void SideForgeEditor::resized()
{
    proc.getEditorState().width  = getWidth();
    proc.getEditorState().height = getHeight();
}
} // namespace sf
