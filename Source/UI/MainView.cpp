#include "MainView.h"
#include "LookAndFeel.h"
#include "../PluginProcessor.h"
#include "../Model/FactoryPresets.h"

namespace sf
{
namespace
{
constexpr int kHeaderHeight     = 50;
constexpr int kMacroWidth       = 196;
constexpr int kMeterWidth       = 132;
constexpr int kDisplayTabHeight = 26;
constexpr int kPanelTabHeight   = 28;
constexpr int kFooterHeight     = 22;
constexpr int kPanelHeight      = 212;   // two rows of controls without scrolling

const char* const kDisplayPageNames[] = { "Waveform", "Curve", "Spectrum", "Matrix" };
constexpr int kNumDisplayPages = 4;

enum PresetMenuId
{
    idPresetNone = 0,
    idSavePreset = 900,
    idLoadPreset,
    idOpenFolder,
    idInitialise,
    idFactoryBase = 1000,
    idUserBase    = 2000,
};
} // namespace

// ===========================================================================

MainView::MainView (SideForgeProcessor& processorToUse, juce::AudioProcessorEditor* hostEditor)
    : processor (processorToUse)
{
    context.apvts = &processor.getAPVTS();
    context.midiLearn = &processor.getMidiLearn();
    // The editor is how a control reaches the host's own parameter context menu.
    context.editor = hostEditor;

    buildHeader();

    // -- macros ---------------------------------------------------------------
    macros = std::make_unique<MacroControls> (context);
    addAndMakeVisible (*macros);

    // -- display --------------------------------------------------------------
    visualizer.setSource (&processor.getAnalysisBus());
    spectrumView.setSource (&processor.getSpectralDucker());
    curveEditor.setModel (&processor.getCurveModel());
    curveEditor.onCurveChanged = [this] { processor.curveChanged(); };

    bandMatrixView = std::make_unique<BandMatrixView> (processor, context);
    bandMatrixView->onBandSelected = [this] (int band)
    {
        if (bandMatrixPanel != nullptr)
            bandMatrixPanel->setSelectedBand (band);
    };

    addAndMakeVisible (visualizer);
    addChildComponent (curveEditor);
    addChildComponent (spectrumView);
    addChildComponent (*bandMatrixView);

    visualizer.setFrozen (processor.getEditorState().freezeViz);
    visualizer.onFreezeToggled = [this]
    {
        processor.getEditorState().freezeViz = visualizer.isFrozen();
        spectrumView.setFrozen (visualizer.isFrozen());
        freezeButton.setToggleState (visualizer.isFrozen(), juce::dontSendNotification);
    };

    freezeButton.setClickingTogglesState (true);
    freezeButton.setTooltip ("Holds the display still without affecting the audio.");
    freezeButton.onClick = [this] { visualizer.setFrozen (freezeButton.getToggleState()); };
    addAndMakeVisible (freezeButton);

    deltaButton.setClickingTogglesState (true);
    deltaButton.setTooltip ("Overlays the spectrum of the signal the plugin is removing.");
    deltaButton.onClick = [this] { spectrumView.setShowDelta (deltaButton.getToggleState()); };
    addAndMakeVisible (deltaButton);

    undoButton.setTooltip ("Undo the last curve edit (Ctrl+Z).");
    undoButton.onClick = [this] { curveEditor.undo(); };
    addChildComponent (undoButton);

    redoButton.setTooltip ("Redo the last undone curve edit (Ctrl+Shift+Z).");
    redoButton.onClick = [this] { curveEditor.redo(); };
    addChildComponent (redoButton);

    // -- meters ---------------------------------------------------------------
    auto& meters = processor.getMeters();
    inputMeter.setSource (&meters.input[0], &meters.input[1]);
    sidechainMeter.setSource (&meters.sidechain[0], &meters.sidechain[1]);
    outputMeter.setSource (&meters.output[0], &meters.output[1]);
    reductionMeter.setSource (&meters.gainReduction);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (sidechainMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (reductionMeter);

    buildPanels();
    setActivePanel (juce::jlimit (0, (int) panels.size() - 1, processor.getEditorState().openPanel));

    startTimerHz (45);
    refreshRelevance();
}

MainView::~MainView()
{
    stopTimer();
    curveEditor.setModel (nullptr);
}

void MainView::buildHeader()
{
    modeSelector = std::make_unique<SFSegmentedControl> (context, pid::mode, theme::accent,
        "Which engines produce the gain signal: the compressor, the drawn curve, "
        "the spectral stage, or the compressor and curve together plus spectral");
    modeSelector->onDisabledOptionClicked = [this] (int index)
    {
        // Spectral and Hybrid need an STFT, which Live quality does not run. Rather
        // than refusing the click, move to the quality that makes it work.
        if (auto* quality = context.getParameter (pid::quality))
        {
            quality->beginChangeGesture();
            quality->setValueNotifyingHost (quality->convertTo0to1 ((float) Quality::Studio));
            quality->endChangeGesture();
        }

        if (auto* mode = context.getParameter (pid::mode))
        {
            mode->beginChangeGesture();
            mode->setValueNotifyingHost (mode->convertTo0to1 ((float) index));
            mode->endChangeGesture();
        }
    };
    addAndMakeVisible (*modeSelector);

    // Full Range or Band Matrix. It sits next to the engine mode because the two
    // together are what decide the signal path.
    routingSelector = std::make_unique<SFSegmentedControl> (context, pid::routingMode, theme::ok,
        "Full Range processes the whole spectrum with one detector. Band Matrix splits it "
        "into up to four bands, each with its own processor, timing and detector");
    addAndMakeVisible (*routingSelector);

    bypassToggle = std::make_unique<SFToggle> (context, pid::bypass, "Bypass",
                                               "Click-free global bypass. Latency stays reported, so the host "
                                               "keeps the track aligned.", theme::warning);
    addAndMakeVisible (*bypassToggle);

    presetButton.setTooltip ("Factory presets, and saving or loading your own.");
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    slotAButton.setClickingTogglesState (true);
    slotBButton.setClickingTogglesState (true);
    slotAButton.setRadioGroupId (1);
    slotBButton.setRadioGroupId (1);
    slotAButton.setToggleState (true, juce::dontSendNotification);
    slotAButton.setTooltip ("Compare two complete settings. Switching stores what you had in the slot you leave.");
    slotBButton.setTooltip (slotAButton.getTooltip());
    slotAButton.onClick = [this] { processor.setActiveSlot (0); refreshRelevance(); };
    slotBButton.onClick = [this] { processor.setActiveSlot (1); refreshRelevance(); };

    copySlotButton.setTooltip ("Copies the current settings into the other slot, so you can A/B a change.");
    copySlotButton.onClick = [this]
    {
        processor.storeToSlot (processor.getActiveSlot() == 0 ? 1 : 0);
    };

    addAndMakeVisible (slotAButton);
    addAndMakeVisible (slotBButton);
    addAndMakeVisible (copySlotButton);
}

void MainView::buildPanels()
{
    panelViewport.setViewedComponent (&panelHolder, false);
    panelViewport.setScrollBarsShown (true, false);
    panelViewport.setScrollBarThickness (9);
    addAndMakeVisible (panelViewport);

    panels.push_back (std::make_unique<CompressorPanel> (context));
    panels.push_back (std::make_unique<TriggerPanel> (context, [this] { processor.fireManualTrigger(); }));
    panels.push_back (std::make_unique<CurvePanel> (context));
    panels.push_back (std::make_unique<SpectrumPanel> (context));
    panels.push_back (std::make_unique<BandsPanel> (context));

    {
        auto matrixPanel = std::make_unique<BandMatrixPanel> (context, [this] (int band)
        {
            if (bandMatrixView != nullptr)
                bandMatrixView->setSelectedBand (band);
        });

        bandMatrixPanel = matrixPanel.get();
        panels.push_back (std::move (matrixPanel));
    }

    panels.push_back (std::make_unique<RoutingPanel> (context));
    panels.push_back (std::make_unique<OutputPanel> (context));

    for (auto& panel : panels)
        panelHolder.addChildComponent (*panel);
}

void MainView::setActivePanel (int index)
{
    activePanel = juce::jlimit (0, (int) panels.size() - 1, index);
    processor.getEditorState().openPanel = activePanel;

    for (int i = 0; i < (int) panels.size(); ++i)
        panels[(size_t) i]->setVisible (i == activePanel);

    resized();
    repaint();
}

void MainView::setDisplayPage (DisplayPage page)
{
    displayPage = page;

    visualizer.setVisible (page == DisplayPage::Wave);
    curveEditor.setVisible (page == DisplayPage::Curve);
    spectrumView.setVisible (page == DisplayPage::Spectrum);

    if (bandMatrixView != nullptr)
        bandMatrixView->setVisible (page == DisplayPage::Matrix);

    undoButton.setVisible (page == DisplayPage::Curve);
    redoButton.setVisible (page == DisplayPage::Curve);
    deltaButton.setVisible (page == DisplayPage::Spectrum);

    resized();
    repaint();
}

// ===========================================================================
//  Presets
// ===========================================================================

juce::File MainView::getUserPresetDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("SideForge")
                         .getChildFile ("Presets");

    if (! directory.exists())
        directory.createDirectory();

    return directory;
}

void MainView::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    menu.addSectionHeader ("Factory");

