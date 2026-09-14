#include "../TestHelpers.h"
#include "PluginEditor.h"
#include "UI/Controls.h"
#include "UI/CurveEditor.h"
#include "UI/MainView.h"
#include "UI/Panels.h"
#include <functional>
#include <set>

using namespace sft;

namespace
{
/** Every parameter ID that has a control somewhere in the editor's tree. */
void collectBoundParameters (juce::Component& component, std::set<juce::String>& found)
{
    if (auto* knob = dynamic_cast<sf::SFKnob*> (&component))
        found.insert (knob->getParameterID());
    else if (auto* choice = dynamic_cast<sf::SFChoice*> (&component))
        found.insert (choice->getParameterID());
    else if (auto* toggle = dynamic_cast<sf::SFToggle*> (&component))
        found.insert (toggle->getParameterID());
    else if (auto* segmented = dynamic_cast<sf::SFSegmentedControl*> (&component))
        found.insert (segmented->getParameterID());

    for (auto* child : component.getChildren())
        if (child != nullptr)
            collectBoundParameters (*child, found);
}

/** Renders the editor into an offscreen image, which exercises every paint path. */
void renderEditor (juce::AudioProcessorEditor& editor)
{
    juce::Image image { juce::Image::ARGB,
                        juce::jmax (1, editor.getWidth()),
                        juce::jmax (1, editor.getHeight()), true };
    juce::Graphics g { image };
    editor.paintEntireComponent (g, true);
}

std::unique_ptr<juce::AudioProcessorEditor> openEditor (sf::SideForgeProcessor& p)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditor() };
    SF_CHECK_MSG (editor != nullptr, "the processor must create an editor");
    return editor;
}
} // namespace

// ---------------------------------------------------------------------------
//  Coverage: nothing in the plugin is unreachable, nothing on screen is dead
// ---------------------------------------------------------------------------

SF_TEST (editor_everyParameterHasAControl)
{
    auto p = makeProcessor();
    auto editor = openEditor (*p);

    std::set<juce::String> bound;
    collectBoundParameters (*editor, bound);

    juce::StringArray missing;

    for (auto* parameter : p->getAPVTS().processor.getParameters())
    {
        auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);

        if (withID == nullptr)
            continue;

        if (bound.find (withID->paramID) == bound.end())
            missing.add (withID->paramID);
    }

    SF_CHECK_MSG (missing.isEmpty(),
                  ("parameters with no control in the editor: "
                   + missing.joinIntoString (", ").toStdString()).c_str());
}

SF_TEST (editor_everyControlPointsAtARealParameter)
{
    auto p = makeProcessor();
    auto editor = openEditor (*p);

    std::set<juce::String> bound;
    collectBoundParameters (*editor, bound);

    SF_CHECK_MSG (! bound.empty(), "the editor should contain parameter controls");

    for (const auto& id : bound)
        SF_CHECK_MSG (p->getAPVTS().getParameter (id) != nullptr,
                      ("control bound to a parameter that does not exist: " + id.toStdString()).c_str());
}

// ---------------------------------------------------------------------------
//  Layout and painting
// ---------------------------------------------------------------------------

SF_TEST (editor_paintsAtEverySizeInItsRange)
{
    auto p = makeProcessor();
    auto editor = openEditor (*p);

    struct Size { int width; int height; };

    for (const auto& size : { Size { sf::SideForgeEditor::kMinWidth, sf::SideForgeEditor::kMinHeight },
                              Size { 1180, 760 },
                              Size { 1440, 900 },
                              Size { 1920, 1080 },
                              Size { 2560, 1400 } })
    {
        editor->setSize (size.width, size.height);
        renderEditor (*editor);

        SF_CHECK (editor->getWidth() == size.width);
        SF_CHECK (editor->getHeight() == size.height);
    }
}

