#pragma once

// =============================================================================
//  SideForge parameter identifiers.
//
//  THESE STRINGS ARE A PUBLIC CONTRACT. Host automation lanes and saved sessions
//  reference them by name. Never rename an existing ID: add a new one and handle
//  the old one in PresetState::migrate().
// =============================================================================

namespace sf::pid
{
// ---- global -----------------------------------------------------------------
inline constexpr auto mode            = "mode";             // Compress/Curve/Spectral/Hybrid
inline constexpr auto quality         = "quality";          // Live/Studio/Spectral/Render
inline constexpr auto bypass          = "bypass";
inline constexpr auto mix             = "mix";              // 0..100 %
inline constexpr auto outputGain      = "outputGain";       // dB
inline constexpr auto inputGain       = "inputGain";        // dB
inline constexpr auto autoGain        = "autoGain";
inline constexpr auto monitor         = "monitor";          // Normal/SC listen/Delta

// ---- macros -----------------------------------------------------------------
inline constexpr auto macroDepth      = "macroDepth";
inline constexpr auto macroSeparation = "macroSeparation";
inline constexpr auto macroTiming     = "macroTiming";
inline constexpr auto macroCharacter  = "macroCharacter";
inline constexpr auto macrosEnabled   = "macrosEnabled";

// ---- sidechain routing / detector -------------------------------------------
inline constexpr auto scSource        = "scSource";         // External/Internal
inline constexpr auto scHighPass      = "scHighPass";       // Hz
inline constexpr auto scLowPass       = "scLowPass";        // Hz
inline constexpr auto scAudition      = "scAudition";
inline constexpr auto scGain          = "scGain";           // dB

// ---- compressor -------------------------------------------------------------
inline constexpr auto compThreshold   = "compThreshold";    // dB
inline constexpr auto compRatio       = "compRatio";
inline constexpr auto compKnee        = "compKnee";         // dB
inline constexpr auto compAttack      = "compAttack";       // ms
inline constexpr auto compHold        = "compHold";         // ms
inline constexpr auto compRelease     = "compRelease";      // ms
inline constexpr auto compRange       = "compRange";        // dB (max GR)
inline constexpr auto compDetector    = "compDetector";     // Peak/RMS/Hybrid
inline constexpr auto compAutoRelease = "compAutoRelease";
inline constexpr auto compStereoLink  = "compStereoLink";   // 0..100 %
inline constexpr auto compLookahead   = "compLookahead";    // ms

// ---- curve engine -----------------------------------------------------------
inline constexpr auto curveSyncMode   = "curveSyncMode";    // Tempo/Free
inline constexpr auto curveDivision   = "curveDivision";    // index into division table
inline constexpr auto curveRateHz     = "curveRateHz";      // free-run rate
inline constexpr auto curveTrigMode   = "curveTrigMode";    // Loop/OneShot/Audio/MIDI
inline constexpr auto curveDepth      = "curveDepth";       // 0..100 %
inline constexpr auto curvePhase      = "curvePhase";       // 0..1
inline constexpr auto curveSwing      = "curveSwing";       // -50..50 %
inline constexpr auto curveSmooth     = "curveSmooth";      // ms (anti-click)
inline constexpr auto curvePreDelay   = "curvePreDelay";    // ms
inline constexpr auto curvePostDelay  = "curvePostDelay";   // ms
inline constexpr auto curveAttack     = "curveAttack";      // ms
inline constexpr auto curveHold       = "curveHold";        // ms
inline constexpr auto curveRecover    = "curveRecover";     // ms
inline constexpr auto curveSnap       = "curveSnap";        // grid divisions
inline constexpr auto curveSnapEnable = "curveSnapEnable";

// ---- trigger ----------------------------------------------------------------
inline constexpr auto trigSource      = "trigSource";       // SC/Internal/MIDI/Transport/Manual
inline constexpr auto trigDetector    = "trigDetector";     // Peak/RMS/Transient/Weighted
inline constexpr auto trigThreshold   = "trigThreshold";    // dB
inline constexpr auto trigHysteresis  = "trigHysteresis";   // dB
inline constexpr auto trigMinInterval = "trigMinInterval";  // ms
inline constexpr auto trigHighPass    = "trigHighPass";     // Hz
inline constexpr auto trigLowPass     = "trigLowPass";      // Hz
inline constexpr auto trigAudition    = "trigAudition";
inline constexpr auto trigMidiNote    = "trigMidiNote";     // 0..127, 128 = any

// ---- spectral ---------------------------------------------------------------
inline constexpr auto specDepth       = "specDepth";        // dB max attenuation
inline constexpr auto specLowFreq     = "specLowFreq";      // Hz
inline constexpr auto specHighFreq    = "specHighFreq";     // Hz
inline constexpr auto specFreqSmooth  = "specFreqSmooth";   // 0..100 %
inline constexpr auto specTimeSmooth  = "specTimeSmooth";   // 0..100 %
inline constexpr auto specAttack      = "specAttack";       // ms
inline constexpr auto specRelease     = "specRelease";      // ms
inline constexpr auto specBlend       = "specBlend";        // 0 = broadband .. 1 = spectral
inline constexpr auto specPreserve    = "specPreserve";     // preserve body 0..100 %
inline constexpr auto specTonal       = "specTonal";        // tonal(0) .. transient(1)
inline constexpr auto specGuard       = "specGuard";        // artifact protection 0..100 %
inline constexpr auto specTilt        = "specTilt";         // collision tilt dB/oct

// ---- multiband / channel ----------------------------------------------------
inline constexpr auto bandMode        = "bandMode";         // 1 band / 3 band
inline constexpr auto xover1          = "xover1";           // Hz
inline constexpr auto xover2          = "xover2";           // Hz
inline constexpr auto channelMode     = "channelMode";      // Stereo/LR/MS
inline constexpr auto channelLink     = "channelLink";      // 0..100 %
inline constexpr auto monoLowEnable   = "monoLowEnable";
inline constexpr auto monoLowFreq     = "monoLowFreq";      // Hz

// per band (index 0..2)
inline constexpr const char* bandEnable[3]  = { "band1Enable",  "band2Enable",  "band3Enable"  };
inline constexpr const char* bandDepth[3]   = { "band1Depth",   "band2Depth",   "band3Depth"   };
inline constexpr const char* bandAttack[3]  = { "band1Attack",  "band2Attack",  "band3Attack"  };
inline constexpr const char* bandRelease[3] = { "band1Release", "band2Release", "band3Release" };

// ---- visual / non-DSP -------------------------------------------------------
inline constexpr auto vizEnabled      = "vizEnabled";
inline constexpr auto vizReduced      = "vizReduced";
} // namespace sf::pid

namespace sf
{
enum class Mode        { Compress = 0, Curve, Spectral, Hybrid };
enum class Quality     { Live = 0, Studio, Spectral, Render };
enum class Monitor     { Normal = 0, SidechainListen, Delta };
enum class ScSource    { External = 0, Internal };
enum class Detector    { Peak = 0, Rms, Hybrid };
enum class TrigSource  { Sidechain = 0, Internal, Midi, Transport, Manual };
enum class TrigDetect  { Peak = 0, Rms, Transient, Weighted };
enum class SyncMode    { Tempo = 0, Free };
enum class TrigMode    { Loop = 0, OneShot, Audio, Midi };
enum class BandMode    { Single = 0, Three };
enum class ChannelMode { Stereo = 0, LeftRight, MidSide };

inline constexpr int kNumBands = 3;
inline constexpr int kMaxChannels = 2;
} // namespace sf
