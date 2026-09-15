#include "Panels.h"
#include "LookAndFeel.h"
#include "../Parameters/ParamIDs.h"

namespace sf
{
// ===========================================================================
//  PanelBase
// ===========================================================================

PanelBase::PanelBase (const ControlContext& contextToUse, juce::String titleToUse)
    : context (contextToUse), title (std::move (titleToUse))
{
    groups.push_back ({ {}, {} });
}

PanelBase::~PanelBase() = default;

void PanelBase::addGroup (const juce::String& name)
{
    groups.push_back ({ name, {} });
}

std::vector<juce::Component*> PanelBase::componentsInLastGroup() const
{
    std::vector<juce::Component*> found;
    const auto group = (int) groups.size() - 1;

    for (const auto& item : items)
        if (item.group == group)
            found.push_back (item.component);

    return found;
}

SFKnob& PanelBase::addKnob (const juce::String& parameterID, const juce::String& name,
                            const juce::String& tooltip)
{
    auto knob = std::make_unique<SFKnob> (context, parameterID, name, tooltip);
    auto* raw = knob.get();

    addAndMakeVisible (*raw);
    items.push_back ({ raw, kKnobWidth, kKnobHeight, (int) groups.size() - 1 });
    owned.push_back (std::move (knob));

    return *raw;
}

SFChoice& PanelBase::addChoice (const juce::String& parameterID, const juce::String& name,
                                const juce::String& tooltip, int width)
{
    auto choice = std::make_unique<SFChoice> (context, parameterID, name, tooltip);
    auto* raw = choice.get();

    addAndMakeVisible (*raw);
    items.push_back ({ raw, width, 44, (int) groups.size() - 1 });
    owned.push_back (std::move (choice));

    return *raw;
}

SFToggle& PanelBase::addToggle (const juce::String& parameterID, const juce::String& name,
                                const juce::String& tooltip, juce::Colour onColour)
{
    auto toggle = std::make_unique<SFToggle> (context, parameterID, name, tooltip, onColour);
    auto* raw = toggle.get();

    addAndMakeVisible (*raw);
    items.push_back ({ raw, 104, 26, (int) groups.size() - 1 });
    owned.push_back (std::move (toggle));

    return *raw;
}

void PanelBase::addCustom (std::unique_ptr<juce::Component> component, int width, int height)
{
    auto* raw = component.get();

    addAndMakeVisible (*raw);
    items.push_back ({ raw, width, height, (int) groups.size() - 1 });
    owned.push_back (std::move (component));
}

void PanelBase::setRelevance (juce::Component& control, bool isRelevant)
{
    if (auto* knob = dynamic_cast<SFKnob*> (&control))         knob->setRelevant (isRelevant);
    else if (auto* choice = dynamic_cast<SFChoice*> (&control)) choice->setRelevant (isRelevant);
    else if (auto* toggle = dynamic_cast<SFToggle*> (&control)) toggle->setRelevant (isRelevant);
    else                                                        control.setEnabled (isRelevant);
}

void PanelBase::refreshBadges()
{
    for (const auto& item : items)
    {
        if (auto* knob = dynamic_cast<SFKnob*> (item.component))          knob->refreshBadge();
        else if (auto* choice = dynamic_cast<SFChoice*> (item.component)) choice->refreshBadge();
        else if (auto* toggle = dynamic_cast<SFToggle*> (item.component)) toggle->refreshBadge();
    }
}

int PanelBase::layout (int width, bool apply)
{
    const auto usable = juce::jmax (120, width - theme::gap * 2);

    auto x = theme::gap;
    auto y = theme::gap;
    auto rowHeight = 0;
    auto currentGroup = -1;

    // Only a real layout pass clears these; a measuring pass must leave the
    // headings where the last real pass put them, because setBounds with
    // unchanged bounds will not trigger another one.
    if (apply)
        for (auto& group : groups)
            group.labelBounds = {};

    // Total width of the group starting at `index`, so a group can be kept whole.
    auto groupWidthFrom = [this] (size_t index)
    {
        auto total = 0;

        for (auto scan = index; scan < items.size() && items[scan].group == items[index].group; ++scan)
            if (items[scan].component != nullptr && items[scan].component->isVisible())
                total += items[scan].width + theme::gapSmall;

        return total;
    };

    for (size_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];

        // Hidden controls take no space. The Band Matrix panel holds a full set
        // of controls for all four bands and shows one at a time, so skipping
        // them here is what lets the visible band flow as if it were alone.
        if (item.component == nullptr || ! item.component->isVisible())
            continue;

        const auto startsGroup = (item.group != currentGroup);
        const auto needsLabel = startsGroup && groups[(size_t) item.group].name.isNotEmpty();
        const auto extra = startsGroup && currentGroup >= 0 ? kGroupGap : 0;

        // A group that would be split across the row break but fits on a row of
        // its own is moved down whole: a heading with two of its four knobs under
        // it and the rest on the next line reads as two different groups.
        auto advance = item.width;

        if (startsGroup)
        {
            const auto whole = groupWidthFrom (index);

            if (whole <= usable)
                advance = whole;
        }

        // Wrap when the item (plus the gap before it) would overflow the row.
        if (x + extra + advance > usable + theme::gap && x > theme::gap)
        {
            x = theme::gap;
            y += rowHeight + kRowGap;
            rowHeight = 0;
        }
        else
        {
            x += extra;
        }

        if (startsGroup)
        {
            currentGroup = item.group;

            if (needsLabel && apply)
            {
                // The heading's rule runs only as far as the group's own controls,
                // so two groups sharing a row do not draw over each other.
                groups[(size_t) item.group].labelBounds =
                    { x, y, juce::jlimit (60, juce::jmax (60, usable - x), groupWidthFrom (index)), 11 };
            }
        }

        const auto labelOffset = groups[(size_t) item.group].name.isNotEmpty() ? 14 : 0;

        if (apply)
            item.component->setBounds (x, y + labelOffset, item.width, item.height);

        x += item.width + theme::gapSmall;
        rowHeight = juce::jmax (rowHeight, item.height + labelOffset);
    }

