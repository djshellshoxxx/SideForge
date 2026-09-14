#pragma once
#include "../Parameters/MidiLearn.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace sf
{
/** The shared services every parameter control needs. One instance lives on the
    editor and is handed down by pointer. */
struct ControlContext
{
    juce::AudioProcessorValueTreeState* apvts = nullptr;
    MidiLearnManager* midiLearn = nullptr;
    juce::AudioProcessorEditor* editor = nullptr;

    juce::RangedAudioParameter* getParameter (const juce::String& id) const
    {
        return apvts != nullptr ? apvts->getParameter (id) : nullptr;
    }
};

/**
    The right-click menu every control in the plugin shares.

    It offers three groups:

      - whatever the **host** provides for this parameter (VST3 hosts put their
        automation, MIDI CC assignment and "show in automation lane" commands
        here) — retrieved through `AudioProcessorEditor::getHostContext()`,
      - the plugin's own **MIDI learn**, for hosts that do not offer CC binding,
      - the usual per-control commands: type a value, reset to default, copy and
        paste.

    Everything is asynchronous; no modal loops are used, because the plugin is
    built with `JUCE_MODAL_LOOPS_PERMITTED=0`.
*/
namespace ParameterMenu
{
/** Shows the menu next to the mouse. `onRequestTextEntry` is called if the user
    picks "Type value", so the owning control can open its own inline editor. */
void show (juce::Component& owner,
           juce::RangedAudioParameter& parameter,
           const ControlContext& context,
           std::function<void()> onRequestTextEntry = {},
           std::function<void()> onChanged = {});

/** True when something has been copied and can be pasted into `parameter`. */
bool hasClipboardValue();

/** The text a control should show as its MIDI badge, or an empty string. */
juce::String midiBadgeText (const juce::String& parameterID, const ControlContext& context);

/** True while this specific parameter is waiting to learn a controller. */
bool isLearning (const juce::String& parameterID, const ControlContext& context);
} // namespace ParameterMenu
} // namespace sf
