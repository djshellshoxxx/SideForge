#include "FactoryPresets.h"
#include "../Parameters/ParamIDs.h"
#include "../Parameters/Divisions.h"

namespace sf::FactoryPresets
{
namespace
{
struct Setting
{
    const char* id;
    float value;   // in the parameter's own units; choices use their index
};

struct Preset
{
    const char* name;
    const char* description;
    int curveShape;                 // index into CurveModel::getShapeNames()
    std::initializer_list<Setting> settings;
};

// Division indices, named so the tables below stay readable.
constexpr int kDiv16  = 7;    // 1/16
constexpr int kDiv8   = 10;   // 1/8
constexpr int kDiv4   = 13;   // 1/4
constexpr int kDiv1Bar = 21;  // 1 Bar

const Preset kPresets[] =
{
    { "Kick vs Bass",
      "Fast broadband duck keyed from the kick, with the detector listening only to the low end.",
      0,
      { { pid::mode, (float) Mode::Compress }, { pid::quality, (float) Quality::Studio },
        { pid::compThreshold, -26.0f }, { pid::compRatio, 6.0f }, { pid::compKnee, 3.0f },
        { pid::compAttack, 0.5f }, { pid::compHold, 10.0f }, { pid::compRelease, 90.0f },
        { pid::compRange, 12.0f }, { pid::compDetector, (float) Detector::Peak },
        { pid::compStereoLink, 100.0f },
        { pid::scHighPass, 30.0f }, { pid::scLowPass, 220.0f },
        { pid::bandMode, (float) BandMode::Single },
        { pid::specBlend, 0.0f } } },

    { "Vocal vs Music",
      "Spectral ducking that opens space for a vocal only where the music actually collides with it.",
      0,
      { { pid::mode, (float) Mode::Spectral }, { pid::quality, (float) Quality::Spectral },
        { pid::specDepth, 9.0f }, { pid::specLowFreq, 180.0f }, { pid::specHighFreq, 9000.0f },
        { pid::specPreserve, 40.0f }, { pid::specGuard, 55.0f }, { pid::specFreqSmooth, 40.0f },
        { pid::specTimeSmooth, 45.0f }, { pid::specAttack, 8.0f }, { pid::specRelease, 120.0f },
        { pid::specTonal, 35.0f }, { pid::specBlend, 100.0f },
        { pid::scHighPass, 90.0f }, { pid::scLowPass, 12000.0f } } },

    { "Transparent Spectral Space",
      "A gentle, wide collision map. Almost inaudible on its own, obvious the moment you bypass it.",
      0,
      { { pid::mode, (float) Mode::Spectral }, { pid::quality, (float) Quality::Spectral },
        { pid::specDepth, 6.0f }, { pid::specLowFreq, 120.0f }, { pid::specHighFreq, 14000.0f },
        { pid::specPreserve, 60.0f }, { pid::specGuard, 70.0f }, { pid::specFreqSmooth, 55.0f },
        { pid::specTimeSmooth, 60.0f }, { pid::specAttack, 15.0f }, { pid::specRelease, 200.0f },
        { pid::specTonal, 25.0f }, { pid::specBlend, 100.0f } } },

    { "Four On The Floor Pump",
      "Tempo-locked quarter-note pump. No sidechain input required.",
      1,
      { { pid::mode, (float) Mode::Curve }, { pid::quality, (float) Quality::Live },
        { pid::curveSyncMode, (float) SyncMode::Tempo }, { pid::curveDivision, (float) kDiv4 },
        { pid::curveTrigMode, (float) TrigMode::Loop }, { pid::curveDepth, 100.0f },
        { pid::curveSmooth, 2.0f }, { pid::curvePhase, 0.0f }, { pid::curveSwing, 0.0f },
        { pid::compLookahead, 0.0f } } },

    { "Irregular Live Kick",
      "Curve re-triggered by the sidechain, for a drummer who does not play to a grid.",
      1,
      { { pid::mode, (float) Mode::Curve }, { pid::quality, (float) Quality::Studio },
        { pid::curveSyncMode, (float) SyncMode::Free }, { pid::curveRateHz, 2.4f },
        { pid::curveTrigMode, (float) TrigMode::Audio }, { pid::curveDepth, 100.0f },
        { pid::curveSmooth, 3.0f },
        { pid::trigSource, (float) TrigSource::Sidechain },
        { pid::trigDetector, (float) TrigDetect::Transient },
        { pid::trigThreshold, -28.0f }, { pid::trigHysteresis, 6.0f },
        { pid::trigMinInterval, 80.0f },
        { pid::trigHighPass, 35.0f }, { pid::trigLowPass, 250.0f } } },

    { "Mid/Side Vocal Clearing",
      "Ducks the centre and leaves the sides open, so a lead vocal sits in front of a wide bed.",
      0,
      { { pid::mode, (float) Mode::Hybrid }, { pid::quality, (float) Quality::Spectral },
        { pid::channelMode, (float) ChannelMode::MidSide }, { pid::channelLink, 15.0f },
        { pid::compThreshold, -30.0f }, { pid::compRatio, 3.0f }, { pid::compRange, 8.0f },
        { pid::compAttack, 6.0f }, { pid::compRelease, 160.0f },
        { pid::specDepth, 8.0f }, { pid::specLowFreq, 200.0f }, { pid::specHighFreq, 8000.0f },
        { pid::specPreserve, 45.0f }, { pid::specBlend, 70.0f },
        { pid::curveDepth, 0.0f } } },

    { "Aggressive Stutter",
      "Sixteenth-note gate. Hard steps, minimal smoothing.",
      6,
      { { pid::mode, (float) Mode::Curve }, { pid::quality, (float) Quality::Live },
        { pid::curveSyncMode, (float) SyncMode::Tempo }, { pid::curveDivision, (float) kDiv16 },
        { pid::curveTrigMode, (float) TrigMode::Loop }, { pid::curveDepth, 100.0f },
        { pid::curveSmooth, 0.6f }, { pid::curveSwing, 0.0f } } },

    { "Sub Only Ducking",
      "Three-band split with only the low band ducking, so the sub steps aside and nothing else moves.",
      0,
      { { pid::mode, (float) Mode::Compress }, { pid::quality, (float) Quality::Studio },
        { pid::bandMode, (float) BandMode::Three },
        { pid::xover1, 110.0f }, { pid::xover2, 3000.0f },
        { pid::bandDepth[0], 100.0f }, { pid::bandDepth[1], 0.0f }, { pid::bandDepth[2], 0.0f },
        { pid::bandAttack[0], 1.0f }, { pid::bandRelease[0], 60.0f },
        { pid::compThreshold, -28.0f }, { pid::compRatio, 8.0f }, { pid::compRange, 18.0f },
        { pid::compAttack, 1.0f }, { pid::compRelease, 110.0f },
        { pid::scHighPass, 25.0f }, { pid::scLowPass, 180.0f },
        { pid::monoLowEnable, 1.0f }, { pid::monoLowFreq, 110.0f } } },
};

constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (Preset));

/** A loop-length curve for the 1-bar presets that want a longer shape. */
void applyShape (CurveModel& curve, int shapeIndex)
{
    curve.setPresetShape (juce::jlimit (0, CurveModel::getShapeNames().size() - 1, shapeIndex));
}
} // namespace

int getNumPresets() { return kNumPresets; }

juce::String getName (int index)
{
    return kPresets[juce::jlimit (0, kNumPresets - 1, index)].name;
}

juce::String getDescription (int index)
{
    return kPresets[juce::jlimit (0, kNumPresets - 1, index)].description;
}

juce::StringArray getNames()
{
    juce::StringArray names;

    for (const auto& p : kPresets)
        names.add (p.name);

    return names;
}

void apply (int index, juce::AudioProcessorValueTreeState& apvts, CurveModel& curve)
{
    const auto& preset = kPresets[juce::jlimit (0, kNumPresets - 1, index)];

    // Start from defaults so a preset never inherits stray values from the state
    // that happened to be loaded before it.
    for (auto* parameter : apvts.processor.getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            withID->setValueNotifyingHost (withID->getDefaultValue());

    for (const auto& setting : preset.settings)
    {
        if (auto* parameter = apvts.getParameter (setting.id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (setting.value));
    }

    applyShape (curve, preset.curveShape);

    juce::ignoreUnused (kDiv8, kDiv1Bar);
}
} // namespace sf::FactoryPresets
