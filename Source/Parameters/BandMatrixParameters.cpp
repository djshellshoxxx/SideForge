#include "BandMatrixParameters.h"
#include <cmath>

namespace sf
{
namespace
{
using APF   = juce::AudioParameterFloat;
using APC   = juce::AudioParameterChoice;
using APB   = juce::AudioParameterBool;
using Group = juce::AudioProcessorParameterGroup;
using Range = juce::NormalisableRange<float>;

/** Band Matrix parameters are version 2: they did not exist in version 1, which
    is exactly how a version-1 session ends up with the defaults. */
constexpr int kBandMatrixParamVersion = 2;

juce::ParameterID makeID (const char* id) { return { id, kBandMatrixParamVersion }; }

Range skewed (float lo, float hi, float centre)
{
    Range r { lo, hi };
    r.setSkewForCentre (centre);
    return r;
}

juce::String dbText    (float v, int) { return juce::String (v, std::abs (v) < 10.0f ? 2 : 1) + " dB"; }
juce::String msText    (float v, int) { return v < 10.0f ? juce::String (v, 2) + " ms" : juce::String (v, 1) + " ms"; }
juce::String hzText    (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, 1) + " Hz"; }
juce::String pctText   (float v, int) { return juce::String (v, 1) + " %"; }
juce::String ratioText (float v, int) { return v >= 39.5f ? juce::String ("inf:1") : juce::String (v, 2) + ":1"; }

juce::String panText (float v, int)
{
    if (std::abs (v) < 0.5f)
        return "C";

    return (v < 0.0f ? "L" : "R") + juce::String (std::abs (v), 0);
}

std::unique_ptr<APF> fp (const char* id, const juce::String& name, Range range, float def,
                         juce::String (*toText) (float, int))
{
    return std::make_unique<APF> (makeID (id), name, range, def,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText));
}

std::unique_ptr<APC> cp (const char* id, const juce::String& name, juce::StringArray choices, int def)
{
    return std::make_unique<APC> (makeID (id), name, std::move (choices), def);
}

std::unique_ptr<APB> bp (const char* id, const juce::String& name, bool def)
{
    return std::make_unique<APB> (makeID (id), name, def);
}

/** Solo and mute are monitoring aids. They are saved with the session so it
    comes back as you left it, but automating them from a host would be a way to
    accidentally silence a mix, so they are not offered as automation targets. */
std::unique_ptr<APB> monitoringToggle (const char* id, const juce::String& name, bool def)
{
    return std::make_unique<APB> (makeID (id), name, def,
                                  juce::AudioParameterBoolAttributes().withAutomatable (false));
}

/** Defaults that make a freshly enabled band useful rather than inert:
    the low band ducks hard, the top band barely moves. */
struct BandDefaults
{
    int   mode;
    float depth;
    float range;
    float attack;
    float release;
    int   scSource;
};

constexpr BandDefaults kBandDefaults[kMaxMatrixBands] =
{
    { (int) BandProcessorMode::Ducker,     100.0f, 18.0f,  1.0f, 140.0f, (int) BandScSource::External },
    { (int) BandProcessorMode::Compressor,  60.0f, 12.0f,  8.0f, 120.0f, (int) BandScSource::External },
    { (int) BandProcessorMode::Ducker,      25.0f,  9.0f, 15.0f,  90.0f, (int) BandScSource::External },
    { (int) BandProcessorMode::Off,         25.0f,  9.0f, 15.0f,  90.0f, (int) BandScSource::External },
};

/** Centre-detented multiplier: 50 % -> 1.0. */
float bipolarScale (float percent, float range) noexcept
{
    const auto unit = (juce::jlimit (0.0f, 100.0f, percent) - 50.0f) / 50.0f;
    return std::pow (range, unit);
}
} // namespace

// ---------------------------------------------------------------------------

juce::StringArray bandModeNames()
{
    return { "Off", "Ducker", "Compressor", "Limiter", "Curve", "Spectral", "Listen" };
}

juce::StringArray bandScSourceNames()
{
    return { "External", "External Band", "Internal", "Internal Band" };
}

juce::StringArray routingModeNames()
{
    return { "Full Range", "Band Matrix" };
}

juce::StringArray bandCountNames()
{
    return { "1 Band", "2 Bands", "3 Bands", "4 Bands" };
}