    return y + rowHeight + theme::gap;
}

int PanelBase::getPreferredHeight (int width) const
{
    return const_cast<PanelBase*> (this)->layout (width, false);
}

void PanelBase::resized()
{
    layout (getWidth(), true);
}

void PanelBase::paint (juce::Graphics& g)
{
    for (const auto& group : groups)
    {
        if (group.name.isEmpty() || group.labelBounds.isEmpty())
            continue;

        SideForgeLookAndFeel::drawSectionHeading (g, group.labelBounds, group.name);
    }
}

// ===========================================================================
//  Compressor
// ===========================================================================

CompressorPanel::CompressorPanel (const ControlContext& contextToUse)
    : PanelBase (contextToUse, "Compressor")
{
    addGroup ("Detection");
    controls.push_back (&addChoice (pid::compDetector, "Detector",
                                    "Peak follows transients, RMS follows loudness, Hybrid splits the difference.", 104));
    controls.push_back (&addKnob (pid::compThreshold, "Threshold",
                                  "Sidechain level above which gain reduction starts."));
    controls.push_back (&addKnob (pid::compRatio, "Ratio",
                                  "How much of the level above the threshold is removed. 40:1 reads as inf:1 and limits."));
    controls.push_back (&addKnob (pid::compKnee, "Knee",
                                  "Width of the soft transition around the threshold. 0 dB is a hard corner."));
    controls.push_back (&addKnob (pid::compRange, "Range",
                                  "Maximum gain reduction, however loud the sidechain gets."));

    addGroup ("Timing");
    controls.push_back (&addKnob (pid::compAttack, "Attack", "How fast the gain reduction arrives."));
    controls.push_back (&addKnob (pid::compHold, "Hold", "How long the deepest reduction is held before release begins."));
    controls.push_back (&addKnob (pid::compRelease, "Release", "How fast the gain comes back."));
    controls.push_back (&addToggle (pid::compAutoRelease, "Auto Release",
                                    "Recover faster after short, deep events and slower under sustained ones."));
}

void CompressorPanel::updateRelevance (const EngineSettings& settings)
{
    const auto relevant = settings.useCompressor;

    for (auto* control : controls)
        setRelevance (*control, relevant);
}

// ===========================================================================
//  Trigger
// ===========================================================================

namespace
{
/** A plain push button the panel owns, used for the manual test trigger. */
class ActionButton : public juce::TextButton
{
public:
    ActionButton (const juce::String& text, const juce::String& tooltip, std::function<void()> action)
    {
        setButtonText (text);
        setTooltip (tooltip);
        onClick = std::move (action);
    }
};
} // namespace

