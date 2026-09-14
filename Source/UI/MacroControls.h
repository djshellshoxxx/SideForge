#pragma once
#include "Controls.h"

namespace sf
{
/**
    The four macros, and the switch that takes them out of circuit.

    They are modulation over parameters the user can also reach directly in the
    panels below — never a parallel set of hidden values — which is what keeps
    A/B comparison and session recall exact. `MacroMapping.cpp` is where each one
    is actually applied.
*/
class MacroControls : public juce::Component
{
public:
    explicit MacroControls (const ControlContext&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Greys the four knobs when the macros are switched out. */
    void setMacrosActive (bool active);

    /** Refreshes the MIDI badges. Called from the editor's timer. */
    void refreshBadges();

private:
    std::unique_ptr<SFKnob> depth, separation, timing, character;
    std::unique_ptr<SFToggle> enabled;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroControls)
};
} // namespace sf
