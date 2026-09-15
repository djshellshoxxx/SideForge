#pragma once
#include "ParameterLayout.h"
#include "../DSP/BandMatrix.h"

namespace sf
{
// =============================================================================
//  Band Matrix parameter identifiers.
//
//  THESE STRINGS ARE A PUBLIC CONTRACT, exactly like the ones in ParamIDs.h.
//  They are all new in state version 2; nothing existing was renamed or
//  repurposed, so a session saved before the Band Matrix existed simply has no
//  values for them and loads with the defaults — which put the plugin in Full
//  Range mode and leave its behaviour unchanged.
// =============================================================================
namespace pid
{
inline constexpr auto routingMode  = "routingMode";   // Full Range / Band Matrix
inline constexpr auto bmBandCount  = "bmBandCount";   // 1..4 bands
inline constexpr auto bmXover1     = "bmXover1";
inline constexpr auto bmXover2     = "bmXover2";
inline constexpr auto bmXover3     = "bmXover3";
inline constexpr auto bmPunch      = "bmPunch";       // matrix-only macro
inline constexpr auto bmRecovery   = "bmRecovery";    // matrix-only macro

/** Per-band IDs. Index 0..3 is Low, Low-Mid, Mid-High, High. */
struct BandIds
{
    const char* mode;
    const char* enable;
    const char* bypass;
    const char* solo;
    const char* mute;
    const char* depth;
    const char* range;
    const char* threshold;
    const char* ratio;
    const char* knee;
    const char* attack;
    const char* hold;
    const char* release;
    const char* autoRelease;
    const char* detector;
    const char* lookahead;
    const char* makeup;
    const char* gain;
    const char* pan;
    const char* mix;
    const char* scSource;
    const char* scFilter;
    const char* scHighPass;
    const char* scLowPass;
    const char* scListen;
};

inline constexpr BandIds band[kMaxMatrixBands] =
{
    { "bm1Mode", "bm1Enable", "bm1Bypass", "bm1Solo", "bm1Mute", "bm1Depth", "bm1Range",
      "bm1Threshold", "bm1Ratio", "bm1Knee", "bm1Attack", "bm1Hold", "bm1Release",
      "bm1AutoRelease", "bm1Detector", "bm1Lookahead", "bm1Makeup", "bm1Gain", "bm1Pan",
      "bm1Mix", "bm1ScSource", "bm1ScFilter", "bm1ScHighPass", "bm1ScLowPass", "bm1ScListen" },
    { "bm2Mode", "bm2Enable", "bm2Bypass", "bm2Solo", "bm2Mute", "bm2Depth", "bm2Range",
      "bm2Threshold", "bm2Ratio", "bm2Knee", "bm2Attack", "bm2Hold", "bm2Release",
      "bm2AutoRelease", "bm2Detector", "bm2Lookahead", "bm2Makeup", "bm2Gain", "bm2Pan",
      "bm2Mix", "bm2ScSource", "bm2ScFilter", "bm2ScHighPass", "bm2ScLowPass", "bm2ScListen" },
    { "bm3Mode", "bm3Enable", "bm3Bypass", "bm3Solo", "bm3Mute", "bm3Depth", "bm3Range",
      "bm3Threshold", "bm3Ratio", "bm3Knee", "bm3Attack", "bm3Hold", "bm3Release",
      "bm3AutoRelease", "bm3Detector", "bm3Lookahead", "bm3Makeup", "bm3Gain", "bm3Pan",
      "bm3Mix", "bm3ScSource", "bm3ScFilter", "bm3ScHighPass", "bm3ScLowPass", "bm3ScListen" },
    { "bm4Mode", "bm4Enable", "bm4Bypass", "bm4Solo", "bm4Mute", "bm4Depth", "bm4Range",
      "bm4Threshold", "bm4Ratio", "bm4Knee", "bm4Attack", "bm4Hold", "bm4Release",
      "bm4AutoRelease", "bm4Detector", "bm4Lookahead", "bm4Makeup", "bm4Gain", "bm4Pan",
      "bm4Mix", "bm4ScSource", "bm4ScFilter", "bm4ScHighPass", "bm4ScLowPass", "bm4ScListen" },
};
} // namespace pid

/** Which of the two top-level signal paths is running. */
enum class RoutingMode { FullRange = 0, BandMatrix };

/** Human-readable names for the choice parameters and the UI. */
juce::StringArray bandModeNames();
juce::StringArray bandScSourceNames();
juce::StringArray routingModeNames();
juce::StringArray bandCountNames();

/** The default display name for a band, given how many bands are active. */
juce::String defaultBandName (int bandIndex, int numBands);

/** Adds every Band Matrix parameter to the layout. */
void addBandMatrixParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/** Cached raw pointers, so the audio thread never does a map lookup. */
struct BandMatrixParamRefs
{
    void attach (juce::AudioProcessorValueTreeState& state);

    std::atomic<float>* routingMode = nullptr;
    std::atomic<float>* bandCount = nullptr;
    std::atomic<float>* crossover[kMaxCrossovers] { nullptr, nullptr, nullptr };
    std::atomic<float>* punch = nullptr;
    std::atomic<float>* recovery = nullptr;

    struct Band
    {
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* enable = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* depth = nullptr;
        std::atomic<float>* range = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* ratio = nullptr;
        std::atomic<float>* knee = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* hold = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* autoRelease = nullptr;
        std::atomic<float>* detector = nullptr;
        std::atomic<float>* lookahead = nullptr;
        std::atomic<float>* makeup = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* scSource = nullptr;
        std::atomic<float>* scFilter = nullptr;
        std::atomic<float>* scHighPass = nullptr;
        std::atomic<float>* scLowPass = nullptr;
        std::atomic<float>* scListen = nullptr;
    };

    Band bands[kMaxMatrixBands];
};

/**
    Resolves the parameters into a `BandMatrix::Settings`.

    `spectralTemplate` is the global Spectrum panel's configuration: a band in
    Spectral mode uses those settings and supplies only its own Depth, so the
    spectral engine is configured in one place rather than four.
*/
BandMatrix::Settings buildBandMatrixSettings (const BandMatrixParamRefs& refs,
                                              const SpectralDucker::Settings& spectralTemplate,
                                              int spectralFftOrder, int spectralOverlap,
                                              float depthScale, float timingScale);

/** Reads just the routing mode, which the processor needs before anything else. */
RoutingMode readRoutingMode (const BandMatrixParamRefs& refs);
} // namespace sf
