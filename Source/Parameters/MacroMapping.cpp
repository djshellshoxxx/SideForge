#include "MacroMapping.h"
#include <cmath>

namespace sf
{
namespace
{
/** Centre-detented multiplier: 50 % -> 1.0, 0 % -> 1/range, 100 % -> range. */
float bipolarScale (float percent, float range) noexcept
{
    const auto unit = (juce::jlimit (0.0f, 100.0f, percent) - 50.0f) / 50.0f;
    return std::pow (range, unit);
}

float unitOf (std::atomic<float>* p) noexcept
{
    return juce::jlimit (0.0f, 1.0f, readFloat (p) * 0.01f);
}
} // namespace

EngineSettings buildEngineSettings (const ParamRefs& params)
{
    EngineSettings s;

    // ---- macros -------------------------------------------------------------
    const auto macrosOn = readBool (params.macrosEnabled);

    const auto depthScale  = macrosOn ? bipolarScale (readFloat (params.macroDepth), 2.0f) : 1.0f;
    const auto timingScale = macrosOn ? bipolarScale (readFloat (params.macroTiming), 3.0f) : 1.0f;
    const auto separation  = macrosOn ? unitOf (params.macroSeparation) : -1.0f;  // < 0 = leave alone
    const auto character   = macrosOn ? unitOf (params.macroCharacter)  : -1.0f;

    // ---- global -------------------------------------------------------------
    s.mode     = readEnum<Mode>     (params.mode);
    s.quality  = readEnum<Quality>  (params.quality);
    s.monitor  = readEnum<Monitor>  (params.monitor);
    s.scSource = readEnum<ScSource> (params.scSource);

    s.bypass       = readBool (params.bypass);
    s.autoGain     = readBool (params.autoGain);
    s.scAudition   = readBool (params.scAudition);
    s.trigAudition = readBool (params.trigAudition);
    s.vizEnabled   = readBool (params.vizEnabled);
    s.vizReduced   = readBool (params.vizReduced);

    s.mixUnit      = unitOf (params.mix);
    s.inputGainDb  = readFloat (params.inputGain);
    s.outputGainDb = readFloat (params.outputGain);

    // ---- sidechain detector -------------------------------------------------
    s.scHighPassHz = readFloat (params.scHighPass);
    s.scLowPassHz  = readFloat (params.scLowPass);
    s.scGainDb     = readFloat (params.scGain);
    s.scStereoLink = readFloat (params.compStereoLink);
    s.scDetector   = readEnum<Detector> (params.compDetector);
    s.lookaheadMs  = readFloat (params.compLookahead);

    // ---- compressor ---------------------------------------------------------
    s.comp.thresholdDb = readFloat (params.compThreshold);
    s.comp.ratio       = readFloat (params.compRatio);
    s.comp.kneeDb      = readFloat (params.compKnee);
    s.comp.attackMs    = readFloat (params.compAttack)  * timingScale;
    s.comp.holdMs      = readFloat (params.compHold)    * timingScale;
    s.comp.releaseMs   = readFloat (params.compRelease) * timingScale;
    s.comp.rangeDb     = readFloat (params.compRange)   * depthScale;
    s.comp.autoRelease = readBool  (params.compAutoRelease);

    if (character >= 0.0f)
    {
        // Soft end: wider knee and a gentler ratio. Hard end: tight knee, steeper
        // ratio, and a faster onset.
        const auto softness = 1.0f - character;
        s.comp.kneeDb   = juce::jlimit (0.0f, 24.0f, s.comp.kneeDb * (0.25f + 1.75f * softness));
        s.comp.ratio    = juce::jmax (1.0f, s.comp.ratio * (0.6f + 0.8f * character));
        s.comp.attackMs = juce::jmax (0.0f, s.comp.attackMs * (0.4f + 1.2f * softness));
    }

    s.comp.rangeDb = juce::jlimit (0.0f, 80.0f, s.comp.rangeDb);

    // ---- curve --------------------------------------------------------------
    s.curve.syncMode      = readEnum<SyncMode> (params.curveSyncMode);
    s.curve.divisionIndex = readInt (params.curveDivision);
    s.curve.rateHz        = readFloat (params.curveRateHz);
    s.curve.trigMode      = readEnum<TrigMode> (params.curveTrigMode);
    s.curve.depth         = juce::jlimit (0.0f, 1.0f, unitOf (params.curveDepth) * depthScale);
    s.curve.phaseOffset   = readFloat (params.curvePhase);
    s.curve.swing         = readFloat (params.curveSwing) * 0.01f;
    s.curve.smoothMs      = readFloat (params.curveSmooth)   * timingScale;
    s.curve.preDelayMs    = readFloat (params.curvePreDelay);
    s.curve.postDelayMs   = readFloat (params.curvePostDelay);
    s.curve.attackMs      = readFloat (params.curveAttack)   * timingScale;
    s.curve.holdMs        = readFloat (params.curveHold)     * timingScale;
    s.curve.recoverMs     = readFloat (params.curveRecover)  * timingScale;

    // ---- trigger ------------------------------------------------------------
    s.trigger.source         = readEnum<TrigSource> (params.trigSource);
    s.trigger.detect         = readEnum<TrigDetect> (params.trigDetector);
    s.trigger.thresholdDb    = readFloat (params.trigThreshold);
    s.trigger.hysteresisDb   = readFloat (params.trigHysteresis);
    s.trigger.minIntervalMs  = readFloat (params.trigMinInterval);
    s.trigger.highPassHz     = readFloat (params.trigHighPass);
    s.trigger.lowPassHz      = readFloat (params.trigLowPass);
    s.trigger.midiNote       = readInt   (params.trigMidiNote);
    s.trigger.transportDivisionIdx = s.curve.divisionIndex;

    // MIDI curve mode only makes sense with MIDI triggers, so the routing follows
    // the mode rather than letting the two controls contradict each other.
    if (s.curve.trigMode == TrigMode::Midi)
        s.trigger.source = TrigSource::Midi;

    // ---- spectral -----------------------------------------------------------
    s.spectral.depthDb      = readFloat (params.specDepth) * depthScale;
    s.spectral.lowFreqHz    = readFloat (params.specLowFreq);
    s.spectral.highFreqHz   = readFloat (params.specHighFreq);
    s.spectral.freqSmooth   = unitOf (params.specFreqSmooth);
    s.spectral.timeSmooth   = unitOf (params.specTimeSmooth);
    s.spectral.attackMs     = readFloat (params.specAttack)  * timingScale;
    s.spectral.releaseMs    = readFloat (params.specRelease) * timingScale;
    s.spectral.preserveBody = unitOf (params.specPreserve);
    s.spectral.tonal        = unitOf (params.specTonal);
    s.spectral.guard        = unitOf (params.specGuard);
    s.spectral.tiltDbPerOct = readFloat (params.specTilt);
    s.spectral.depthDb      = juce::jlimit (0.0f, 60.0f, s.spectral.depthDb);

    auto blend = unitOf (params.specBlend);

    if (separation >= 0.0f)
    {
        // Separation pushes the work from the broadband stage into the spectral
        // stage and asks the collision map to be more selective as it does so.
        blend = juce::jlimit (0.0f, 1.0f, blend * (0.5f + separation));
        s.spectral.preserveBody = juce::jlimit (0.0f, 1.0f,
                                                s.spectral.preserveBody + (separation - 0.5f) * 0.5f);
    }

    // ---- bands --------------------------------------------------------------
    s.bands.bandMode      = readEnum<BandMode>    (params.bandMode);
    s.bands.channelMode   = readEnum<ChannelMode> (params.channelMode);
    s.bands.xover1Hz      = readFloat (params.xover1);
    s.bands.xover2Hz      = readFloat (params.xover2);
    s.bands.channelLink   = unitOf (params.channelLink);
    s.bands.monoLowEnable = readBool (params.monoLowEnable);
    s.bands.monoLowHz     = readFloat (params.monoLowFreq);

    for (int i = 0; i < kNumBands; ++i)
    {
        s.bands.bands[i].enabled   = readBool (params.bandEnable[i]);
        s.bands.bands[i].depth     = juce::jlimit (0.0f, 1.0f, unitOf (params.bandDepth[i]) * depthScale);
        s.bands.bands[i].attackMs  = readFloat (params.bandAttack[i])  * timingScale;
        s.bands.bands[i].releaseMs = readFloat (params.bandRelease[i]) * timingScale;
    }

    // ---- how the modes combine the engines ----------------------------------
    const auto cfg = qualityConfig (s.quality);

    switch (s.mode)
    {
        case Mode::Curve:
            s.useCompressor = false;
            s.useCurve = true;
            s.spectralActive = false;
            s.broadbandBlend = 1.0f;
            break;

        case Mode::Spectral:
            s.useCompressor = false;
            s.useCurve = false;
            s.spectralActive = cfg.spectralAvailable;
            s.broadbandBlend = 1.0f;
            s.spectral.amount = 1.0f;
            break;

        case Mode::Hybrid:
            s.useCompressor = true;
            s.useCurve = true;
            s.spectralActive = cfg.spectralAvailable;
            // The blend divides the work: what the spectral stage takes on, the
            // broadband stage gives up, so total ducking stays roughly constant.
            s.broadbandBlend = s.spectralActive ? 1.0f - blend : 1.0f;
            s.spectral.amount = s.spectralActive ? blend : 0.0f;
            break;

        case Mode::Compress:
        default:
            s.useCompressor = true;
            s.useCurve = false;
            s.spectralActive = false;
            s.broadbandBlend = 1.0f;
            break;
    }

    // Live quality has no spectral stage at all, so a session restored with
    // Spectral or Hybrid selected falls back to the broadband path rather than
    // silently doing nothing.
    if (! cfg.spectralAvailable && s.mode == Mode::Spectral)
    {
        s.useCompressor = true;
        s.broadbandBlend = 1.0f;
    }

    s.lookaheadMs = juce::jlimit (0.0f, cfg.maxLookaheadMs, s.lookaheadMs);

    return s;
}
} // namespace sf
