#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamIDs.h"

namespace sf
{
/** Builds the full APVTS parameter layout. Called once by the processor. */
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw parameter pointers so the audio thread never does a map lookup.
    All members are plain atomics owned by the APVTS. */
struct ParamRefs
{
    void attach (juce::AudioProcessorValueTreeState& s);

    std::atomic<float>* mode = nullptr;
    std::atomic<float>* quality = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* monitor = nullptr;

    std::atomic<float>* macroDepth = nullptr;
    std::atomic<float>* macroSeparation = nullptr;
    std::atomic<float>* macroTiming = nullptr;
    std::atomic<float>* macroCharacter = nullptr;
    std::atomic<float>* macrosEnabled = nullptr;

    std::atomic<float>* scSource = nullptr;
    std::atomic<float>* scHighPass = nullptr;
    std::atomic<float>* scLowPass = nullptr;
    std::atomic<float>* scAudition = nullptr;
    std::atomic<float>* scGain = nullptr;

    std::atomic<float>* compThreshold = nullptr;
    std::atomic<float>* compRatio = nullptr;
    std::atomic<float>* compKnee = nullptr;
    std::atomic<float>* compAttack = nullptr;
    std::atomic<float>* compHold = nullptr;
    std::atomic<float>* compRelease = nullptr;
    std::atomic<float>* compRange = nullptr;
    std::atomic<float>* compDetector = nullptr;
    std::atomic<float>* compAutoRelease = nullptr;
    std::atomic<float>* compStereoLink = nullptr;
    std::atomic<float>* compLookahead = nullptr;

    std::atomic<float>* curveSyncMode = nullptr;
    std::atomic<float>* curveDivision = nullptr;
    std::atomic<float>* curveRateHz = nullptr;
    std::atomic<float>* curveTrigMode = nullptr;
    std::atomic<float>* curveDepth = nullptr;
    std::atomic<float>* curvePhase = nullptr;
    std::atomic<float>* curveSwing = nullptr;
    std::atomic<float>* curveSmooth = nullptr;
    std::atomic<float>* curvePreDelay = nullptr;
    std::atomic<float>* curvePostDelay = nullptr;
    std::atomic<float>* curveAttack = nullptr;
    std::atomic<float>* curveHold = nullptr;
    std::atomic<float>* curveRecover = nullptr;
    std::atomic<float>* curveSnap = nullptr;
    std::atomic<float>* curveSnapEnable = nullptr;

    std::atomic<float>* trigSource = nullptr;
    std::atomic<float>* trigDetector = nullptr;
    std::atomic<float>* trigThreshold = nullptr;
    std::atomic<float>* trigHysteresis = nullptr;
    std::atomic<float>* trigMinInterval = nullptr;
    std::atomic<float>* trigHighPass = nullptr;
    std::atomic<float>* trigLowPass = nullptr;
    std::atomic<float>* trigAudition = nullptr;
    std::atomic<float>* trigMidiNote = nullptr;

    std::atomic<float>* specDepth = nullptr;
    std::atomic<float>* specLowFreq = nullptr;
    std::atomic<float>* specHighFreq = nullptr;
    std::atomic<float>* specFreqSmooth = nullptr;
    std::atomic<float>* specTimeSmooth = nullptr;
    std::atomic<float>* specAttack = nullptr;
    std::atomic<float>* specRelease = nullptr;
    std::atomic<float>* specBlend = nullptr;
    std::atomic<float>* specPreserve = nullptr;
    std::atomic<float>* specTonal = nullptr;
    std::atomic<float>* specGuard = nullptr;
    std::atomic<float>* specTilt = nullptr;

    std::atomic<float>* bandMode = nullptr;
    std::atomic<float>* xover1 = nullptr;
    std::atomic<float>* xover2 = nullptr;
    std::atomic<float>* channelMode = nullptr;
    std::atomic<float>* channelLink = nullptr;
    std::atomic<float>* monoLowEnable = nullptr;
    std::atomic<float>* monoLowFreq = nullptr;

    std::atomic<float>* bandEnable[kNumBands]  { nullptr, nullptr, nullptr };
    std::atomic<float>* bandDepth[kNumBands]   { nullptr, nullptr, nullptr };
    std::atomic<float>* bandAttack[kNumBands]  { nullptr, nullptr, nullptr };
    std::atomic<float>* bandRelease[kNumBands] { nullptr, nullptr, nullptr };

    std::atomic<float>* vizEnabled = nullptr;
    std::atomic<float>* vizReduced = nullptr;
};

// Convenience readers -------------------------------------------------------
inline bool  readBool  (std::atomic<float>* p) noexcept { return p != nullptr && p->load() >= 0.5f; }
inline float readFloat (std::atomic<float>* p) noexcept { return p != nullptr ? p->load() : 0.0f; }
inline int   readInt   (std::atomic<float>* p) noexcept { return p != nullptr ? (int) std::lround (p->load()) : 0; }
template <typename E>
inline E readEnum (std::atomic<float>* p) noexcept { return static_cast<E> (readInt (p)); }
} // namespace sf