TriggerPanel::TriggerPanel (const ControlContext& contextToUse, std::function<void()> onTestTrigger)
    : PanelBase (contextToUse, "Trigger")
{
    addGroup ("Source");
    addChoice (pid::trigSource, "Source",
               "Where triggers come from. MIDI is selected automatically when the curve's trigger mode is MIDI.", 110);
    addChoice (pid::trigDetector, "Detection",
               "Transient reacts to onsets, Weighted follows perceived loudness.", 104);

    addGroup ("Threshold");
    audioControls.push_back (&addKnob (pid::trigThreshold, "Threshold", "Level a trigger has to cross."));
    audioControls.push_back (&addKnob (pid::trigHysteresis, "Hysteresis",
                                       "How far the level must fall again before another trigger can arm."));
    audioControls.push_back (&addKnob (pid::trigMinInterval, "Min Gap",
                                       "Shortest time allowed between two triggers."));

    addGroup ("Trigger Filter");
    audioControls.push_back (&addKnob (pid::trigHighPass, "High Pass",
                                       "Removes low content from the trigger detector only."));
    audioControls.push_back (&addKnob (pid::trigLowPass, "Low Pass",
                                       "Removes high content from the trigger detector only."));
    audioControls.push_back (&addToggle (pid::trigAudition, "Audition",
                                         "Monitor the filtered trigger signal so you can hear what the detector hears.",
                                         theme::warning));

    addGroup ("MIDI");
    midiControls.push_back (&addKnob (pid::trigMidiNote, "Note",
                                      "MIDI note that fires a trigger. 128 means any note."));

    addGroup ("Manual");
    addCustom (std::make_unique<ActionButton> ("Test Trigger",
                                               "Fires one trigger immediately, whatever the source is set to.",
                                               std::move (onTestTrigger)),
               104, 26);
}

void TriggerPanel::updateRelevance (const EngineSettings& settings)
{
    // Triggers exist to start the curve, so they only matter when the curve is
    // in one of its triggered modes.
    const auto triggersMatter = settings.useCurve && settings.curve.trigMode != TrigMode::Loop;
    const auto audioSource = settings.trigger.source == TrigSource::Sidechain
                          || settings.trigger.source == TrigSource::Internal;

    for (auto* control : audioControls)
        setRelevance (*control, triggersMatter && audioSource);

    for (auto* control : midiControls)
        setRelevance (*control, triggersMatter && settings.trigger.source == TrigSource::Midi);
}

// ===========================================================================
//  Curve
// ===========================================================================

CurvePanel::CurvePanel (const ControlContext& contextToUse)
    : PanelBase (contextToUse, "Curve")
{
    addGroup ("Rate");
    controls.push_back (&addChoice (pid::curveSyncMode, "Sync",
                                    "Tempo locks the cycle to the host; Free runs at a fixed rate.", 92));
    divisionControl = &addChoice (pid::curveDivision, "Division",
                                  "Cycle length in note values, including dotted and triplet.", 96);
    controls.push_back (divisionControl);
    rateControl = &addKnob (pid::curveRateHz, "Free Rate", "Cycle rate when Sync is set to Free.");
    controls.push_back (rateControl);
    controls.push_back (&addChoice (pid::curveTrigMode, "Trigger",
                                    "Loop runs continuously; One Shot, Audio and MIDI start the curve from a trigger.", 96));

    addGroup ("Shape");
    controls.push_back (&addKnob (pid::curveDepth, "Depth", "How much of the drawn curve is applied."));
    controls.push_back (&addKnob (pid::curvePhase, "Phase", "Rotates the curve within its cycle."));
    controls.push_back (&addKnob (pid::curveSwing, "Swing",
                                  "Lengthens the first cycle of each pair and shortens the second."));
    controls.push_back (&addKnob (pid::curveSmooth, "Smooth",
                                  "Anti-click filter on the curve output. Small values keep steps sharp."));

    addGroup ("Envelope");
    controls.push_back (&addKnob (pid::curveAttack, "Attack",
                                  "Limits how fast the gain may fall. 0 ms plays the drawn curve exactly."));
    controls.push_back (&addKnob (pid::curveHold, "Hold",
                                  "Holds the deepest point before the gain is allowed to recover."));
    controls.push_back (&addKnob (pid::curveRecover, "Recover",
                                  "Limits how fast the gain may rise. 0 ms plays the drawn curve exactly."));

    addGroup ("Delays");
    triggeredOnlyControls.push_back (&addKnob (pid::curvePreDelay, "Pre Delay",
                                               "Waits at unity gain after a trigger before the curve starts."));
    triggeredOnlyControls.push_back (&addKnob (pid::curvePostDelay, "Post Delay",
                                               "Holds the curve's final value before releasing back to unity."));

    addGroup ("Grid");
    controls.push_back (&addKnob (pid::curveSnap, "Snap", "Grid divisions across one cycle."));
    controls.push_back (&addToggle (pid::curveSnapEnable, "Snap To Grid",
                                    "Hold Shift while dragging a node to bypass the grid."));
}