    for (int i = 0; i < FactoryPresets::getNumPresets(); ++i)
        menu.addItem (idFactoryBase + i, FactoryPresets::getName (i));

    auto userFiles = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.sideforge");
    userFiles.sort();

    if (! userFiles.isEmpty())
    {
        menu.addSectionHeader ("User");

        for (int i = 0; i < userFiles.size(); ++i)
            menu.addItem (idUserBase + i, userFiles[i].getFileNameWithoutExtension());
    }

    menu.addSeparator();
    menu.addItem (idSavePreset, "Save preset...");
    menu.addItem (idLoadPreset, "Load preset...");
    menu.addItem (idOpenFolder, "Open preset folder");
    menu.addSeparator();
    menu.addItem (idInitialise, "Initialise (reset everything)");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [this, userFiles] (int result)
    {
        if (result == idPresetNone)
            return;

        if (result >= idUserBase)
        {
            const auto index = result - idUserBase;

            if (juce::isPositiveAndBelow (index, userFiles.size()))
                if (auto xml = juce::XmlDocument::parse (userFiles[index]))
                    processor.applyState (juce::ValueTree::fromXml (*xml));
        }
        else if (result >= idFactoryBase)
        {
            processor.setCurrentProgram (result - idFactoryBase);
        }
        else
        {
            switch (result)
            {
                case idSavePreset: savePresetToFile(); break;
                case idLoadPreset: loadPresetFromFile(); break;
                case idOpenFolder: getUserPresetDirectory().revealToUser(); break;

                case idInitialise:
                    for (auto* parameter : processor.getAPVTS().processor.getParameters())
                        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                            withID->setValueNotifyingHost (withID->getDefaultValue());

                    processor.getCurveModel().setPresetShape (0);
                    processor.curveChanged();
                    break;

                default: break;
            }
        }

        refreshRelevance();
        repaint();
    });
}