juce::String defaultBandName (int bandIndex, int numBands)
{
    bandIndex = juce::jlimit (0, kMaxMatrixBands - 1, bandIndex);
    numBands = juce::jlimit (1, kMaxMatrixBands, numBands);

    if (numBands == 1)
        return "Full";

    if (bandIndex == 0)
        return "Low";

    if (bandIndex == numBands - 1)
        return "High";

    if (numBands == 3)
        return "Mid";

    return bandIndex == 1 ? "Low Mid" : "High Mid";
}

// ---------------------------------------------------------------------------

void addBandMatrixParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add (std::make_unique<Group> ("bandMatrix", "Band Matrix", "|",
        cp (pid::routingMode, "Routing", routingModeNames(), (int) RoutingMode::FullRange),
        cp (pid::bmBandCount, "Matrix Bands", bandCountNames(), 2),   // three bands
        fp (pid::bmXover1, "Matrix Crossover 1", skewed (20.0f, 1000.0f, 150.0f),    120.0f, hzText),
        fp (pid::bmXover2, "Matrix Crossover 2", skewed (200.0f, 8000.0f, 2000.0f), 2500.0f, hzText),
        fp (pid::bmXover3, "Matrix Crossover 3", skewed (1000.0f, 18000.0f, 8000.0f), 8000.0f, hzText),
        fp (pid::bmPunch,    "Matrix Punch",    Range { 0.0f, 100.0f }, 50.0f, pctText),
        fp (pid::bmRecovery, "Matrix Recovery", Range { 0.0f, 100.0f }, 50.0f, pctText)));

    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        const auto& ids = pid::band[b];
        const auto& defaults = kBandDefaults[b];
        const auto label = juce::String ("Band ") + juce::String (b + 1) + " ";
        const auto groupId = juce::String ("bandMatrixBand") + juce::String (b + 1);

        layout.add (std::make_unique<Group> (groupId, "Band Matrix " + juce::String (b + 1), "|",
            cp (ids.mode, label + "Mode", bandModeNames(), defaults.mode),
            cp (ids.scSource, label + "SC Source", bandScSourceNames(), defaults.scSource),
            cp (ids.detector, label + "Detection",
                juce::StringArray { "Peak", "RMS", "Hybrid" }, (int) Detector::Peak),

            bp (ids.enable, label + "Enable", true),
            bp (ids.bypass, label + "Bypass", false),
            monitoringToggle (ids.solo, label + "Solo", false),
            monitoringToggle (ids.mute, label + "Mute", false),
            bp (ids.autoRelease, label + "Auto Release", false),
            bp (ids.scFilter, label + "SC Filter", false),
            monitoringToggle (ids.scListen, label + "SC Listen", false),

            fp (ids.depth,     label + "Depth",     Range { 0.0f, 100.0f },          defaults.depth, pctText),
            fp (ids.range,     label + "Range",     Range { 0.0f, 60.0f },           defaults.range, dbText),
            fp (ids.threshold, label + "Threshold", Range { -60.0f, 0.0f },          -30.0f, dbText),
            fp (ids.ratio,     label + "Ratio",     skewed (1.0f, 40.0f, 4.0f),        4.0f, ratioText),
            fp (ids.knee,      label + "Knee",      Range { 0.0f, 24.0f },             6.0f, dbText),
            fp (ids.attack,    label + "Attack",    skewed (0.05f, 200.0f, 10.0f),  defaults.attack, msText),
            fp (ids.hold,      label + "Hold",      skewed (0.0f, 500.0f, 50.0f),      0.0f, msText),
            fp (ids.release,   label + "Release",   skewed (5.0f, 2000.0f, 200.0f), defaults.release, msText),
            fp (ids.lookahead, label + "Lookahead", Range { 0.0f, 20.0f },             0.0f, msText),
            fp (ids.makeup,    label + "Makeup",    Range { -12.0f, 24.0f },           0.0f, dbText),
            fp (ids.gain,      label + "Gain",      Range { -24.0f, 24.0f },           0.0f, dbText),
            fp (ids.pan,       label + "Pan",       Range { -100.0f, 100.0f },         0.0f, panText),
            fp (ids.mix,       label + "Mix",       Range { 0.0f, 100.0f },          100.0f, pctText),
            fp (ids.scHighPass, label + "SC High Pass", skewed (20.0f, 2000.0f, 200.0f),      20.0f, hzText),
            fp (ids.scLowPass,  label + "SC Low Pass",  skewed (200.0f, 20000.0f, 2000.0f), 20000.0f, hzText)));
    }
}

// ---------------------------------------------------------------------------

