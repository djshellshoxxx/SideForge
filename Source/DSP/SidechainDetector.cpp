#include "SidechainDetector.h"

namespace sf
{
void SidechainDetector::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr = sampleRate;
    preparedChannels = juce::jlimit (1, kMaxCh, numChannels);
    activeChannels = preparedChannels;

    filtered.setSize (preparedChannels, maxBlockSize, false, true, true);
    envelope.setSize (preparedChannels, maxBlockSize, false, true, true);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize,
                                        (juce::uint32) preparedChannels };

    highPass.prepare (spec);
    highPass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowPass.prepare (spec);
    lowPass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    gain.reset (sampleRate, 0.02);

    for (auto& f : followers)
        f.prepare (sampleRate);

    setFilters (hpHz, lpHz);
    setDetectorType (currentDetector);
    reset();
}

void SidechainDetector::reset()
{
    highPass.reset();
    lowPass.reset();
    filtered.clear();
    envelope.clear();
    gain.setCurrentAndTargetValue (gain.getTargetValue());

    for (auto& f : followers)
        f.reset();
}

void SidechainDetector::setFilters (float highPassHz, float lowPassHz)
{
    const auto nyquistLimit = (float) (sr * 0.49);
    hpHz = juce::jlimit (10.0f, nyquistLimit, highPassHz);
    lpHz = juce::jlimit (hpHz, nyquistLimit, lowPassHz);

    highPass.setCutoffFrequency (hpHz);
    lowPass.setCutoffFrequency (lpHz);
}

void SidechainDetector::setGainDb (float db)
{
    gain.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 36.0f, db)));
}

void SidechainDetector::setDetectorType (Detector detector)
{
    currentDetector = detector;

    for (auto& f : followers)
    {
        f.setDetector (detector);
        f.setTimes (0.0f, 0.0f, 0.0f);   // instantaneous: ballistics live downstream
        f.setAutoRelease (false);
    }
}

void SidechainDetector::process (const juce::AudioBuffer<float>& source, int startChannel,
                                 int numChannels, int numSamples)
{
    numSamples = juce::jmin (numSamples, filtered.getNumSamples());
    activeChannels = juce::jlimit (1, preparedChannels, numChannels);

    // --- copy the requested channels in, duplicating mono across the pair -----
    for (int ch = 0; ch < activeChannels; ++ch)
    {
        const auto srcChannel = startChannel + juce::jmin (ch, juce::jmax (0, numChannels - 1));

        if (numChannels > 0 && srcChannel < source.getNumChannels() && srcChannel >= 0)
            filtered.copyFrom (ch, 0, source, srcChannel, 0, numSamples);
        else
            filtered.clear (ch, 0, numSamples);
    }

    // --- gain, then band-limit ----------------------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = gain.getNextValue();

        for (int ch = 0; ch < activeChannels; ++ch)
            filtered.getWritePointer (ch)[i] *= g;
    }

    for (int ch = 0; ch < activeChannels; ++ch)
    {
        auto* d = filtered.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            auto v = sanitise (d[i]);
            v = highPass.processSample (ch, v);
            v = lowPass.processSample (ch, v);
            d[i] = flushDenormal (v);
        }
    }

    // --- envelopes, then stereo link ----------------------------------------
    for (int ch = 0; ch < activeChannels; ++ch)
    {
        const auto* src = filtered.getReadPointer (ch);
        auto* dst = envelope.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
            dst[i] = followers[ch].process (src[i]);
    }

    if (activeChannels > 1 && stereoLink > 0.0f)
    {
        auto* l = envelope.getWritePointer (0);
        auto* r = envelope.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto linked = juce::jmax (l[i], r[i]);
            l[i] += (linked - l[i]) * stereoLink;
            r[i] += (linked - r[i]) * stereoLink;
        }
    }

    // Channels beyond the active count mirror channel 0 so downstream code can
    // always read any prepared channel.
    for (int ch = activeChannels; ch < preparedChannels; ++ch)
    {
        envelope.copyFrom (ch, 0, envelope, 0, 0, numSamples);
        filtered.copyFrom (ch, 0, filtered, 0, 0, numSamples);
    }
}
} // namespace sf