void MainView::savePresetToFile()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Save SideForge preset",
                                                        getUserPresetDirectory(),
                                                        "*.sideforge");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        const auto state = processor.captureState (file.getFileNameWithoutExtension());

        if (auto xml = state.createXml())
            xml->writeTo (file.withFileExtension ("sideforge"));
    });
}

void MainView::loadPresetFromFile()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Load SideForge preset",
                                                        getUserPresetDirectory(),
                                                        "*.sideforge");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File() || ! file.existsAsFile())
            return;

        if (auto xml = juce::XmlDocument::parse (file))
            processor.applyState (juce::ValueTree::fromXml (*xml));

        refreshRelevance();
        repaint();
    });
}

// ===========================================================================
//  Refresh
// ===========================================================================

void MainView::timerCallback()
{
    ++refreshCounter;

    visualizer.refresh();

    if (displayPage == DisplayPage::Spectrum)
        spectrumView.refresh();
    else if (refreshCounter % 3 == 0)
        spectrumView.refresh();   // keep it warm so switching pages is not blank

    if (bandMatrixView != nullptr)
    {
        // The matrix display runs its own FFT, so it only analyses while it is
        // the page on screen.
        bandMatrixView->setAnalysisEnabled (displayPage == DisplayPage::Matrix);

        if (displayPage == DisplayPage::Matrix || refreshCounter % 6 == 0)
            bandMatrixView->refresh();
    }

    inputMeter.refresh();
    sidechainMeter.refresh();
    outputMeter.refresh();
    reductionMeter.refresh();

    if (displayPage == DisplayPage::Curve)
        curveEditor.setPlayheadPhase (processor.getCurveEngine().isRunning()
                                          ? processor.getCurveEngine().getPhase() : -1.0f);

    // Relevance and badges change rarely; four times a second is plenty and keeps
    // the per-frame cost of the editor down.
    if (refreshCounter % 11 == 0)
    {
        refreshRelevance();

        for (auto& panel : panels)
            panel->refreshBadges();

        macros->refreshBadges();
        bypassToggle->refreshBadge();

        undoButton.setEnabled (curveEditor.canUndo());
        redoButton.setEnabled (curveEditor.canRedo());

        refreshStatusText();
    }
}

