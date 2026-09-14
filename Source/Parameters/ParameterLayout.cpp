#include "ParameterLayout.h"
#include "Divisions.h"

namespace sf
{
using APF   = juce::AudioParameterFloat;
using APC   = juce::AudioParameterChoice;
using APB   = juce::AudioParameterBool;
using API   = juce::AudioParameterInt;
using Group = juce::AudioProcessorParameterGroup;
using Range = juce::NormalisableRange<float>;

namespace
{
constexpr int kParamVersion = 1;

juce::ParameterID makeID (const char* id) { return { id, kParamVersion }; }

Range skewed (float lo, float hi, float centre)
{
    Range r { lo, hi };
    r.setSkewForCentre (centre);
    return r;
}

juce::String dbText     (float v, int) { return juce::String (v, std::abs (v) < 10.0f ? 2 : 1) + " dB"; }
juce::String msText     (float v, int) { return v < 10.0f ? juce::String (v, 2) + " ms" : juce::String (v, 1) + " ms"; }
juce::String hzText     (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, 1) + " Hz"; }
juce::String pctText    (float v, int) { return juce::String (v, 1) + " %"; }
juce::String ratioText  (float v, int) { return v >= 39.5f ? juce::String ("inf:1") : juce::String (v, 2) + ":1"; }
juce::String hzRateText (float v, int) { return v < 1.0f ? juce::String (1000.0f / v, 0) + " ms" : juce::String (v, 2) + " Hz"; }
juce::String phaseText  (float v, int) { return juce::String (v * 360.0f, 1) + " deg"; }

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
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Group> ("global", "Global", "|",
        cp (pid::mode,    "Mode",    juce::StringArray { "Compress", "Curve", "Spectral", "Hybrid" }, 0),
        cp (pid::quality, "Quality", juce::StringArray { "Live", "Studio", "Spectral", "Render" }, 1),
        cp (pid::monitor, "Monitor", juce::StringArray { "Normal", "Sidechain", "Delta" }, 0),
        bp (pid::bypass,   "Bypass", false),
        bp (pid::autoGain, "Auto Gain", false),
        fp (pid::mix,        "Mix",         Range { 0.0f, 100.0f },  100.0f, pctText),
        fp (pid::inputGain,  "Input Gain",  Range { -24.0f, 24.0f },   0.0f, dbText),
        fp (pid::outputGain, "Output Gain", Range { -24.0f, 24.0f },   0.0f, dbText)));

    layout.add (std::make_unique<Group> ("macros", "Macros", "|",
        fp (pid::macroDepth,      "Depth",      Range { 0.0f, 100.0f }, 50.0f, pctText),
        fp (pid::macroSeparation, "Separation", Range { 0.0f, 100.0f }, 50.0f, pctText),
        fp (pid::macroTiming,     "Timing",     Range { 0.0f, 100.0f }, 50.0f, pctText),
        fp (pid::macroCharacter,  "Character",  Range { 0.0f, 100.0f }, 50.0f, pctText),
        bp (pid::macrosEnabled,   "Macros Active", true)));

    layout.add (std::make_unique<Group> ("sidechain", "Sidechain", "|",
        cp (pid::scSource,   "SC Source", juce::StringArray { "External", "Internal" }, 0),
        fp (pid::scHighPass, "SC High Pass", skewed (20.0f, 2000.0f, 200.0f),       20.0f, hzText),
        fp (pid::scLowPass,  "SC Low Pass",  skewed (200.0f, 20000.0f, 2000.0f), 20000.0f, hzText),
        fp (pid::scGain,     "SC Gain",      Range { -24.0f, 24.0f },                0.0f, dbText),
        bp (pid::scAudition, "SC Audition", false)));

    layout.add (std::make_unique<Group> ("compressor", "Compressor", "|",
        fp (pid::compThreshold,  "Threshold",   Range { -60.0f, 0.0f },         -20.0f, dbText),
        fp (pid::compRatio,      "Ratio",       skewed (1.0f, 40.0f, 4.0f),       4.0f, ratioText),
        fp (pid::compKnee,       "Knee",        Range { 0.0f, 24.0f },            6.0f, dbText),
        fp (pid::compAttack,     "Attack",      skewed (0.05f, 200.0f, 10.0f),    5.0f, msText),
        fp (pid::compHold,       "Hold",        skewed (0.0f, 500.0f, 50.0f),     0.0f, msText),
        fp (pid::compRelease,    "Release",     skewed (5.0f, 2000.0f, 200.0f), 120.0f, msText),
        fp (pid::compRange,      "Range",       Range { 0.0f, 60.0f },           60.0f, dbText),
        fp (pid::compStereoLink, "Stereo Link", Range { 0.0f, 100.0f },         100.0f, pctText),
        fp (pid::compLookahead,  "Lookahead",   Range { 0.0f, 20.0f },            0.0f, msText),
        cp (pid::compDetector,    "Detection",    juce::StringArray { "Peak", "RMS", "Hybrid" }, 2),
        bp (pid::compAutoRelease, "Auto Release", false)));

    layout.add (std::make_unique<Group> ("curve", "Curve", "|",
        cp (pid::curveSyncMode, "Curve Sync",    juce::StringArray { "Tempo", "Free" }, 0),
        cp (pid::curveDivision, "Curve Rate",    divisionNames(), kDefaultDivision),
        cp (pid::curveTrigMode, "Curve Trigger", juce::StringArray { "Loop", "One Shot", "Audio", "MIDI" }, 0),
        fp (pid::curveRateHz,    "Curve Free Rate", skewed (0.01f, 50.0f, 2.0f),     2.0f, hzRateText),
        fp (pid::curveDepth,     "Curve Depth",     Range { 0.0f, 100.0f },        100.0f, pctText),
        fp (pid::curvePhase,     "Curve Phase",     Range { 0.0f, 1.0f },            0.0f, phaseText),
        fp (pid::curveSwing,     "Curve Swing",     Range { -50.0f, 50.0f },         0.0f, pctText),
        fp (pid::curveSmooth,    "Curve Smooth",    skewed (0.0f, 50.0f, 5.0f),      2.0f, msText),
        fp (pid::curvePreDelay,  "Pre Delay",       skewed (0.0f, 200.0f, 20.0f),    0.0f, msText),
        fp (pid::curvePostDelay, "Post Delay",      skewed (0.0f, 200.0f, 20.0f),    0.0f, msText),
        // Attack / Hold / Recover are rate limits on the curve's own output, so
        // 0 ms means "play the drawn curve exactly" and is the right default.
        fp (pid::curveAttack,    "Curve Attack",    skewed (0.0f, 200.0f, 10.0f),    0.0f, msText),
        fp (pid::curveHold,      "Curve Hold",      skewed (0.0f, 500.0f, 50.0f),    0.0f, msText),
        fp (pid::curveRecover,   "Curve Recover",   skewed (0.0f, 2000.0f, 200.0f),  0.0f, msText),
        std::make_unique<API> (makeID (pid::curveSnap), "Curve Snap", 1, 64, 16),
        bp (pid::curveSnapEnable, "Curve Snap On", true)));

    layout.add (std::make_unique<Group> ("trigger", "Trigger", "|",
        cp (pid::trigSource,   "Trig Source",    juce::StringArray { "Sidechain", "Internal", "MIDI", "Transport", "Manual" }, 0),
        cp (pid::trigDetector, "Trig Detection", juce::StringArray { "Peak", "RMS", "Transient", "Weighted" }, 0),
        fp (pid::trigThreshold,   "Trig Threshold",  Range { -60.0f, 0.0f },            -30.0f, dbText),
        fp (pid::trigHysteresis,  "Trig Hysteresis", Range { 0.0f, 24.0f },               3.0f, dbText),
        fp (pid::trigMinInterval, "Trig Min Gap",    skewed (0.0f, 500.0f, 50.0f),       20.0f, msText),
        fp (pid::trigHighPass,    "Trig High Pass",  skewed (20.0f, 2000.0f, 200.0f),    20.0f, hzText),
        fp (pid::trigLowPass,     "Trig Low Pass",   skewed (200.0f, 20000.0f, 2000.0f), 20000.0f, hzText),
        std::make_unique<API> (makeID (pid::trigMidiNote), "Trig MIDI Note", 0, 128, 36),
        bp (pid::trigAudition, "Trig Audition", false)));

    layout.add (std::make_unique<Group> ("spectral", "Spectral", "|",
        fp (pid::specDepth,      "Spec Depth",        Range { 0.0f, 48.0f },              12.0f, dbText),
        fp (pid::specLowFreq,    "Spec Low",          skewed (20.0f, 2000.0f, 200.0f),    60.0f, hzText),
        fp (pid::specHighFreq,   "Spec High",         skewed (500.0f, 20000.0f, 4000.0f), 16000.0f, hzText),
        fp (pid::specFreqSmooth, "Spec Freq Smooth",  Range { 0.0f, 100.0f },             35.0f, pctText),
        fp (pid::specTimeSmooth, "Spec Time Smooth",  Range { 0.0f, 100.0f },             40.0f, pctText),
        fp (pid::specAttack,     "Spec Attack",       skewed (0.5f, 200.0f, 20.0f),        5.0f, msText),
        fp (pid::specRelease,    "Spec Release",      skewed (5.0f, 1000.0f, 150.0f),     80.0f, msText),
        fp (pid::specBlend,      "Spec Blend",        Range { 0.0f, 100.0f },             70.0f, pctText),
        fp (pid::specPreserve,   "Preserve Body",     Range { 0.0f, 100.0f },             30.0f, pctText),
        fp (pid::specTonal,      "Tonal / Transient", Range { 0.0f, 100.0f },             50.0f, pctText),
        fp (pid::specGuard,      "Artifact Guard",    Range { 0.0f, 100.0f },             50.0f, pctText),
        fp (pid::specTilt,       "Collision Tilt",    Range { -6.0f, 6.0f },               0.0f, dbText)));

    layout.add (std::make_unique<Group> ("bands", "Bands", "|",
        cp (pid::bandMode,    "Band Mode",    juce::StringArray { "1 Band", "3 Bands" }, 0),
        cp (pid::channelMode, "Channel Mode", juce::StringArray { "Stereo", "Left / Right", "Mid / Side" }, 0),
        fp (pid::xover1,      "Crossover 1",  skewed (40.0f, 1000.0f, 200.0f),    180.0f, hzText),
        fp (pid::xover2,      "Crossover 2",  skewed (500.0f, 16000.0f, 3000.0f), 2500.0f, hzText),
        fp (pid::channelLink, "Channel Link", Range { 0.0f, 100.0f },             100.0f, pctText),
        fp (pid::monoLowFreq, "Mono Below",   skewed (20.0f, 300.0f, 120.0f),     120.0f, hzText),
        bp (pid::monoLowEnable, "Mono Low On", false),
        bp (pid::bandEnable[0], "Band 1 On", true),
        bp (pid::bandEnable[1], "Band 2 On", true),
        bp (pid::bandEnable[2], "Band 3 On", true),
        fp (pid::bandDepth[0],   "Band 1 Depth",   Range { 0.0f, 100.0f },        100.0f, pctText),
        fp (pid::bandDepth[1],   "Band 2 Depth",   Range { 0.0f, 100.0f },        100.0f, pctText),
        fp (pid::bandDepth[2],   "Band 3 Depth",   Range { 0.0f, 100.0f },        100.0f, pctText),
        fp (pid::bandAttack[0],  "Band 1 Attack",  skewed (0.0f, 100.0f, 5.0f),     0.5f, msText),
        fp (pid::bandAttack[1],  "Band 2 Attack",  skewed (0.0f, 100.0f, 5.0f),     0.5f, msText),
        fp (pid::bandAttack[2],  "Band 3 Attack",  skewed (0.0f, 100.0f, 5.0f),     0.5f, msText),
        fp (pid::bandRelease[0], "Band 1 Release", skewed (1.0f, 1000.0f, 80.0f),  20.0f, msText),
        fp (pid::bandRelease[1], "Band 2 Release", skewed (1.0f, 1000.0f, 80.0f),  20.0f, msText),
        fp (pid::bandRelease[2], "Band 3 Release", skewed (1.0f, 1000.0f, 80.0f),  20.0f, msText)));

    layout.add (std::make_unique<Group> ("visual", "Visualisation", "|",
        bp (pid::vizEnabled, "Visualiser On", true),
        bp (pid::vizReduced, "Reduced Animation", false)));

    return layout;
}

