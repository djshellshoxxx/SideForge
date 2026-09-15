#include "BandMatrixPanel.h"
#include "BandMatrixView.h"
#include "LookAndFeel.h"

namespace sf
{
namespace
{
/** A plain push button the panel owns, for actions that are not parameters. */
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

/** Selects which band the panel is showing. */
class BandSelectButton : public juce::TextButton
{
public:
    BandSelectButton (const juce::String& text, juce::Colour colour, std::function<void()> action)
    {
        setButtonText (text);
        setTooltip ("Show this band's controls. The band cards above select it too.");
        setClickingTogglesState (true);
        setRadioGroupId (0x5f4d);
        setColour (juce::TextButton::buttonOnColourId, colour);
        setColour (juce::TextButton::textColourOnId, theme::background);
        onClick = std::move (action);
    }
};

/** The band clipboard. One per plugin build rather than per instance, so a band
    can be copied from one instance of SideForge and pasted into another. */
bool copiedValid = false;
std::vector<float> copiedValues;   // normalised, in `bandParameterIds` order
} // namespace

std::vector<const char*> BandMatrixPanel::bandParameterIds (int band)
{
    const auto& b = pid::band[juce::jlimit (0, kMaxMatrixBands - 1, band)];

    // Solo and mute are deliberately absent: they are monitoring state, and
    // pasting someone else's solo into a band is never what was meant.
    return { b.mode, b.enable, b.bypass, b.depth, b.range, b.threshold, b.ratio, b.knee,
             b.attack, b.hold, b.release, b.autoRelease, b.detector, b.lookahead, b.makeup,
             b.gain, b.pan, b.mix, b.scSource, b.scFilter, b.scHighPass, b.scLowPass,
             b.scListen };
}

bool BandMatrixPanel::hasCopiedBand() noexcept { return copiedValid; }

void BandMatrixPanel::copyBand (const ControlContext& context, int band)
{
    const auto ids = bandParameterIds (band);
    copiedValues.clear();

    for (const auto* id : ids)
    {
        auto* parameter = context.getParameter (id);
        copiedValues.push_back (parameter != nullptr ? parameter->getValue() : 0.0f);
    }

    copiedValid = true;
}

void BandMatrixPanel::pasteBand (const ControlContext& context, int band)
{
    if (! copiedValid)
        return;

    const auto ids = bandParameterIds (band);

    for (size_t i = 0; i < ids.size() && i < copiedValues.size(); ++i)
    {
        if (auto* parameter = context.getParameter (ids[i]))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, copiedValues[i]));
            parameter->endChangeGesture();
        }
    }
}