void MainView::refreshRelevance()
{
    const auto settings = buildEngineSettings (processor.getParams());

    for (auto& panel : panels)
        panel->updateRelevance (settings);

    const auto spectralAvailable = qualityConfig (settings.quality).spectralAvailable;
    modeSelector->setOptionEnabled ((int) Mode::Spectral, spectralAvailable,
                                    "Spectral processing needs Studio quality or higher. "
                                    "Click to switch and select it.");
    modeSelector->setOptionEnabled ((int) Mode::Hybrid, spectralAvailable,
                                    "Hybrid mode needs Studio quality or higher. "
                                    "Click to switch and select it.");

    macros->setMacrosActive (readBool (processor.getParams().macrosEnabled));

    if (bandMatrixView != nullptr)
        bandMatrixView->setReducedAnimation (settings.vizReduced);

    visualizer.setEnabled (settings.vizEnabled);
    visualizer.setReducedAnimation (settings.vizReduced);
    visualizer.setShowSidechain (processor.isSidechainConnected()
                                 || settings.scSource == ScSource::Internal);
    spectrumView.setReducedAnimation (settings.vizReduced);
    spectrumView.setFrequencyRange (settings.spectral.lowFreqHz, settings.spectral.highFreqHz);
    spectrumView.setCrossovers (settings.bands.bandMode == BandMode::Three,
                                settings.bands.xover1Hz, settings.bands.xover2Hz);

    curveEditor.setGrid (readInt (processor.getParams().curveSnap),
                         readBool (processor.getParams().curveSnapEnable));

    slotAButton.setToggleState (processor.getActiveSlot() == 0, juce::dontSendNotification);
    slotBButton.setToggleState (processor.getActiveSlot() == 1, juce::dontSendNotification);

    lastSettings = settings;
}

