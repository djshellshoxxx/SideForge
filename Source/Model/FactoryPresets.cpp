#include "FactoryPresets.h"
#include "../Parameters/ParamIDs.h"
#include "../Parameters/BandMatrixParameters.h"
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

/** Shorthands for the Band Matrix tables below. `BM(n)` is band n's ID block. */
#define BM(n) pid::band[n]

constexpr float kMatrix   = (float) RoutingMode::BandMatrix;
constexpr float kFull     = (float) RoutingMode::FullRange;
constexpr float kOff      = (float) BandProcessorMode::Off;
constexpr float kDuck     = (float) BandProcessorMode::Ducker;
constexpr float kComp     = (float) BandProcessorMode::Compressor;
constexpr float kLimit    = (float) BandProcessorMode::Limiter;
constexpr float kScExt    = (float) BandScSource::External;
constexpr float kScExtBand = (float) BandScSource::ExternalBand;
constexpr float kTwoBands   = 1.0f;   // the choice index, not the count
constexpr float kThreeBands = 2.0f;

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

    // ---------------------------------------------------------------------
    //  Band Matrix
    // ---------------------------------------------------------------------

    { "Kick vs Sub Bass",
      "Two bands. Everything under 90 Hz ducks hard for the kick; the rest of the bass is untouched.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kTwoBands },
        { pid::bmXover1, 90.0f },
        { BM(0).mode, kDuck }, { BM(0).depth, 100.0f }, { BM(0).range, 24.0f },
        { BM(0).threshold, -34.0f }, { BM(0).attack, 0.5f }, { BM(0).release, 120.0f },
        { BM(0).scSource, kScExt },
        { BM(1).mode, kOff } } },

    { "Kick vs Full Bass",
      "Two bands with a higher split, so the whole bass register steps aside rather than just the sub.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kTwoBands },
        { pid::bmXover1, 250.0f },
        { BM(0).mode, kDuck }, { BM(0).depth, 100.0f }, { BM(0).range, 18.0f },
        { BM(0).threshold, -32.0f }, { BM(0).attack, 1.0f }, { BM(0).release, 150.0f },
        { BM(1).mode, kOff } } },

    { "Hard Lows, Light Highs",
      "The headline Band Matrix move: severe low-end space, a touch of movement up top, nothing in between that pumps.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 120.0f }, { pid::bmXover2, 2500.0f },
        { BM(0).mode, kDuck }, { BM(0).depth, 100.0f }, { BM(0).range, 18.0f },
        { BM(0).attack, 0.4f }, { BM(0).release, 140.0f }, { BM(0).threshold, -34.0f },
        { BM(1).mode, kComp }, { BM(1).depth, 45.0f }, { BM(1).ratio, 3.0f },
        { BM(1).range, 12.0f }, { BM(1).attack, 8.0f }, { BM(1).release, 100.0f },
        { BM(2).mode, kDuck }, { BM(2).depth, 15.0f }, { BM(2).range, 9.0f },
        { BM(2).attack, 18.0f }, { BM(2).release, 60.0f } } },

    { "Vocal vs Music Bands",
      "Three bands keyed from a vocal, with the midrange taking most of the movement.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 200.0f }, { pid::bmXover2, 5000.0f },
        { BM(0).mode, kDuck }, { BM(0).depth, 30.0f }, { BM(0).range, 9.0f },
        { BM(1).mode, kDuck }, { BM(1).depth, 85.0f }, { BM(1).range, 12.0f },
        { BM(1).attack, 6.0f }, { BM(1).release, 180.0f }, { BM(1).threshold, -36.0f },
        { BM(2).mode, kDuck }, { BM(2).depth, 35.0f }, { BM(2).range, 8.0f },
        { BM(2).attack, 12.0f }, { BM(2).release, 140.0f } } },

    { "Vocal Presence Pocket",
      "Carves a narrow presence window so a lead sits forward, leaving the low end and the air alone.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 1200.0f }, { pid::bmXover2, 6000.0f },
        { BM(0).mode, kOff },
        { BM(1).mode, kDuck }, { BM(1).depth, 90.0f }, { BM(1).range, 10.0f },
        { BM(1).attack, 5.0f }, { BM(1).release, 200.0f }, { BM(1).threshold, -38.0f },
        { BM(2).mode, kOff } } },

    { "Midrange Dialogue Space",
      "Gentle, slow midrange ducking for dialogue over a bed. Wide band, low depth, long release.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 250.0f }, { pid::bmXover2, 4000.0f },
        { BM(0).mode, kOff },
        { BM(1).mode, kDuck }, { BM(1).depth, 60.0f }, { BM(1).range, 7.0f },
        { BM(1).attack, 20.0f }, { BM(1).release, 400.0f }, { BM(1).threshold, -40.0f },
        { BM(2).mode, kOff } } },

    { "Snare vs Synth Mids",
      "Fast, short mid-band ducking that opens a slot for a snare without touching the sub or the top.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 180.0f }, { pid::bmXover2, 4000.0f },
        { BM(0).mode, kOff },
        { BM(1).mode, kDuck }, { BM(1).depth, 100.0f }, { BM(1).range, 14.0f },
        { BM(1).attack, 0.5f }, { BM(1).release, 70.0f }, { BM(1).threshold, -30.0f },
        { BM(1).scSource, kScExtBand },
        { BM(2).mode, kDuck }, { BM(2).depth, 20.0f }, { BM(2).range, 8.0f } } },

    { "Hi-Hat vs Bright Pads",
      "Only the top band moves, keyed from the matching high band of the sidechain.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 200.0f }, { pid::bmXover2, 6000.0f },
        { BM(0).mode, kOff },
        { BM(1).mode, kOff },
        { BM(2).mode, kDuck }, { BM(2).depth, 80.0f }, { BM(2).range, 10.0f },
        { BM(2).attack, 1.0f }, { BM(2).release, 60.0f }, { BM(2).threshold, -38.0f },
        { BM(2).scSource, kScExtBand } } },

    { "Sub-Only Duck",
      "A single low band under 70 Hz ducking hard. The most surgical way to make room for a kick.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kTwoBands },
        { pid::bmXover1, 70.0f },
        { BM(0).mode, kDuck }, { BM(0).depth, 100.0f }, { BM(0).range, 30.0f },
        { BM(0).attack, 0.3f }, { BM(0).release, 100.0f }, { BM(0).threshold, -36.0f },
        { BM(1).mode, kOff } } },

    { "Three-Band Pump",
      "All three bands ducking on the same trigger, with progressively lighter depth and slower attack upwards.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 150.0f }, { pid::bmXover2, 3000.0f },
        { BM(0).mode, kDuck }, { BM(0).depth, 100.0f }, { BM(0).range, 18.0f },
        { BM(0).attack, 0.5f }, { BM(0).release, 160.0f },
        { BM(1).mode, kDuck }, { BM(1).depth, 70.0f }, { BM(1).range, 14.0f },
        { BM(1).attack, 4.0f }, { BM(1).release, 140.0f },
        { BM(2).mode, kDuck }, { BM(2).depth, 45.0f }, { BM(2).range, 10.0f },
        { BM(2).attack, 10.0f }, { BM(2).release, 120.0f } } },

    { "Transparent Multiband Duck",
      "Low depth everywhere and slow timing. Hard to hear working, obvious the moment you bypass it.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 130.0f }, { pid::bmXover2, 3500.0f },
        { BM(0).mode, kComp }, { BM(0).depth, 40.0f }, { BM(0).range, 8.0f },
        { BM(0).ratio, 2.5f }, { BM(0).attack, 12.0f }, { BM(0).release, 250.0f },
        { BM(1).mode, kComp }, { BM(1).depth, 30.0f }, { BM(1).range, 6.0f },
        { BM(1).ratio, 2.0f }, { BM(1).attack, 20.0f }, { BM(1).release, 300.0f },
        { BM(2).mode, kComp }, { BM(2).depth, 20.0f }, { BM(2).range, 5.0f },
        { BM(2).ratio, 2.0f }, { BM(2).attack, 25.0f }, { BM(2).release, 300.0f } } },

    { "Low-Band Limiter",
      "A ceiling on the low band whenever the trigger hits, leaving everything above it completely alone.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kTwoBands },
        { pid::bmXover1, 160.0f },
        { BM(0).mode, kLimit }, { BM(0).depth, 100.0f }, { BM(0).range, 30.0f },
        { BM(0).threshold, -26.0f }, { BM(0).knee, 2.0f },
        { BM(0).attack, 0.2f }, { BM(0).release, 60.0f }, { BM(0).lookahead, 2.0f },
        { BM(1).mode, kOff } } },

    { "Mid Compression, High Duck",
      "Two different processors at once: the mids are compressed, the top is ducked.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 200.0f }, { pid::bmXover2, 4000.0f },
        { BM(0).mode, kOff },
        { BM(1).mode, kComp }, { BM(1).depth, 80.0f }, { BM(1).range, 14.0f },
        { BM(1).ratio, 4.0f }, { BM(1).attack, 6.0f }, { BM(1).release, 120.0f },
        { BM(2).mode, kDuck }, { BM(2).depth, 55.0f }, { BM(2).range, 10.0f },
        { BM(2).attack, 8.0f }, { BM(2).release, 90.0f } } },

    { "Wide Pads Behind Vocal",
      "Mid and high bands duck while the low band holds, so a pad recedes without losing its weight.",
      0,
      { { pid::routingMode, kMatrix }, { pid::bmBandCount, kThreeBands },
        { pid::bmXover1, 160.0f }, { pid::bmXover2, 3000.0f },
        { BM(0).mode, kOff },
        { BM(1).mode, kDuck }, { BM(1).depth, 75.0f }, { BM(1).range, 11.0f },
        { BM(1).attack, 10.0f }, { BM(1).release, 260.0f }, { BM(1).threshold, -38.0f },
        { BM(2).mode, kDuck }, { BM(2).depth, 60.0f }, { BM(2).range, 9.0f },
        { BM(2).attack, 14.0f }, { BM(2).release, 240.0f }, { BM(2).threshold, -38.0f } } },

    { "Full-Range Classic Sidechain",
      "No band split at all: the original full-range compressor, for when the whole track should move together.",
      0,
      { { pid::routingMode, kFull },
        { pid::mode, (float) Mode::Compress }, { pid::quality, (float) Quality::Studio },
        { pid::compThreshold, -28.0f }, { pid::compRatio, 8.0f }, { pid::compKnee, 4.0f },
        { pid::compAttack, 1.0f }, { pid::compRelease, 140.0f }, { pid::compRange, 15.0f },
        { pid::compDetector, (float) Detector::Peak },
        { pid::scHighPass, 25.0f }, { pid::scLowPass, 8000.0f } } },

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
