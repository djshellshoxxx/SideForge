#include "MacroControls.h"
#include "LookAndFeel.h"
#include "../Parameters/ParamIDs.h"

namespace sf
{
MacroControls::MacroControls (const ControlContext& context)
{
    depth      = std::make_unique<SFKnob> (context, pid::macroDepth, "Depth",
                                           "Scales how much gain reduction every active engine asks for.");
    separation = std::make_unique<SFKnob> (context, pid::macroSeparation, "Separation",
                                           "Trades broadband ducking for spectral ducking, and makes the "
                                           "collision map more selective as it does.");
    timing     = std::make_unique<SFKnob> (context, pid::macroTiming, "Timing",
                                           "Scales every attack and release time together.");
    character  = std::make_unique<SFKnob> (context, pid::macroCharacter, "Character",
                                           "Soft and transparent at the left - wide knee, gentler ratio, "
                                           "slower onset - through to tight and aggressive at the right.");
    enabled    = std::make_unique<SFToggle> (context, pid::macrosEnabled, "Macros Active",
                                             "Turn this off to hear the panel parameters exactly as they are set.");

    for (auto* knob : { depth.get(), separation.get(), timing.get(), character.get() })
        addAndMakeVisible (*knob);

    addAndMakeVisible (*enabled);
}

void MacroControls::setMacrosActive (bool active)
{
    for (auto* knob : { depth.get(), separation.get(), timing.get(), character.get() })
        knob->setRelevant (active);
}

void MacroControls::refreshBadges()
{
    for (auto* knob : { depth.get(), separation.get(), timing.get(), character.get() })
        knob->refreshBadge();

    enabled->refreshBadge();
}

void MacroControls::paint (juce::Graphics& g)
{
    SideForgeLookAndFeel::drawSectionHeading (g, getLocalBounds().reduced (theme::gapSmall).removeFromTop (14),
                                              "Macros", theme::accent);
}

void MacroControls::resized()
{
    auto area = getLocalBounds().reduced (theme::gapSmall);
    area.removeFromTop (14);   // the heading drawn in paint()

    const auto toggleRow = area.removeFromBottom (24);
    enabled->setBounds (toggleRow.withSizeKeepingCentre (juce::jmin (150, toggleRow.getWidth()), 22));

    const auto knobHeight = area.getHeight() / 2;
    auto topRow = area.removeFromTop (knobHeight);
    auto bottomRow = area;

    const auto knobWidth = topRow.getWidth() / 2;
    depth->setBounds      (topRow.removeFromLeft (knobWidth).reduced (2));
    separation->setBounds (topRow.reduced (2));
    timing->setBounds     (bottomRow.removeFromLeft (knobWidth).reduced (2));
    character->setBounds  (bottomRow.reduced (2));
}
} // namespace sf
