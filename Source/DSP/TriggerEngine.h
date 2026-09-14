#pragma once
#include "EnvelopeFollower.h"
#include "TransportInfo.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace sf
{
/** Sample offsets, within the current block, at which a trigger fired.
    Fixed capacity so nothing allocates on the audio thread; a block containing
    more than `kMax` triggers is already far denser than anything musical. */
struct TriggerEvents
{
    static constexpr int kMax = 64;

    void clear() noexcept { count = 0; }

    void add (int samplePosition) noexcept
    {
        if (count < kMax)
            positions[count++] = samplePosition;
    }

    int count = 0;
    int positions[kMax] {};
};

/**
    Decides *when* something happens. It never produces gain.

    The routing layer is deliberately a single switch over `TrigSource` fed by a
    signal the caller chooses, so adding a source later (a companion Sender plugin
    writing into a shared buffer, for example) means adding one case and one
    producer, not restructuring the engine.

    Real-time safe: filters and envelopes are sized in `prepare()`.
*/
class TriggerEngine
{
public:
    /** Division index used when the trigger source is the host transport. */
    static constexpr int kDefaultTransportDivision = 13;   // 1/4

    struct Settings
    {
        TrigSource source        = TrigSource::Sidechain;
        TrigDetect detect        = TrigDetect::Peak;
        float thresholdDb        = -30.0f;
        float hysteresisDb       = 3.0f;
        float minIntervalMs      = 20.0f;
        float highPassHz         = 20.0f;
        float lowPassHz          = 20000.0f;
        int   midiNote           = 36;    // 128 = any note
        int   transportDivisionIdx = kDefaultTransportDivision;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setSettings (const Settings& s);
    const Settings& getSettings() const noexcept { return settings; }

    /** Message thread: queues a manual trigger to fire at the start of the next block. */
    void fireManualTrigger() noexcept { manualPending.store (true, std::memory_order_release); }

    /**
        Produces the triggers for this block.

        `source` is the audio the caller decided should drive the trigger (the raw
        sidechain bus, or the main input). It is filtered and analysed here with
        the trigger's own filter, which is independent of the compressor's
        detector filter.
    */
    void process (const juce::AudioBuffer<float>& source, int startChannel, int numChannels,
                  int numSamples, const juce::MidiBuffer& midi,
                  const TransportInfo& transport, double quartersThisBlock,
                  TriggerEvents& events);

    /** Filtered trigger signal, for the trigger audition monitor. */
    const juce::AudioBuffer<float>& getAuditionSignal() const noexcept { return filtered; }

    /** Most recent detector level in dB, for the UI's threshold display. */
    float getLevelDb() const noexcept { return levelDb.load (std::memory_order_relaxed); }

    /** True while the detector is above the threshold (gate is open). */
    bool isArmed() const noexcept { return armed.load (std::memory_order_relaxed); }

private:
    void processAudioSource (int numSamples, TriggerEvents& events);
    void processMidiSource (const juce::MidiBuffer& midi, int numSamples, TriggerEvents& events);
    void processTransportSource (const TransportInfo& transport, double quartersThisBlock,
                                 int numSamples, TriggerEvents& events);
    float detectorLevel (float x) noexcept;

    static constexpr int kMaxCh = kMaxChannels;

    double sr = 44100.0;
    int preparedChannels = 0;
    Settings settings;

    juce::AudioBuffer<float> filtered;   // filtered, mono-summed trigger signal
    juce::dsp::StateVariableTPTFilter<float> highPass, lowPass;

    // Weighted detection: a fixed high-pass + high-shelf approximating the
    // K-weighting curve, so "Weighted" tracks perceived loudness rather than
    // raw level.
    juce::dsp::IIR::Filter<float> weightHighPass, weightShelf;

    float rmsState = 0.0f, fastState = 0.0f, slowState = 0.0f;
    float rmsCoeff = 0.0f, fastCoeff = 0.0f, slowCoeff = 0.0f;

    bool  gateOpen = false;
    int   samplesSinceTrigger = 0;
    int   minIntervalSamples = 0;

    double transportPhase = 0.0;   // 0..1 within the trigger division
    bool   transportPrimed = false;

    std::atomic<bool>  manualPending { false };
    std::atomic<bool>  armed { false };
    std::atomic<float> levelDb { -100.0f };
};
} // namespace sf
