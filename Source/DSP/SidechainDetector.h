#pragma once
#include <juce_dsp/juce_dsp.h>
#include "EnvelopeFollower.h"

namespace sf
{
/**
    Turns whatever signal is feeding the sidechain into a per-sample detector
    envelope, and keeps a filtered copy of that signal around for audition.

    Real-time safe: all buffers and filter states are sized in `prepare()`.
*/
class SidechainDetector
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setFilters (float highPassHz, float lowPassHz);
    void setGainDb (float db);
    /** Selects Peak / RMS / Hybrid magnitude extraction.

        The detector deliberately has no attack, hold or release of its own: it
        reports a fast magnitude and the engines downstream own the ballistics, so
        that "Attack" on the compressor means attack of the gain reduction. */
    void setDetectorType (Detector detector);
    /** 0 = fully independent channels, 100 = fully linked. */
    void setStereoLink (float percent) noexcept { stereoLink = juce::jlimit (0.0f, 1.0f, percent * 0.01f); }

    /** Filters `numChannels` channels starting at `startChannel` of `source`,
        then writes the (link-blended) envelope for each channel.
        Pass a null/empty source to feed silence. */
    void process (const juce::AudioBuffer<float>& source, int startChannel,
                  int numChannels, int numSamples);

    /** Filtered sidechain signal, for audition and metering. */
    const juce::AudioBuffer<float>& getFiltered() const noexcept { return filtered; }

    /** Per-sample detector envelope for every channel, as a buffer. */
    const juce::AudioBuffer<float>& getEnvelopeBuffer() const noexcept { return envelope; }

    /** Per-sample detector envelope (linear magnitude) for a channel. */
    const float* getEnvelope (int channel) const noexcept
    {
        return envelope.getReadPointer (juce::jlimit (0, envelope.getNumChannels() - 1, channel));
    }

    int getNumChannels() const noexcept { return activeChannels; }

private:
    static constexpr int kMaxCh = kMaxChannels;

    double sr = 44100.0;
    int    preparedChannels = 0;
    int    activeChannels = 0;

    juce::AudioBuffer<float> filtered;
    juce::AudioBuffer<float> envelope;

    juce::dsp::StateVariableTPTFilter<float> highPass, lowPass;
    juce::SmoothedValue<float> gain;
    EnvelopeFollower followers[kMaxCh];

    float stereoLink = 1.0f;
    float hpHz = 20.0f, lpHz = 20000.0f;
    Detector currentDetector = Detector::Peak;
};
} // namespace sf