SF_TEST (editor_refusesToGoSmallerThanItsMinimum)
{
    auto p = makeProcessor();
    auto editor = openEditor (*p);

    auto* constrainer = editor->getConstrainer();
    SF_CHECK_MSG (constrainer != nullptr, "a resizable editor needs a constrainer");
    SF_CHECK (constrainer->getMinimumWidth() == sf::SideForgeEditor::kMinWidth);
    SF_CHECK (constrainer->getMinimumHeight() == sf::SideForgeEditor::kMinHeight);
}

SF_TEST (editor_paintsInEveryModeAndDisplayPage)
{
    auto p = makeProcessor();

    for (int mode = 0; mode < 4; ++mode)
    {
        auto* modeParam = p->getAPVTS().getParameter (sf::pid::mode);
        modeParam->setValueNotifyingHost (modeParam->convertTo0to1 ((float) mode));

        auto editor = openEditor (*p);
        editor->setSize (1180, 760);

        // Walk every panel and every display page so no paint path is skipped.
        for (int panel = 0; panel < 7; ++panel)
        {
            auto* mainView = dynamic_cast<sf::MainView*> (editor->getChildComponent (0));
            SF_CHECK_MSG (mainView != nullptr, "the editor should host a MainView");

            mainView->setDisplayPage ((sf::MainView::DisplayPage) (panel % 3));
            renderEditor (*editor);
        }
    }
}

SF_TEST (editor_windowSizeIsRememberedInTheState)
{
    auto p = makeProcessor();

    {
        auto editor = openEditor (*p);
        editor->setSize (1366, 820);
    }

    SF_CHECK (p->getEditorState().width == 1366);
    SF_CHECK (p->getEditorState().height == 820);

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto restored = makeProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());

    SF_CHECK (restored->getEditorState().width == 1366);
    SF_CHECK (restored->getEditorState().height == 820);

    auto editor = openEditor (*restored);
    SF_CHECK (editor->getWidth() == 1366);
    SF_CHECK (editor->getHeight() == 820);
}

SF_TEST (editor_openingAndClosingRepeatedlyIsClean)
{
    auto p = makeProcessor();

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 512 };
    juce::MidiBuffer midi;

    for (int i = 0; i < 6; ++i)
    {
        auto editor = openEditor (*p);
        editor->setSize (1180, 760);
        renderEditor (*editor);

        for (int b = 0; b < 4; ++b)
        {
            buffer.clear();
            for (int ch = 0; ch < 2; ++ch)
                fillSine (buffer, ch, 440.0, 48000.0, 0.4f);

            p->processBlock (buffer, midi);
            SF_CHECK (allFinite (buffer));
        }
    }
}

// ---------------------------------------------------------------------------
//  MIDI learn
// ---------------------------------------------------------------------------

SF_TEST (midiLearn_learnsAControllerAndThenFollowsIt)
{
    auto p = makeProcessor();
    auto& learn = p->getMidiLearn();

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 256 };
    juce::MidiBuffer midi;

    auto sendController = [&] (int cc, int value)
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 0);
        buffer.clear();
        p->processBlock (buffer, midi);
        learn.applyPendingChanges();
    };

    SF_CHECK (learn.getControllerFor (sf::pid::compThreshold) == -1);

    learn.startLearn (sf::pid::compThreshold);
    SF_CHECK (learn.isLearning());

    const auto before = getParam (*p, sf::pid::compThreshold);

    sendController (74, 100);

    SF_CHECK_MSG (! learn.isLearning(), "the learn should complete on the first controller move");
    SF_CHECK_MSG (learn.getControllerFor (sf::pid::compThreshold) == 74,
                  "the moved controller should have been bound");

    // The movement that taught the binding must not also jump the parameter.
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), before, 0.001);

    // Subsequent movements do drive it.
    sendController (74, 127);
    const auto atMax = getParam (*p, sf::pid::compThreshold);

    sendController (74, 0);
    const auto atMin = getParam (*p, sf::pid::compThreshold);

    SF_CHECK_MSG (atMax > atMin, "the bound controller should move the parameter");
    SF_CHECK_NEAR (atMax, 0.0, 0.01);      // compThreshold's range is -60 .. 0 dB
    SF_CHECK_NEAR (atMin, -60.0, 0.01);

    // An unbound controller changes nothing.
    const auto held = getParam (*p, sf::pid::compThreshold);
    sendController (7, 127);
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), held, 0.001);
}

