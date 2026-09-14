#include "ParameterContextMenu.h"
#include "LookAndFeel.h"

namespace sf::ParameterMenu
{
namespace
{
struct Clipboard
{
    bool  valid = false;
    juce::String parameterID;
    float realValue = 0.0f;
    float normalised = 0.0f;
};

Clipboard& clipboard()
{
    static Clipboard c;
    return c;
}

/** Controllers worth offering by name; anything else goes through "Other CC". */
struct NamedController { int cc; const char* name; };

constexpr NamedController kCommonControllers[] =
{
    {  1, "Mod Wheel" },   {  2, "Breath" },      {  4, "Foot" },
    {  7, "Volume" },      { 10, "Pan" },         { 11, "Expression" },
    { 71, "Resonance" },   { 74, "Brightness" },  { 91, "Reverb Send" },
};

enum MenuId
{
    idNone = 0,
    idMidiLearn = 1,
    idMidiClear,
    idTypeValue,
    idResetDefault,
    idCopy,
    idPaste,
    idClearAllMidi,
    idHostBase   = 1000,
    idAssignBase = 2000,
};

void setFromReal (juce::RangedAudioParameter& parameter, float realValue)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (realValue));
    parameter.endChangeGesture();
}

void setFromNormalised (juce::RangedAudioParameter& parameter, float normalised)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    parameter.endChangeGesture();
}
} // namespace

bool hasClipboardValue() { return clipboard().valid; }

juce::String midiBadgeText (const juce::String& parameterID, const ControlContext& context)
{
    if (context.midiLearn == nullptr)
        return {};

    if (isLearning (parameterID, context))
        return "LEARN";

    const auto cc = context.midiLearn->getControllerFor (parameterID);

    return cc >= 0 ? ("CC" + juce::String (cc)) : juce::String();
}

bool isLearning (const juce::String& parameterID, const ControlContext& context)
{
    return context.midiLearn != nullptr
        && context.midiLearn->isLearning()
        && context.midiLearn->getLearningParameterID() == parameterID;
}

void show (juce::Component& owner,
           juce::RangedAudioParameter& parameter,
           const ControlContext& context,
           std::function<void()> onRequestTextEntry,
           std::function<void()> onChanged)
{
    const auto parameterID = parameter.paramID;
    auto* midiLearn = context.midiLearn;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&owner.getLookAndFeel());

    menu.addSectionHeader (parameter.getName (48) + "   "
                           + parameter.getCurrentValueAsText().trim());

    // ---- host-provided items -------------------------------------------------
    // In a VST3 host this is where "Add automation", "Assign MIDI CC" and the
    // host's own learn commands appear. It is simply absent elsewhere.
    std::unique_ptr<juce::HostProvidedContextMenu> hostMenu;

    if (context.editor != nullptr)
        if (auto* hostContext = context.editor->getHostContext())
            hostMenu = hostContext->getContextMenuForParameter (&parameter);

    if (hostMenu != nullptr)
    {
        auto hostItems = hostMenu->getEquivalentPopupMenu();

        if (hostItems.containsAnyActiveItems())
        {
            menu.addSeparator();
            menu.addSubMenu ("Host", hostItems);
        }
    }

    // ---- MIDI ----------------------------------------------------------------
    if (midiLearn != nullptr)
    {
        menu.addSeparator();

        const auto learning = isLearning (parameterID, context);
        const auto assigned = midiLearn->getControllerFor (parameterID);

        menu.addItem (idMidiLearn,
                      learning ? "Cancel MIDI learn"
                               : (assigned >= 0 ? "Re-learn MIDI CC" : "MIDI learn"),
                      true, learning);

        juce::PopupMenu assign;

        for (const auto& controller : kCommonControllers)
            assign.addItem (idAssignBase + controller.cc,
                            "CC " + juce::String (controller.cc) + "  " + controller.name,
                            true, assigned == controller.cc);

        juce::PopupMenu allControllers;

        for (int block = 0; block < 8; ++block)
        {
            juce::PopupMenu range;

            for (int cc = block * 16; cc < (block + 1) * 16; ++cc)
                range.addItem (idAssignBase + cc, "CC " + juce::String (cc), true, assigned == cc);

            allControllers.addSubMenu (juce::String (block * 16) + " - " + juce::String (block * 16 + 15),
                                       range);
        }

        assign.addSeparator();
        assign.addSubMenu ("All controllers", allControllers);
        menu.addSubMenu ("Assign MIDI CC", assign);

        if (assigned >= 0)
            menu.addItem (idMidiClear, "Remove CC " + juce::String (assigned));

        if (midiLearn->getNumMappings() > 0)
            menu.addItem (idClearAllMidi,
                          "Clear all MIDI mappings (" + juce::String (midiLearn->getNumMappings()) + ")");
    }

    // ---- value ---------------------------------------------------------------
    menu.addSeparator();

    if (onRequestTextEntry != nullptr)
        menu.addItem (idTypeValue, "Type value...");

    menu.addItem (idResetDefault, "Reset to default ("
                                      + parameter.getText (parameter.getDefaultValue(), 24).trim() + ")");
    menu.addSeparator();
    menu.addItem (idCopy, "Copy value");
    menu.addItem (idPaste, "Paste value", clipboard().valid);

    // ---------------------------------------------------------------------------
    auto options = juce::PopupMenu::Options()
                       .withTargetComponent (&owner)
                       .withMousePosition()
                       .withMinimumWidth (190);

    menu.showMenuAsync (options,
                        [&parameter, parameterID, midiLearn, onRequestTextEntry, onChanged] (int result)
    {
        if (result == idNone)
            return;

        switch (result)
        {
            case idMidiLearn:
                if (midiLearn != nullptr)
                {
                    if (midiLearn->isLearning() && midiLearn->getLearningParameterID() == parameterID)
                        midiLearn->cancelLearn();
                    else
                        midiLearn->startLearn (parameterID);
                }
                break;

            case idMidiClear:
                if (midiLearn != nullptr)
                    midiLearn->clearMapping (parameterID);
                break;

            case idClearAllMidi:
                if (midiLearn != nullptr)
                    midiLearn->clearAllMappings();
                break;

            case idTypeValue:
                if (onRequestTextEntry != nullptr)
                    onRequestTextEntry();
                break;

            case idResetDefault:
                setFromNormalised (parameter, parameter.getDefaultValue());
                break;

            case idCopy:
                clipboard() = { true, parameterID,
                                parameter.convertFrom0to1 (parameter.getValue()),
                                parameter.getValue() };
                break;

            case idPaste:
                if (clipboard().valid)
                {
                    // Pasting between two controls of the same kind keeps the real
                    // value; pasting across different kinds keeps the position,
                    // which is the only thing that can be meaningful.
                    if (clipboard().parameterID == parameterID)
                        setFromReal (parameter, clipboard().realValue);
                    else
                        setFromNormalised (parameter, clipboard().normalised);
                }
                break;

            default:
                if (result >= idAssignBase && result < idAssignBase + MidiLearnManager::kNumControllers)
                {
                    if (midiLearn != nullptr)
                        midiLearn->setMapping (parameterID, result - idAssignBase);
                }
                break;
        }

        if (onChanged != nullptr)
            onChanged();
    });
}
} // namespace sf::ParameterMenu