void BandMatrixParamRefs::attach (juce::AudioProcessorValueTreeState& state)
{
    auto get = [&state] (const char* id) { return state.getRawParameterValue (id); };

    routingMode = get (pid::routingMode);
    bandCount   = get (pid::bmBandCount);
    crossover[0] = get (pid::bmXover1);
    crossover[1] = get (pid::bmXover2);
    crossover[2] = get (pid::bmXover3);
    punch    = get (pid::bmPunch);
    recovery = get (pid::bmRecovery);

    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        const auto& ids = pid::band[b];
        auto& band = bands[b];

        band.mode        = get (ids.mode);
        band.enable      = get (ids.enable);
        band.bypass      = get (ids.bypass);
        band.solo        = get (ids.solo);
        band.mute        = get (ids.mute);
        band.depth       = get (ids.depth);
        band.range       = get (ids.range);
        band.threshold   = get (ids.threshold);
        band.ratio       = get (ids.ratio);
        band.knee        = get (ids.knee);
        band.attack      = get (ids.attack);
        band.hold        = get (ids.hold);
        band.release     = get (ids.release);
        band.autoRelease = get (ids.autoRelease);
        band.detector    = get (ids.detector);
        band.lookahead   = get (ids.lookahead);
        band.makeup      = get (ids.makeup);
        band.gain        = get (ids.gain);
        band.pan         = get (ids.pan);
        band.mix         = get (ids.mix);
        band.scSource    = get (ids.scSource);
        band.scFilter    = get (ids.scFilter);
        band.scHighPass  = get (ids.scHighPass);
        band.scLowPass   = get (ids.scLowPass);
        band.scListen    = get (ids.scListen);
    }
}

RoutingMode readRoutingMode (const BandMatrixParamRefs& refs)
{
    return readEnum<RoutingMode> (refs.routingMode);
}

BandMatrix::Settings buildBandMatrixSettings (const BandMatrixParamRefs& refs,
                                              const SpectralDucker::Settings& spectralTemplate,
                                              int spectralFftOrder, int spectralOverlap,
                                              float depthScale, float timingScale)
{
    BandMatrix::Settings s;

    s.numBands = juce::jlimit (1, kMaxMatrixBands, readInt (refs.bandCount) + 1);

    for (int i = 0; i < kMaxCrossovers; ++i)
        s.crossoverHz[i] = readFloat (refs.crossover[i]);

    // Punch lengthens the attack — letting the transient through before the duck
    // arrives — and Recovery scales the release. They sit on top of the global
    // Timing macro rather than replacing it.
    const auto punchScale    = bipolarScale (readFloat (refs.punch), 4.0f);
    const auto recoveryScale = bipolarScale (readFloat (refs.recovery), 4.0f);

    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        const auto& source = refs.bands[b];
        auto& band = s.bands[b];

        band.mode    = readEnum<BandProcessorMode> (source.mode);
        band.enabled = readBool (source.enable);
        band.bypass  = readBool (source.bypass);

        band.depth    = juce::jlimit (0.0f, 1.0f, readFloat (source.depth) * 0.01f * depthScale);
        band.rangeDb  = readFloat (source.range);
        band.gainDb   = readFloat (source.gain);
        band.makeupDb = readFloat (source.makeup);
        band.pan      = juce::jlimit (-1.0f, 1.0f, readFloat (source.pan) * 0.01f);
        band.mix      = juce::jlimit (0.0f, 1.0f, readFloat (source.mix) * 0.01f);

        band.thresholdDb = readFloat (source.threshold);
        band.ratio       = readFloat (source.ratio);
        band.kneeDb      = readFloat (source.knee);
        band.attackMs    = readFloat (source.attack) * timingScale * punchScale;
        band.holdMs      = readFloat (source.hold) * timingScale;
        band.releaseMs   = readFloat (source.release) * timingScale * recoveryScale;
        band.autoRelease = readBool (source.autoRelease);
        band.detector    = readEnum<Detector> (source.detector);
        band.lookaheadMs = readFloat (source.lookahead);

        band.scSource       = readEnum<BandScSource> (source.scSource);
        band.scFilterEnable = readBool (source.scFilter);
        band.scHighPassHz   = readFloat (source.scHighPass);
        band.scLowPassHz    = readFloat (source.scLowPass);
        band.scListen       = readBool (source.scListen);

        // A band in Spectral mode uses the Spectrum panel's configuration and
        // contributes only its own Depth, so there is one place to set up the
        // spectral engine rather than four.
        band.spectral = spectralTemplate;
        band.spectralFftOrder = spectralFftOrder;
        band.spectralOverlap = spectralOverlap;

        s.solo[b] = readBool (source.solo);
        s.mute[b] = readBool (source.mute);
    }

    return s;
}
} // namespace sf