SF_TEST (midiLearn_rebindingMovesTheAssignment)
{
    auto p = makeProcessor();
    auto& learn = p->getMidiLearn();

    learn.setMapping (sf::pid::compRatio, 20);
    SF_CHECK (learn.getControllerFor (sf::pid::compRatio) == 20);
    SF_CHECK (learn.getParameterForController (20) == juce::String (sf::pid::compRatio));

    // One parameter answers to one controller.
    learn.setMapping (sf::pid::compRatio, 21);
    SF_CHECK (learn.getControllerFor (sf::pid::compRatio) == 21);
    SF_CHECK (learn.getParameterForController (20).isEmpty());
    SF_CHECK (learn.getNumMappings() == 1);

    learn.clearMapping (sf::pid::compRatio);
    SF_CHECK (learn.getControllerFor (sf::pid::compRatio) == -1);
    SF_CHECK (learn.getNumMappings() == 0);
}

SF_TEST (midiLearn_mappingsSurviveAStateRoundTrip)
{
    auto p = makeProcessor();
    p->getMidiLearn().setMapping (sf::pid::macroDepth, 1);
    p->getMidiLearn().setMapping (sf::pid::mix, 11);

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto restored = makeProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());

    SF_CHECK (restored->getMidiLearn().getControllerFor (sf::pid::macroDepth) == 1);
    SF_CHECK (restored->getMidiLearn().getControllerFor (sf::pid::mix) == 11);
    SF_CHECK (restored->getMidiLearn().getNumMappings() == 2);
}

SF_TEST (midiLearn_ignoresMappingsToParametersThatNoLongerExist)
{
    auto p = makeProcessor();

    juce::ValueTree tree { sf::MidiLearnManager::stateTag };

    juce::ValueTree good { "Map" };
    good.setProperty ("cc", 5, nullptr);
    good.setProperty ("param", sf::pid::mix, nullptr);
    tree.appendChild (good, nullptr);

    juce::ValueTree stale { "Map" };
    stale.setProperty ("cc", 6, nullptr);
    stale.setProperty ("param", "aParameterFromSomeOtherPlugin", nullptr);
    tree.appendChild (stale, nullptr);

    p->getMidiLearn().fromValueTree (tree);

    SF_CHECK (p->getMidiLearn().getControllerFor (sf::pid::mix) == 5);
    SF_CHECK_MSG (p->getMidiLearn().getNumMappings() == 1,
                  "a mapping to a parameter that no longer exists must be dropped");
}

SF_TEST (midiLearn_cancellingLeavesNothingBound)
{
    auto p = makeProcessor();
    auto& learn = p->getMidiLearn();

    learn.startLearn (sf::pid::outputGain);
    SF_CHECK (learn.isLearning());

    learn.cancelLearn();
    SF_CHECK (! learn.isLearning());

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 256 };
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 50, 64), 0);
    buffer.clear();
    p->processBlock (buffer, midi);
    learn.applyPendingChanges();

    SF_CHECK (learn.getControllerFor (sf::pid::outputGain) == -1);
}

SF_TEST (midiLearn_doesNotDisturbTheAudio)
{
    auto p = makeProcessor();
    p->getMidiLearn().setMapping (sf::pid::macroDepth, 1);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), 256 };
    juce::MidiBuffer midi;

    for (int b = 0; b < 200; ++b)
    {
        midi.clear();

        // A controller sweeping continuously, as a fader would.
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, b % 128), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, (b * 7) % 128), 128);

        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            fillSine (buffer, ch, 440.0, 48000.0, 0.4f);

        p->processBlock (buffer, midi);
        p->getMidiLearn().applyPendingChanges();

        SF_CHECK (allFinite (buffer));
    }
}

