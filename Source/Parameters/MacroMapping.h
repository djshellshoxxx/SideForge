#pragma once
#include "ParameterLayout.h"
#include "../DSP/CompressorEngine.h"
#include "../DSP/CurveEngine.h"
#include "../DSP/LatencyManager.h"
#include "../DSP/MultibandProcessor.h"
#include "../DSP/SpectralDucker.h"
#include "../DSP/TriggerEngine.h"

namespace sf
{
/**
    Everything the audio thread needs for one block, resolved from the parameters
    in a single pass.

    Building this is the only place where parameter values are interpreted, which
    keeps `processBlock` free of policy: it reads this struct and runs the engines.
*/
struct EngineSettings
{
    Mode     mode     = Mode::Compress;
    Quality  quality  = Quality::Studio;
    Monitor  monitor  = Monitor::Normal;
    ScSource scSource = ScSource::External;

    bool bypass       = false;
    bool autoGain     = false;
    bool scAudition   = false;
    bool trigAudition = false;
    bool vizEnabled   = true;
    bool vizReduced   = false;

    float mixUnit       = 1.0f;   // 0..1
    float inputGainDb   = 0.0f;
    float outputGainDb  = 0.0f;

    float scHighPassHz = 20.0f;
    float scLowPassHz  = 20000.0f;
    float scGainDb     = 0.0f;
    float scStereoLink = 100.0f;  // percent
    Detector scDetector = Detector::Hybrid;

    float lookaheadMs = 0.0f;

    // Which engines contribute this block.
    bool useCompressor  = true;
    bool useCurve       = false;
    bool spectralActive = false;

    /** Scales the broadband gain's departure from unity. Hybrid uses it to trade
        broadband ducking against spectral ducking. */
    float broadbandBlend = 1.0f;

    CompressorEngine::Settings   comp;
    CurveEngine::Settings        curve;
    TriggerEngine::Settings      trigger;
    SpectralDucker::Settings     spectral;
    MultibandProcessor::Settings bands;
};

/**
    Resolves the APVTS values — with the four macros applied on top — into an
    `EngineSettings`.

    The macros are deliberately *modulation*, not extra parameters: every one of
    them scales values the user can also reach directly, so nothing in the UI is a
    dead end and A/B recall stays exact.

      - **Depth** scales how much gain reduction every engine asks for.
      - **Separation** trades broadband ducking for spectral ducking and raises the
        preserve-body control, i.e. how surgically the two sources are separated.
      - **Timing** scales every attack and release time together.
      - **Character** moves from soft and transparent (wide knee, gentle ratio) to
        hard and aggressive (tight knee, steeper ratio, faster onset).
*/
EngineSettings buildEngineSettings (const ParamRefs& params);
} // namespace sf