void ParamRefs::attach (juce::AudioProcessorValueTreeState& s)
{
    auto get = [&s] (const char* id) { return s.getRawParameterValue (id); };

    mode = get (pid::mode);                       quality = get (pid::quality);
    bypass = get (pid::bypass);                   mix = get (pid::mix);
    outputGain = get (pid::outputGain);           inputGain = get (pid::inputGain);
    autoGain = get (pid::autoGain);               monitor = get (pid::monitor);

    macroDepth = get (pid::macroDepth);           macroSeparation = get (pid::macroSeparation);
    macroTiming = get (pid::macroTiming);         macroCharacter = get (pid::macroCharacter);
    macrosEnabled = get (pid::macrosEnabled);

    scSource = get (pid::scSource);               scHighPass = get (pid::scHighPass);
    scLowPass = get (pid::scLowPass);             scAudition = get (pid::scAudition);
    scGain = get (pid::scGain);

    compThreshold = get (pid::compThreshold);     compRatio = get (pid::compRatio);
    compKnee = get (pid::compKnee);               compAttack = get (pid::compAttack);
    compHold = get (pid::compHold);               compRelease = get (pid::compRelease);
    compRange = get (pid::compRange);             compDetector = get (pid::compDetector);
    compAutoRelease = get (pid::compAutoRelease); compStereoLink = get (pid::compStereoLink);
    compLookahead = get (pid::compLookahead);

    curveSyncMode = get (pid::curveSyncMode);     curveDivision = get (pid::curveDivision);
    curveRateHz = get (pid::curveRateHz);         curveTrigMode = get (pid::curveTrigMode);
    curveDepth = get (pid::curveDepth);           curvePhase = get (pid::curvePhase);
    curveSwing = get (pid::curveSwing);           curveSmooth = get (pid::curveSmooth);
    curvePreDelay = get (pid::curvePreDelay);     curvePostDelay = get (pid::curvePostDelay);
    curveAttack = get (pid::curveAttack);         curveHold = get (pid::curveHold);
    curveRecover = get (pid::curveRecover);       curveSnap = get (pid::curveSnap);
    curveSnapEnable = get (pid::curveSnapEnable);

    trigSource = get (pid::trigSource);           trigDetector = get (pid::trigDetector);
    trigThreshold = get (pid::trigThreshold);     trigHysteresis = get (pid::trigHysteresis);
    trigMinInterval = get (pid::trigMinInterval); trigHighPass = get (pid::trigHighPass);
    trigLowPass = get (pid::trigLowPass);         trigAudition = get (pid::trigAudition);
    trigMidiNote = get (pid::trigMidiNote);

    specDepth = get (pid::specDepth);             specLowFreq = get (pid::specLowFreq);
    specHighFreq = get (pid::specHighFreq);       specFreqSmooth = get (pid::specFreqSmooth);
    specTimeSmooth = get (pid::specTimeSmooth);   specAttack = get (pid::specAttack);
    specRelease = get (pid::specRelease);         specBlend = get (pid::specBlend);
    specPreserve = get (pid::specPreserve);       specTonal = get (pid::specTonal);
    specGuard = get (pid::specGuard);             specTilt = get (pid::specTilt);

    bandMode = get (pid::bandMode);               xover1 = get (pid::xover1);
    xover2 = get (pid::xover2);                   channelMode = get (pid::channelMode);
    channelLink = get (pid::channelLink);         monoLowEnable = get (pid::monoLowEnable);
    monoLowFreq = get (pid::monoLowFreq);

    for (int i = 0; i < kNumBands; ++i)
    {
        bandEnable[i]  = get (pid::bandEnable[i]);
        bandDepth[i]   = get (pid::bandDepth[i]);
        bandAttack[i]  = get (pid::bandAttack[i]);
        bandRelease[i] = get (pid::bandRelease[i]);
    }

    vizEnabled = get (pid::vizEnabled);
    vizReduced = get (pid::vizReduced);
}
} // namespace sf