void BandMatrixPanel::resetBand (const ControlContext& context, int band)
{
    for (const auto* id : bandParameterIds (band))
    {
        if (auto* parameter = context.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
    }
}

BandMatrixPanel::BandMatrixPanel (const ControlContext& contextToUse,
                                  std::function<void (int)> onBandSelected)
    : PanelBase (contextToUse, "Matrix"), bandSelected (std::move (onBandSelected))
{
    // ---- matrix-wide ---------------------------------------------------------
    addGroup ("Matrix");
    globalControls.push_back (&addChoice (pid::routingMode, "Routing",
                                          "Full Range runs the original single-path engine. "
                                          "Band Matrix splits the track into bands and gives each "
                                          "one its own detector and processor.", 112));
    globalControls.push_back (&addChoice (pid::bmBandCount, "Bands",
                                          "How many frequency regions the track is divided into.", 96));
    globalControls.push_back (&addKnob (pid::bmXover1, "Low / Mid",
                                        "First crossover. Drag the handle on the display to move it."));
    globalControls.push_back (&addKnob (pid::bmXover2, "Mid / High",
                                        "Second crossover. Only used with three or more bands."));
    globalControls.push_back (&addKnob (pid::bmXover3, "High / Top",
                                        "Third crossover. Only used with four bands."));

    addGroup ("Matrix Macros");
    globalControls.push_back (&addKnob (pid::bmPunch, "Punch",
                                        "Lengthens every band's attack, letting the transient through "
                                        "before the duck arrives. Centre is neutral."));
    globalControls.push_back (&addKnob (pid::bmRecovery, "Recovery",
                                        "Scales every band's release together. Centre is neutral."));

    // ---- per-band ------------------------------------------------------------
    for (int band = 0; band < kMaxMatrixBands; ++band)
        buildBandControls (band);

    // ---- band selection and clipboard ---------------------------------------
    addGroup ("Band");

    for (int band = 0; band < kMaxMatrixBands; ++band)
    {
        auto button = std::make_unique<BandSelectButton> (
            juce::String (band + 1), BandMatrixView::bandColour (band),
            [this, band]
            {
                setSelectedBand (band);

                if (bandSelected != nullptr)
                    bandSelected (band);
            });

        if (band == 0)
            button->setToggleState (true, juce::dontSendNotification);

        bandButtons.push_back (button.get());
        addCustom (std::move (button), 34, 26);
    }

    addGroup ("Clipboard");
    addCustom (std::make_unique<ActionButton> ("Copy",
                                               "Copies every setting of the selected band, except "
                                               "solo and mute.",
                                               [this] { copyBand (context, selectedBand); }),
               64, 26);
    addCustom (std::make_unique<ActionButton> ("Paste",
                                               "Pastes the copied band settings into the selected band.",
                                               [this] { pasteBand (context, selectedBand); }),
               64, 26);
    addCustom (std::make_unique<ActionButton> ("Reset",
                                               "Returns the selected band to its defaults.",
                                               [this] { resetBand (context, selectedBand); }),
               64, 26);

    applyVisibility();
}

void BandMatrixPanel::buildBandControls (int band)
{
    const auto& ids = pid::band[band];
    const auto name = juce::String ("Band ") + juce::String (band + 1);
    auto& section = sections[(size_t) band];

    auto remember = [&section] (juce::Component& control) -> juce::Component&
    {
        section.controls.push_back (&control);
        return control;
    };

    addGroup (name);
    remember (addChoice (ids.mode, "Processor",
                         "Off passes the band through. Ducker is a steep fixed-depth attenuation, "
                         "Compressor a conventional ratio, Limiter a ceiling on this band. "
                         "Curve follows the drawn envelope; Spectral runs collision ducking inside "
                         "the band; Listen auditions the detector.", 112));
    remember (addToggle (ids.enable, "Enable",
                         "Off suspends this band's processing. Its audio still passes through.",
                         theme::ok));
    remember (addToggle (ids.bypass, "Bypass",
                         "Passes this frequency region through unchanged, still latency compensated.",
                         theme::warning));
    remember (addToggle (ids.solo, "Solo",
                         "Hear only the soloed bands. Soloing several is additive.", theme::collision));
    remember (addToggle (ids.mute, "Mute",
                         "Removes this frequency region from the output.", theme::danger));

    addGroup (name + " Amount");
    remember (addKnob (ids.depth, "Depth",
                       "The headline control: how much of the configured reduction this band uses. "
                       "0 % is genuinely no processing."));
    remember (addKnob (ids.range, "Range", "Maximum reduction for this band before Depth scales it."));
    remember (addKnob (ids.gain, "Gain", "Output trim for this band alone."));
    section.compressorOnly.push_back (&remember (addKnob (ids.makeup, "Makeup",
                                                          "Make-up gain. Compressor and Limiter modes only.")));
    remember (addKnob (ids.mix, "Mix", "Dry/wet within this band."));
    remember (addKnob (ids.pan, "Pan", "Places this frequency region in the stereo field."));

    addGroup (name + " Dynamics");
    section.dynamicsOnly.push_back (&remember (addKnob (ids.threshold, "Threshold",
                                                        "Detector level at which this band starts to move.")));
    section.compressorOnly.push_back (&remember (addKnob (ids.ratio, "Ratio",
                                                          "Compressor mode only. Ducker and Limiter pin their own.")));
    section.dynamicsOnly.push_back (&remember (addKnob (ids.knee, "Knee",
                                                        "Softness of the corner around the threshold.")));

    addGroup (name + " Timing");
    section.dynamicsOnly.push_back (&remember (addKnob (ids.attack, "Attack", "How fast this band ducks.")));
    section.dynamicsOnly.push_back (&remember (addKnob (ids.hold, "Hold",
                                                        "Holds the deepest reduction before release starts.")));
    section.dynamicsOnly.push_back (&remember (addKnob (ids.release, "Release", "How fast this band recovers.")));
    remember (addKnob (ids.lookahead, "Lookahead",
                       "Delays this band so the reduction can start before the transient. "
                       "Every other band is padded to match."));
    section.dynamicsOnly.push_back (&remember (addToggle (ids.autoRelease, "Auto Release",
                                                          "Programme-dependent release for this band.")));

    addGroup (name + " Detector");
    remember (addChoice (ids.scSource, "Source",
                         "External is the whole sidechain; External Band is the sidechain split by "
                         "the same crossovers, so this band listens only to its own region. "
                         "Internal keys from the track itself.", 118));
    section.dynamicsOnly.push_back (&remember (addChoice (ids.detector, "Detection",
                                                          "Peak follows transients, RMS loudness.", 96)));
    remember (addToggle (ids.scFilter, "SC Filter",
                         "Filters what this band's detector hears. It never filters the audio."));
    section.filterOnly.push_back (&remember (addKnob (ids.scHighPass, "SC High Pass",
                                                      "Detector high-pass for this band only.")));
    section.filterOnly.push_back (&remember (addKnob (ids.scLowPass, "SC Low Pass",
                                                      "Detector low-pass for this band only.")));
    remember (addToggle (ids.scListen, "SC Listen",
                         "Auditions what this band's detector hears, in place of its audio.",
                         theme::warning));
}

void BandMatrixPanel::applyVisibility()
{
    for (int band = 0; band < kMaxMatrixBands; ++band)
        for (auto* control : sections[(size_t) band].controls)
            if (control != nullptr)
                control->setVisible (band == selectedBand);

    resized();
    repaint();
}

void BandMatrixPanel::setSelectedBand (int index)
{
    const auto clamped = juce::jlimit (0, kMaxMatrixBands - 1, index);

    if (clamped == selectedBand)
        return;

    selectedBand = clamped;

    for (int band = 0; band < (int) bandButtons.size(); ++band)
        if (auto* button = dynamic_cast<juce::Button*> (bandButtons[(size_t) band]))
            button->setToggleState (band == selectedBand, juce::dontSendNotification);

    applyVisibility();
}

void BandMatrixPanel::updateRelevance (const EngineSettings& settings)
{
    juce::ignoreUnused (settings);

    if (context.apvts == nullptr)
        return;

    auto raw = [this] (const char* id) { return context.apvts->getRawParameterValue (id); };

    const auto matrix = (readEnum<RoutingMode> (raw (pid::routingMode)) == RoutingMode::BandMatrix);
    const auto bandCount = juce::jlimit (1, kMaxMatrixBands, readInt (raw (pid::bmBandCount)) + 1);

    // Routing itself is always live - it is how you get into the matrix. The
    // crossovers a smaller band count does not use are greyed.
    for (size_t i = 0; i < globalControls.size(); ++i)
    {
        auto* control = globalControls[i];

        if (control == nullptr)
            continue;

        auto relevant = matrix;

        if (i == 0)      relevant = true;                       // Routing
        else if (i == 2) relevant = matrix && bandCount >= 2;    // Low / Mid
        else if (i == 3) relevant = matrix && bandCount >= 3;    // Mid / High
        else if (i == 4) relevant = matrix && bandCount >= 4;    // High / Top

        setRelevance (*control, relevant);
    }

    for (auto* button : bandButtons)
        if (button != nullptr)
            button->setEnabled (matrix);

    // Bands beyond the active count cannot be selected.
    for (int band = 0; band < (int) bandButtons.size(); ++band)
        if (bandButtons[(size_t) band] != nullptr)
            bandButtons[(size_t) band]->setEnabled (matrix && band < bandCount);

    // Within the selected band, grey whatever its processor mode does not use.
    const auto& ids = pid::band[selectedBand];
    const auto mode = readEnum<BandProcessorMode> (raw (ids.mode));
    const auto filterOn = readBool (raw (ids.scFilter));

    const auto usesDynamics = (mode == BandProcessorMode::Ducker
                            || mode == BandProcessorMode::Compressor
                            || mode == BandProcessorMode::Limiter);
    const auto usesRatio = (mode == BandProcessorMode::Compressor);
    const auto live = matrix && selectedBand < bandCount;
    const auto working = live && mode != BandProcessorMode::Off;

    auto& section = sections[(size_t) selectedBand];

    for (auto* control : section.controls)
        if (control != nullptr)
            setRelevance (*control, live);

    for (auto* control : section.dynamicsOnly)
        if (control != nullptr)
            setRelevance (*control, working && usesDynamics);

    for (auto* control : section.compressorOnly)
        if (control != nullptr)
            setRelevance (*control, working && usesRatio);

    for (auto* control : section.filterOnly)
        if (control != nullptr)
            setRelevance (*control, live && filterOn);
}
} // namespace sf
