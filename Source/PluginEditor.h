#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace sf
{
class SideForgeProcessor;

class SideForgeEditor : public juce::AudioProcessorEditor
{
public:
    explicit SideForgeEditor (SideForgeProcessor&);
    ~SideForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SideForgeProcessor& proc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideForgeEditor)
};
} // namespace sf