// ---------------------------------------------------------------------------
//  Curve editor
// ---------------------------------------------------------------------------

SF_TEST (curveEditor_undoAndRedoWalkTheEditHistory)
{
    sf::CurveModel model;
    model.setPresetShape (0);   // Flat

    sf::CurveEditor editor;
    editor.setBounds (0, 0, 400, 200);

    int changeCount = 0;
    editor.onCurveChanged = [&changeCount] { ++changeCount; };
    editor.setModel (&model);

    SF_CHECK_MSG (! editor.canUndo(), "a freshly attached model has nothing to undo");

    const auto originalNodes = model.getNumNodes();

    editor.applyShape (1);   // Classic Pump
    SF_CHECK (editor.canUndo());
    SF_CHECK (changeCount == 1);

    const auto pumpNodes = model.getNumNodes();
    SF_CHECK (pumpNodes != originalNodes);

    editor.applyShape (6);   // 1/8 Gate
    const auto gateNodes = model.getNumNodes();

    editor.undo();
    SF_CHECK_MSG (model.getNumNodes() == pumpNodes, "undo should restore the previous shape");
    SF_CHECK (editor.canRedo());

    editor.undo();
    SF_CHECK_MSG (model.getNumNodes() == originalNodes, "undo should walk all the way back");
    SF_CHECK (! editor.canUndo());

    editor.redo();
    SF_CHECK (model.getNumNodes() == pumpNodes);

    editor.redo();
    SF_CHECK (model.getNumNodes() == gateNodes);
    SF_CHECK (! editor.canRedo());

    editor.setModel (nullptr);
}

SF_TEST (curveEditor_paintsEveryShapeWithoutTrouble)
{
    sf::CurveModel model;
    sf::CurveEditor editor;
    editor.setBounds (0, 0, 640, 280);
    editor.setModel (&model);

    juce::Image image { juce::Image::ARGB, 640, 280, true };

    for (int shape = 0; shape < sf::CurveModel::getShapeNames().size(); ++shape)
    {
        editor.applyShape (shape);

        for (const bool snap : { true, false })
        {
            editor.setGrid (snap ? 16 : 7, snap);
            editor.setPlayheadPhase (snap ? 0.25f : -1.0f);

            juce::Graphics g { image };
            editor.paintEntireComponent (g, true);
        }
    }

    editor.setModel (nullptr);
}

// ---------------------------------------------------------------------------
//  Layout integrity
// ---------------------------------------------------------------------------

SF_TEST (editor_noControlIsClippedOutOfItsPanel)
{
    auto p = makeProcessor();
    auto editor = openEditor (*p);

    // Narrow enough that the busiest panels have to wrap onto extra rows.
    for (const int width : { sf::SideForgeEditor::kMinWidth, 1100, 1180, 1600 })
    {
        editor->setSize (width, sf::SideForgeEditor::kMinHeight);

        std::vector<sf::PanelBase*> found;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (auto* panel = dynamic_cast<sf::PanelBase*> (&c))
                found.push_back (panel);

            for (auto* child : c.getChildren())
                if (child != nullptr)
                    walk (*child);
        };
        walk (*editor);

        SF_CHECK_MSG (found.size() == 7, "the editor should contain seven panels");

        for (auto* panel : found)
        {
            for (auto* child : panel->getChildren())
            {
                if (child == nullptr)
                    continue;

                const auto bounds = child->getBounds();

                SF_CHECK_MSG (bounds.getBottom() <= panel->getHeight(),
                              ("a control in the " + panel->getTitle().toStdString()
                               + " panel is below its bottom edge at width "
                               + std::to_string (width)).c_str());
                SF_CHECK_MSG (bounds.getRight() <= panel->getWidth(),
                              ("a control in the " + panel->getTitle().toStdString()
                               + " panel is past its right edge at width "
                               + std::to_string (width)).c_str());
                SF_CHECK_MSG (bounds.getX() >= 0 && bounds.getY() >= 0,
                              "a control is positioned off the top or left of its panel");
            }
        }
    }
}