void CurvePanel::updateRelevance (const EngineSettings& settings)
{
    const auto relevant = settings.useCurve;
    const auto tempo = settings.curve.syncMode == SyncMode::Tempo;
    const auto triggered = settings.curve.trigMode != TrigMode::Loop;

    for (auto* control : controls)
        setRelevance (*control, relevant);

    // Division and Free Rate are two ways of saying the same thing; only one of
    // them is doing anything at a time.
    if (divisionControl != nullptr) setRelevance (*divisionControl, relevant && tempo);
    if (rateControl != nullptr)     setRelevance (*rateControl, relevant && ! tempo);

    for (auto* control : triggeredOnlyControls)
        setRelevance (*control, relevant && triggered);
}

// ===========================================================================
//  Spectrum
// ===========================================================================

SpectrumPanel::SpectrumPanel (const ControlContext& contextToUse)
    : PanelBase (contextToUse, "Spectrum")
{
    addGroup ("Amount");
    controls.push_back (&addKnob (pid::specDepth, "Depth", "Maximum attenuation applied to a colliding band."));
    controls.push_back (&addKnob (pid::specBlend, "Blend",
                                  "In Hybrid mode, how much of the ducking the spectral stage does instead of the broadband stage."));
    controls.push_back (&addKnob (pid::specPreserve, "Preserve Body",
                                  "Raise this to duck only where the collision is decisive, so the track keeps its own fundamental."));

    addGroup ("Range");
    controls.push_back (&addKnob (pid::specLowFreq, "Low", "Nothing below this is carved."));
    controls.push_back (&addKnob (pid::specHighFreq, "High", "Nothing above this is carved."));
    controls.push_back (&addKnob (pid::specTilt, "Tilt", "Weights the collision map towards low or high frequencies."));

    addGroup ("Smoothing");
    controls.push_back (&addKnob (pid::specFreqSmooth, "Freq", "Smooths the collision map across neighbouring bins."));
    controls.push_back (&addKnob (pid::specTimeSmooth, "Time", "Extra smoothing over time, on top of attack and release."));
    controls.push_back (&addKnob (pid::specGuard, "Guard",
                                  "Artifact protection: pulls isolated deep notches towards their neighbours."));

    addGroup ("Response");
    controls.push_back (&addKnob (pid::specAttack, "Attack", "How fast a band is pulled down."));
    controls.push_back (&addKnob (pid::specRelease, "Release", "How fast a band comes back."));
    controls.push_back (&addKnob (pid::specTonal, "Tonal / Transient",
                                  "At 0 the map follows sustained sidechain content; at 100 it follows transients."));
}

void SpectrumPanel::updateRelevance (const EngineSettings& settings)
{
    const auto relevant = settings.spectralActive;

    for (auto* control : controls)
        setRelevance (*control, relevant);
}

// ===========================================================================
//  Bands
// ===========================================================================

BandsPanel::BandsPanel (const ControlContext& contextToUse)
    : PanelBase (contextToUse, "Bands")
{
    addGroup ("Split");
    addChoice (pid::bandMode, "Bands", "One broadband stage, or three bands with independent depth and timing.", 92);
    crossoverControls.push_back (&addKnob (pid::xover1, "Low / Mid", "First crossover point."));
    crossoverControls.push_back (&addKnob (pid::xover2, "Mid / High", "Second crossover point."));

    addGroup ("Channels");
    addChoice (pid::channelMode, "Mode",
               "Stereo links both channels. Left / Right can act independently. Mid / Side ducks the centre.", 104);
    channelLinkControl = &addKnob (pid::channelLink, "Link",
                                   "In Left / Right, how much the two channels share their reduction. "
                                   "In Mid / Side, how much of the ducking reaches the sides.");

    addGroup ("Mono Low");
    addToggle (pid::monoLowEnable, "Mono Below", "Sums the bottom of the stereo image to mono.");
    monoLowFreqControl = &addKnob (pid::monoLowFreq, "Frequency", "Everything below this becomes mono.");

    addGroup ("Band 1  Low");
    addToggle (pid::bandEnable[0], "On", "Bypasses this band's gain reduction.");
    addKnob (pid::bandDepth[0], "Depth", "How much of the gain signal this band receives.");
    addKnob (pid::bandAttack[0], "Attack", "Per-band attack, on top of the engine's own timing.");
    addKnob (pid::bandRelease[0], "Release", "Per-band release, on top of the engine's own timing.");

    addGroup ("Band 2  Mid");
    upperBandControls.push_back (&addToggle (pid::bandEnable[1], "On", "Bypasses this band's gain reduction."));
    upperBandControls.push_back (&addKnob (pid::bandDepth[1], "Depth", "How much of the gain signal this band receives."));
    upperBandControls.push_back (&addKnob (pid::bandAttack[1], "Attack", "Per-band attack."));
    upperBandControls.push_back (&addKnob (pid::bandRelease[1], "Release", "Per-band release."));

    addGroup ("Band 3  High");
    upperBandControls.push_back (&addToggle (pid::bandEnable[2], "On", "Bypasses this band's gain reduction."));
    upperBandControls.push_back (&addKnob (pid::bandDepth[2], "Depth", "How much of the gain signal this band receives."));
    upperBandControls.push_back (&addKnob (pid::bandAttack[2], "Attack", "Per-band attack."));
    upperBandControls.push_back (&addKnob (pid::bandRelease[2], "Release", "Per-band release."));
}

