#pragma once
#include "../DSP/AnalysisBus.h"
#include "Theme.h"
#include <vector>

namespace sf
{
/**
    The scrolling centre of the interface.

    It draws, in the colour language the whole plugin shares:

      - the destination waveform (cyan),
      - the sidechain waveform the detector actually hears (orange -> magenta),
      - the gain-reduction envelope over it (violet),
      - the output (green),
      - a tick wherever a trigger fired.

    All of this comes from `AnalysisBus`, which the audio thread fills with one
    small decimated column per ~1.25 ms. The component does no analysis of its own
    and never reads a DSP object, so having the editor open costs the audio thread
    a handful of comparisons per sample and nothing else.
*/
class Visualizer : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    Visualizer();

    void setSource (AnalysisBus* bus) { source = bus; }

    /** Drains the bus and repaints. Called from the editor's timer. */
    void refresh();

    /** Freeze stops the display without stopping the plugin. */
    void setFrozen (bool shouldFreeze);
    bool isFrozen() const noexcept { return frozen; }

    /** Reduced animation halves the repaint rate and drops the glow passes. */
    void setReducedAnimation (bool shouldReduce) { reduced = shouldReduce; repaint(); }

    /** Turns the display off entirely; the bus is still drained so it cannot
        back up and the timeline stays honest when it is switched back on. */
    void setEnabled (bool shouldBeEnabled);

    void setShowOutput (bool shouldShow) { showOutput = shouldShow; repaint(); }
    void setShowSidechain (bool shouldShow) { showSidechain = shouldShow; repaint(); }

    /** How many seconds of history to show. */
    void setTimeWindowSeconds (double seconds);
    double getTimeWindowSeconds() const noexcept { return windowSeconds; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Right-click menu: freeze, how much history to show, and which layers are
        drawn. These have nowhere else to live, since none of them is a
        plugin parameter. */
    void showContextMenu();

    std::function<void()> onFreezeToggled;

private:
    void drawGrid (juce::Graphics&, juce::Rectangle<float>) const;
    void drawWaveform (juce::Graphics&, juce::Rectangle<float>, int first, int count,
                       bool useOutput) const;
    void drawSidechain (juce::Graphics&, juce::Rectangle<float>, int first, int count) const;
    void drawReductionEnvelope (juce::Graphics&, juce::Rectangle<float>, int first, int count) const;
    void drawTriggerMarkers (juce::Graphics&, juce::Rectangle<float>, int first, int count) const;
    void drawLegend (juce::Graphics&, juce::Rectangle<float>) const;

    const WaveformPoint& columnAt (int index) const;

    static constexpr int kHistorySize = 4096;

    AnalysisBus* source = nullptr;
    std::vector<WaveformPoint> history;
    int writeIndex = 0;
    int filled = 0;

    bool frozen = false;
    bool reduced = false;
    bool enabled = true;
    bool showOutput = true;
    bool showSidechain = true;
    double windowSeconds = 2.0;
    int refreshCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Visualizer)
};
} // namespace sf
