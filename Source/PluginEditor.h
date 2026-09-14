#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "UI/LookAndFeel.h"
#include "UI/MainView.h"

namespace sf
{
class SideForgeProcessor;

/**
    The editor is deliberately thin: it owns the look and feel, the resize
    constrainer and the window size that gets saved with the session, and hands
    everything else to `MainView`.

    The whole interface is laid out proportionally from one design size, so the
    plugin scales cleanly from a small window on a laptop to a large one on a
    high-DPI display without any of the text becoming unreadable.
*/
class SideForgeEditor : public juce::AudioProcessorEditor
{
public:
    explicit SideForgeEditor (SideForgeProcessor&);
    ~SideForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The size the layout is designed at; the window scales relative to this. */
    static constexpr int kDesignWidth  = 1180;
    static constexpr int kDesignHeight = 760;
    static constexpr int kMinWidth     = 980;
    static constexpr int kMinHeight    = 660;

private:
    SideForgeProcessor& proc;
    SideForgeLookAndFeel lookAndFeel;
    std::unique_ptr<MainView> mainView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideForgeEditor)
};
} // namespace sf