SF_TEST (editor_everyPanelReportsAHeightThatFitsItsControls)
{
    auto p = makeProcessor();
    sf::ControlContext context;
    context.apvts = &p->getAPVTS();
    context.midiLearn = &p->getMidiLearn();

    std::vector<std::unique_ptr<sf::PanelBase>> panels;
    panels.push_back (std::make_unique<sf::CompressorPanel> (context));
    panels.push_back (std::make_unique<sf::TriggerPanel> (context, [] {}));
    panels.push_back (std::make_unique<sf::CurvePanel> (context));
    panels.push_back (std::make_unique<sf::SpectrumPanel> (context));
    panels.push_back (std::make_unique<sf::BandsPanel> (context));
    panels.push_back (std::make_unique<sf::RoutingPanel> (context));
    panels.push_back (std::make_unique<sf::OutputPanel> (context));

    for (auto& panel : panels)
    {
        for (const int width : { 400, 700, 960, 1200, 2000 })
        {
            const auto height = panel->getPreferredHeight (width);
            SF_CHECK_MSG (height > 0, "a panel must report a positive height");

            panel->setBounds (0, 0, width, height);

            for (auto* child : panel->getChildren())
                if (child != nullptr)
                    SF_CHECK_MSG (child->getBounds().getBottom() <= height,
                                  ("the " + panel->getTitle().toStdString()
                                   + " panel reported a height that clips a control at width "
                                   + std::to_string (width)).c_str());
        }
    }
}

SF_TEST (editor_everyChoiceControlIsPopulatedAndShowsItsValue)
{
    auto p = makeProcessor();
    auto editor = openEditor (*p);
    editor->setSize (1180, 760);

    std::vector<sf::SFChoice*> choices;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        if (auto* choice = dynamic_cast<sf::SFChoice*> (&c))
            choices.push_back (choice);

        for (auto* child : c.getChildren())
            if (child != nullptr)
                walk (*child);
    };
    walk (*editor);

    SF_CHECK_MSG (! choices.empty(), "the editor should contain choice controls");

    for (auto* control : choices)
    {
        const auto id = control->getParameterID();
        auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (p->getAPVTS().getParameter (id));

        SF_CHECK_MSG (parameter != nullptr,
                      ("a choice control is bound to something that is not a choice parameter: "
                       + id.toStdString()).c_str());

        if (parameter == nullptr)
            continue;

        // An empty combo box renders blank and cannot be used: the attachment
        // matches on item index and does not add the items itself.
        SF_CHECK_MSG (control->getComboBox().getNumItems() == parameter->choices.size(),
                      ("the " + id.toStdString() + " combo box has "
                       + std::to_string (control->getComboBox().getNumItems())
                       + " items but the parameter has "
                       + std::to_string (parameter->choices.size())).c_str());

        SF_CHECK_MSG (control->getComboBox().getText().isNotEmpty(),
                      ("the " + id.toStdString() + " combo box shows nothing").c_str());

        // ...and it follows the parameter.
        for (int index = 0; index < parameter->choices.size(); ++index)
        {
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));

            SF_CHECK_MSG (control->getComboBox().getText() == parameter->choices[index],
                          ("the " + id.toStdString() + " combo box did not follow the parameter to "
                           + parameter->choices[index].toStdString()).c_str());
        }

        // Back to the default, via the base-class accessor: AudioParameterChoice
        // hides getDefaultValue behind its own typed interface.
        auto* asBase = static_cast<juce::AudioProcessorParameter*> (parameter);
        asBase->setValueNotifyingHost (asBase->getDefaultValue());
    }
}