void MainView::refreshStatusText()
{
    juce::String left;

    if (readEnum<ScSource> (processor.getParams().scSource) == ScSource::Internal)
    {
        left = "Sidechain: internal (keyed from this track)";
    }
    else if (! processor.isSidechainConnected())
    {
        left = "Sidechain: not connected  -  route a send to this plugin's sidechain input";
    }
    else if (! processor.isSidechainReceivingAudio())
    {
        left = "Sidechain: connected, no signal";
    }
    else
    {
        left = "Sidechain: receiving";
    }

    const auto latencySamples = processor.getReportedLatencySamples();
    const auto sampleRate = juce::jmax (1.0, processor.getSampleRate());

    juce::String right = "Latency " + juce::String (latencySamples) + " samples ("
                       + juce::String (1000.0 * latencySamples / sampleRate, 2) + " ms)";

    if (context.midiLearn != nullptr && context.midiLearn->isLearning())
        right = "Move a MIDI controller to assign it...  " + right;
    else if (context.midiLearn != nullptr && context.midiLearn->getNumMappings() > 0)
        right = juce::String (context.midiLearn->getNumMappings()) + " MIDI  -  " + right;

    if (left != statusLeft || right != statusRight)
    {
        statusLeft = left;
        statusRight = right;
        repaint (footerArea);
    }
}

// ===========================================================================
//  Layout
// ===========================================================================

juce::Rectangle<int> MainView::displayTabBounds (int index) const
{
    const auto width = 84;
    return { displayTabArea.getX() + index * (width + 4), displayTabArea.getY(),
             width, displayTabArea.getHeight() };
}

juce::Rectangle<int> MainView::panelTabBounds (int index) const
{
    if (panels.empty())
        return {};

    const auto width = panelTabArea.getWidth() / (int) panels.size();

    return { panelTabArea.getX() + index * width, panelTabArea.getY(),
             width - 3, panelTabArea.getHeight() };
}

void MainView::resized()
{
    auto area = getLocalBounds();

    headerArea = area.removeFromTop (kHeaderHeight);
    layoutHeader (headerArea);

    footerArea = area.removeFromBottom (kFooterHeight);

    auto panelArea = area.removeFromBottom (kPanelHeight + kPanelTabHeight);
    panelTabArea = panelArea.removeFromTop (kPanelTabHeight).reduced (theme::gap, 2);
    layoutPanels (panelArea.reduced (theme::gap, 0));

    layoutDisplay (area.reduced (theme::gap, theme::gapSmall));
}

void MainView::layoutHeader (juce::Rectangle<int> area)
{
    area = area.reduced (theme::gap, 9);

    // Title on the left, then the mode selector, with the utility cluster right.
    area.removeFromLeft (128);

    auto right = area.removeFromRight (330);

    bypassToggle->setBounds (right.removeFromRight (86).reduced (0, 3));
    right.removeFromRight (theme::gapSmall);
    copySlotButton.setBounds (right.removeFromRight (56).reduced (0, 4));
    slotBButton.setBounds (right.removeFromRight (30).reduced (0, 4));
    slotAButton.setBounds (right.removeFromRight (30).reduced (0, 4));
    right.removeFromRight (theme::gapSmall);
    presetButton.setBounds (right.removeFromRight (100).reduced (0, 4));

    modeSelector->setBounds (area.removeFromLeft (juce::jmin (260, area.getWidth())).reduced (0, 2));
    area.removeFromLeft (theme::gap);
    routingSelector->setBounds (area.removeFromLeft (juce::jmin (176, area.getWidth())).reduced (0, 2));
}

