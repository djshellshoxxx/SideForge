#pragma once
#include "../Model/CurveModel.h"
#include "../Parameters/Divisions.h"
#include "../Parameters/ParamIDs.h"
#include "TransportInfo.h"
#include "TriggerEngine.h"
#include <atomic>

namespace sf
{
/**
    Turns the drawn curve into a per-sample gain multiplier in `[0, 1]`.

    Like the compressor it emits gain rather than audio, so Hybrid mode can simply
    multiply the two.

    Timing is sample-accurate: in tempo-synced Loop mode the phase is re-anchored to
    the host's PPQ position at the start of every block and advanced per sample in
    between, so starting playback in the middle of a bar lands on the right part of
    the curve and a tempo change takes effect immediately without a discontinuity.

    Real-time safe: `process()` reads a `CurveTable` published by the message
    thread and does nothing but arithmetic.
*/
class CurveEngine
{
public:
    struct Settings
    {
        SyncMode syncMode      = SyncMode::Tempo;
        int      divisionIndex = kDefaultDivision;
        float    rateHz        = 2.0f;
        TrigMode trigMode      = TrigMode::Loop;
        float    depth         = 1.0f;    // 0..1
        float    phaseOffset   = 0.0f;    // 0..1 of a cycle
        float    swing         = 0.0f;    // -0.5..0.5
        float    smoothMs      = 2.0f;    // anti-click one-pole
        float    preDelayMs    = 0.0f;
        float    postDelayMs   = 0.0f;
        float    attackMs      = 0.0f;    // 0 = follow the curve exactly
        float    holdMs        = 0.0f;
        float    recoverMs     = 0.0f;    // 0 = follow the curve exactly
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setSettings (const Settings& s) noexcept;
    const Settings& getSettings() const noexcept { return settings; }

    /** Audio thread: the table to read for this block. Must outlive the call. */
    void setTable (const CurveTable* t) noexcept { table = t; }

    /** Writes `numSamples` of gain into `gainOut` (a single, channel-independent
        envelope). `quartersThisBlock` is how far the transport advances over the
        block, which the caller already knows from the tempo. */
    void process (int numSamples, const TransportInfo& transport, double quartersThisBlock,
                  const TriggerEvents& triggers, float* gainOut) noexcept;

    /** Length of one cycle in samples at the current settings. */
    double cycleLengthSamples (const TransportInfo& transport) const noexcept;

    // -- UI state (atomics; safe to read from the message thread) ------------
    float getPhase() const noexcept       { return uiPhase.load (std::memory_order_relaxed); }
    float getCurrentGain() const noexcept { return uiGain.load (std::memory_order_relaxed); }
    bool  isRunning() const noexcept      { return uiRunning.load (std::memory_order_relaxed); }

private:
    /** Applies swing by moving the boundary between each pair of cycles. */
    double applySwing (double phase) const noexcept;
    float  sampleCurve (double phase) const noexcept;
    void   startCycle() noexcept;

    double sr = 44100.0;
    Settings settings;
    const CurveTable* table = nullptr;

    // Phase state. `cyclePhase` is the position inside the current cycle and
    // `pairIndex` says whether this is the first or second cycle of a swing pair.
    double cyclePhase = 0.0;
    int    pairIndex  = 0;

    bool   oneShotActive  = false;
    int    preDelayCounter  = 0;
    int    postDelayCounter = 0;
    float  heldValue = 1.0f;

    // Shaping state.
    float smoothState = 1.0f;
    float shapedValue = 1.0f;
    float smoothCoeff = 0.0f;
    float attackStep  = 0.0f;   // max downward change per sample, 0 = unlimited
    float recoverStep = 0.0f;   // max upward change per sample, 0 = unlimited
    int   holdSamples = 0;
    int   holdCounter = 0;
    int   preDelaySamples = 0, postDelaySamples = 0;

    std::atomic<float> uiPhase { 0.0f };
    std::atomic<float> uiGain  { 1.0f };
    std::atomic<bool>  uiRunning { false };
};
} // namespace sf
