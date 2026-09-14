#pragma once
#include "Controls.h"
#include "CurveEditor.h"
#include "MacroControls.h"
#include "MeterComponent.h"
#include "Panels.h"
#include "SpectrumView.h"
#include "Visualizer.h"
#include <memory>
#include <vector>

namespace sf
{
class SideForgeProcessor;

/**
    The whole interface.

    Structure, top to bottom:

      - a header with the mode selector, presets, A/B and bypass,
      - the four macros on the left, the large display in the middle, meters on the right,
      - a row of panel tabs and the expanded panel beneath it,
      - a status footer.

    One timer drives every animated element. The components it refreshes only ever
    read atomics and lock-free queues that the audio thread has already filled, so
    the cost of having the editor open does not land on the audio thread.
*/
class MainView : public juce::Component,
                 private juce::Timer
{
public:
    MainView (SideForgeProcessor&, juce::AudioProcessorEditor* hostEditor);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Central display pages. */
    enum class DisplayPage { Wave = 0, Curve, Spectrum };

    void setDisplayPage (DisplayPage page);

    /** Selects a lower panel by index. Public so the screenshot mode in the test
        runner can walk every page without simulating clicks on the tab strip. */
    void setActivePanelForScreenshot (int index) { setActivePanel (index); }

    int getNumPanels() const noexcept { return (int) panels.size(); }

    /** Runs one refresh tick immediately instead of waiting for the timer.
        Used by the screenshot mode, where no message loop is running. */
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;

    void buildHeader();
    void buildPanels();
    void layoutHeader (juce::Rectangle<int>);
    void layoutDisplay (juce::Rectangle<int>);
    void layoutPanels (juce::Rectangle<int>);

    void showPresetMenu();
    void savePresetToFile();
    void loadPresetFromFile();
    static juce::File getUserPresetDirectory();

    void setActivePanel (int index);
    void refreshRelevance();
    void refreshStatusText();

    juce::Rectangle<int> displayTabBounds (int index) const;
    juce::Rectangle<int> panelTabBounds (int index) const;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    SideForgeProcessor& processor;
    ControlContext context;

    // -- header ---------------------------------------------------------------
    std::unique_ptr<SFSegmentedControl> modeSelector;
    std::unique_ptr<SFToggle> bypassToggle;
    juce::TextButton presetButton { "Presets" };
    juce::TextButton slotAButton { "A" }, slotBButton { "B" }, copySlotButton { "A > B" };

    // -- macros ---------------------------------------------------------------
    std::unique_ptr<MacroControls> macros;

    // -- display --------------------------------------------------------------
    Visualizer visualizer;
    CurveEditor curveEditor;
    SpectrumView spectrumView;
    juce::TextButton freezeButton { "Freeze" };
    juce::TextButton deltaButton { "Delta" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    DisplayPage displayPage = DisplayPage::Wave;
    int displayTabHover = -1;

    // -- meters ---------------------------------------------------------------
    MeterComponent inputMeter { "IN", theme::destination };
    MeterComponent sidechainMeter { "SC", theme::sidechain };
    MeterComponent outputMeter { "OUT", theme::output };
    GainReductionMeterComponent reductionMeter;

    // -- panels ---------------------------------------------------------------
    // The panels live inside a viewport so that a panel with more controls than
    // fit — Bands, at a small window size — scrolls instead of clipping them.
    // Every panel stays a child of `panelHolder` whether or not it is the visible
    // one, so nothing in the interface is ever outside the component tree.
    juce::Viewport panelViewport;
    juce::Component panelHolder;
    std::vector<std::unique_ptr<PanelBase>> panels;
    int activePanel = 0;
    int panelTabHover = -1;

    // -- status ---------------------------------------------------------------
    juce::String statusLeft, statusRight;
    juce::Rectangle<int> headerArea, displayTabArea, panelTabArea, panelContentArea,
                         footerArea, macroArea;

    juce::TooltipWindow tooltips { this, 650 };

    int refreshCounter = 0;
    EngineSettings lastSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
} // namespace sf