void MainView::layoutDisplay (juce::Rectangle<int> area)
{
    // Macros on the left.
    macroArea = area.removeFromLeft (kMacroWidth);
    macros->setBounds (macroArea);

    area.removeFromLeft (theme::gap);

    // Meters on the right.
    auto meters = area.removeFromRight (kMeterWidth);
    area.removeFromRight (theme::gap);

    meters = meters.reduced (2, 0);
    const auto meterWidth = (meters.getWidth() - theme::gapSmall * 3) / 4;
    inputMeter.setBounds (meters.removeFromLeft (meterWidth));
    meters.removeFromLeft (theme::gapSmall);
    sidechainMeter.setBounds (meters.removeFromLeft (meterWidth));
    meters.removeFromLeft (theme::gapSmall);
    reductionMeter.setBounds (meters.removeFromLeft (meterWidth));
    meters.removeFromLeft (theme::gapSmall);
    outputMeter.setBounds (meters);

    // Display tabs and buttons.
    displayTabArea = area.removeFromTop (kDisplayTabHeight);

    auto buttons = displayTabArea;
    buttons.removeFromLeft (kNumDisplayPages * 88 + 8);

    freezeButton.setBounds (buttons.removeFromRight (62).reduced (0, 3));
    buttons.removeFromRight (theme::gapSmall);

    if (displayPage == DisplayPage::Spectrum)
    {
        deltaButton.setBounds (buttons.removeFromRight (56).reduced (0, 3));
    }
    else if (displayPage == DisplayPage::Curve)
    {
        redoButton.setBounds (buttons.removeFromRight (52).reduced (0, 3));
        buttons.removeFromRight (theme::gapSmall);
        undoButton.setBounds (buttons.removeFromRight (52).reduced (0, 3));
    }

    area.removeFromTop (2);

    visualizer.setBounds (area);
    curveEditor.setBounds (area);
    spectrumView.setBounds (area);

    if (bandMatrixView != nullptr)
        bandMatrixView->setBounds (area);
}

void MainView::layoutPanels (juce::Rectangle<int> area)
{
    panelContentArea = area;
    panelViewport.setBounds (area);

    if (panels.empty())
        return;

    auto contentWidth = area.getWidth();
    auto activeHeight = panels[(size_t) activePanel]->getPreferredHeight (contentWidth);

    // If the panel is taller than the strip, the scrollbar takes width away, so
    // the layout has to be measured again against what is actually left.
    if (activeHeight > area.getHeight())
    {
        contentWidth -= panelViewport.getScrollBarThickness();
        activeHeight = panels[(size_t) activePanel]->getPreferredHeight (contentWidth);
    }

    panelHolder.setSize (contentWidth, juce::jmax (activeHeight, area.getHeight()));

    for (auto& panel : panels)
        panel->setBounds (0, 0, contentWidth,
                          juce::jmax (panel->getPreferredHeight (contentWidth), area.getHeight()));
}

// ===========================================================================
//  Painting and hit testing
// ===========================================================================

void MainView::mouseDown (const juce::MouseEvent& e)
{
    const auto position = e.getPosition();

    for (int i = 0; i < kNumDisplayPages; ++i)
    {
        if (displayTabBounds (i).contains (position))
        {
            setDisplayPage ((DisplayPage) i);
            return;
        }
    }

    for (int i = 0; i < (int) panels.size(); ++i)
    {
        if (panelTabBounds (i).contains (position))
        {
            setActivePanel (i);
            return;
        }
    }
}

void MainView::mouseMove (const juce::MouseEvent& e)
{
    const auto position = e.getPosition();

    auto newDisplayHover = -1;
    auto newPanelHover = -1;

    for (int i = 0; i < kNumDisplayPages; ++i)
        if (displayTabBounds (i).contains (position))
            newDisplayHover = i;

    for (int i = 0; i < (int) panels.size(); ++i)
        if (panelTabBounds (i).contains (position))
            newPanelHover = i;

    if (newDisplayHover != displayTabHover || newPanelHover != panelTabHover)
    {
        displayTabHover = newDisplayHover;
        panelTabHover = newPanelHover;
        repaint (displayTabArea);
        repaint (panelTabArea);
    }
}