void BandsPanel::updateRelevance (const EngineSettings& settings)
{
    const auto threeBand = settings.bands.bandMode == BandMode::Three;

    for (auto* control : crossoverControls)
        setRelevance (*control, threeBand);

    for (auto* control : upperBandControls)
        setRelevance (*control, threeBand);

    // Stereo mode is fully linked by definition, so the link amount has no job.
    if (channelLinkControl != nullptr)
        setRelevance (*channelLinkControl, settings.bands.channelMode != ChannelMode::Stereo);

    if (monoLowFreqControl != nullptr)
        setRelevance (*monoLowFreqControl, settings.bands.monoLowEnable);
}

// ===========================================================================
//  Routing
// ===========================================================================

RoutingPanel::RoutingPanel (const ControlContext& contextToUse)
    : PanelBase (contextToUse, "Routing")
{
    addGroup ("Sidechain");
    addChoice (pid::scSource, "Source",
               "External uses the sidechain bus. Internal keys the plugin from the track it is on.", 104);
    addKnob (pid::scGain, "SC Gain", "Trims the sidechain before the detector sees it.");
    addToggle (pid::scAudition, "Audition",
               "Monitor the filtered detector signal, so you can hear exactly what is driving the gain.",
               theme::warning);

    addGroup ("Detector Filter");
    addKnob (pid::scHighPass, "High Pass", "Keeps low rumble out of the detector.");
    addKnob (pid::scLowPass, "Low Pass", "Keeps high content out of the detector.");
    addKnob (pid::compStereoLink, "Stereo Link", "How much the two detector channels share their envelope.");

    addGroup ("Quality");
    addChoice (pid::quality, "Mode",
               "Live has no latency and no spectral stage. Studio, Spectral and Render trade latency for resolution.", 104);
    lookaheadControl = &addKnob (pid::compLookahead, "Lookahead",
                                 "Delays the audio so the gain change can start before the transient. "
                                 "Adds reported latency, and is capped by the quality mode.");
}

void RoutingPanel::updateRelevance (const EngineSettings& settings)
{
    // Live mode is defined as zero latency, so lookahead has nothing to give.
    if (lookaheadControl != nullptr)
        setRelevance (*lookaheadControl, qualityConfig (settings.quality).maxLookaheadMs > 0.0f);
}

// ===========================================================================
//  Output
// ===========================================================================

OutputPanel::OutputPanel (const ControlContext& contextToUse)
    : PanelBase (contextToUse, "Output")
{
    addGroup ("Levels");
    addKnob (pid::inputGain, "Input", "Trim before any processing.");
    addKnob (pid::mix, "Mix", "Blends the processed signal against the latency-matched dry signal.");
    addKnob (pid::outputGain, "Output", "Trim after everything else.");
    autoGainControl = &addToggle (pid::autoGain, "Auto Gain",
                                  "Makes up the level the compressor's curve removes at a nominal mix level.");

    addGroup ("Monitor");
    addChoice (pid::monitor, "Listen",
               "Normal output, the raw sidechain bus, or Delta: only the signal the plugin removed.", 104);

    addGroup ("Visualiser");
    addToggle (pid::vizEnabled, "Enabled", "Turns the display off entirely. Processing is unaffected.");
    addToggle (pid::vizReduced, "Reduced", "Halves the repaint rate and drops the glow passes.");
}

void OutputPanel::updateRelevance (const EngineSettings& settings)
{
    // Auto gain compensates the compressor's static curve, so it only means
    // something when the compressor is in the path.
    if (autoGainControl != nullptr)
        setRelevance (*autoGainControl, settings.useCompressor);
}
} // namespace sf