void MainView::mouseExit (const juce::MouseEvent&)
{
    if (displayTabHover >= 0 || panelTabHover >= 0)
    {
        displayTabHover = -1;
        panelTabHover = -1;
        repaint (displayTabArea);
        repaint (panelTabArea);
    }
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // -- header ---------------------------------------------------------------
    {
        auto header = headerArea;
        g.setColour (theme::panel);
        g.fillRect (header);
        g.setColour (theme::outline);
        g.fillRect (header.removeFromBottom (1));

        auto title = headerArea.reduced (theme::gap, 0).removeFromLeft (124);

        g.setColour (theme::accent);
        g.setFont (theme::font (20.0f, true));
        g.drawText ("SideForge", title.removeFromTop (30).withTrimmedTop (8),
                    juce::Justification::centredLeft, false);

        g.setColour (theme::textFaint);
        g.setFont (theme::labelFont (9.0f));
        g.drawText ("SIDECHAIN PROCESSOR", title, juce::Justification::topLeft, false);
    }

    // -- display tabs ---------------------------------------------------------
    for (int i = 0; i < kNumDisplayPages; ++i)
    {
        auto bounds = displayTabBounds (i).toFloat();
        const auto selected = ((int) displayPage == i);

        if (selected)
        {
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (bounds, theme::cornerSmall);
            g.setColour (theme::accent);
            g.fillRect (bounds.removeFromBottom (2.0f).reduced (6.0f, 0.0f));
        }
        else if (i == displayTabHover)
        {
            g.setColour (theme::panel);
            g.fillRoundedRectangle (bounds, theme::cornerSmall);
        }

        g.setColour (selected ? theme::text : theme::textDim);
        g.setFont (theme::labelFont (11.0f));
        g.drawText (kDisplayPageNames[i], displayTabBounds (i), juce::Justification::centred, false);
    }

    // -- panel tabs -----------------------------------------------------------
    for (int i = 0; i < (int) panels.size(); ++i)
    {
        const auto bounds = panelTabBounds (i).toFloat();
        const auto selected = (activePanel == i);

        g.setColour (selected ? theme::panelRaised : (i == panelTabHover ? theme::panel : theme::panelSunken));
        g.fillRoundedRectangle (bounds, theme::cornerSmall);

        if (selected)
        {
            g.setColour (theme::accent);
            g.fillRect (bounds.withHeight (2.0f).reduced (8.0f, 0.0f));
        }

        g.setColour (selected ? theme::text : theme::textDim);
        g.setFont (theme::labelFont (11.0f));
        g.drawText (panels[(size_t) i]->getTitle(), panelTabBounds (i), juce::Justification::centred, false);
    }

    // -- panel background -----------------------------------------------------
    if (! panelContentArea.isEmpty())
        SideForgeLookAndFeel::drawPanel (g, panelContentArea.toFloat().expanded (2.0f, 0.0f),
                                         theme::panel, theme::outline);

    // -- footer ---------------------------------------------------------------
    {
        auto footer = footerArea;
        g.setColour (theme::panel);
        g.fillRect (footer);
        g.setColour (theme::outline);
        g.fillRect (footer.removeFromTop (1));

        const auto connected = processor.isSidechainConnected()
                            || readEnum<ScSource> (processor.getParams().scSource) == ScSource::Internal;
        const auto active = processor.isSidechainReceivingAudio();

        footer = footer.reduced (theme::gap, 0);

        const auto indicator = footer.removeFromLeft (12);
        g.setColour (! connected ? theme::danger : (active ? theme::ok : theme::warning));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (indicator.getCentre().toFloat()));

        g.setColour (theme::textDim);
        g.setFont (theme::valueFont (10.5f));

        // Split the strip so the two readouts can never overlap each other.
        auto right = footer.removeFromRight (juce::jmin (footer.getWidth() / 2, 260));
        g.drawText (statusRight, right, juce::Justification::centredRight, true);
        g.drawText (statusLeft, footer.withTrimmedLeft (4), juce::Justification::centredLeft, true);
    }
}
} // namespace sf
