#pragma once
#include "TestFramework.h"
#include "PluginProcessor.h"
#include <cmath>
#include <memory>
#include <random>

namespace sft
{
inline constexpr double kPi = 3.14159265358979323846;

/** Creates a prepared processor with the requested layout.
    scChannels == 0 disables the sidechain bus entirely. */
inline std::unique_ptr<sf::SideForgeProcessor> makeProcessor (double sampleRate = 48000.0,
                                                              int blockSize = 512,
                                                              int mainChannels = 2,
                                                              int scChannels = 2)
{
    auto p = std::make_unique<sf::SideForgeProcessor>();

    const auto main = mainChannels == 1 ? juce::AudioChannelSet::mono()
                                        : juce::AudioChannelSet::stereo();
    const auto sc   = scChannels == 0 ? juce::AudioChannelSet::disabled()
                    : scChannels == 1 ? juce::AudioChannelSet::mono()
                                      : juce::AudioChannelSet::stereo();

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (main);
    layout.inputBuses.add (sc);
    layout.outputBuses.add (main);

    const auto applied = p->setBusesLayout (layout);
    SF_CHECK_MSG (applied, "host layout was rejected by the processor");

    // NOT setPlayConfigDetails: that helper calls disableNonMainBuses(), which
    // would quietly switch the sidechain back off and leave every sidechain test
    // running against a bus that is not there.
    p->setRateAndBufferSizeDetails (sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);

    SF_CHECK_MSG (p->getTotalNumInputChannels() == mainChannels + scChannels,
                  "the requested bus layout did not survive preparation");

    return p;
}

inline void fillSine (juce::AudioBuffer<float>& b, int channel, double freq,
                      double sampleRate, float amp = 0.5f, double phase = 0.0)
{
    auto* d = b.getWritePointer (channel);
    const auto inc = 2.0 * kPi * freq / sampleRate;

    for (int i = 0; i < b.getNumSamples(); ++i)
        d[i] = amp * (float) std::sin (phase + inc * i);
}

inline void fillNoise (juce::AudioBuffer<float>& b, int channel, float amp = 0.5f, unsigned seed = 1234)
{
    std::mt19937 rng { seed };
    std::uniform_real_distribution<float> dist { -amp, amp };
    auto* d = b.getWritePointer (channel);

    for (int i = 0; i < b.getNumSamples(); ++i)
        d[i] = dist (rng);
}

/** Repeating impulse bursts, e.g. a kick-like sidechain trigger. */
inline void fillPulses (juce::AudioBuffer<float>& b, int channel, int periodSamples,
                        int lengthSamples, float amp = 0.9f, int offset = 0)
{
    auto* d = b.getWritePointer (channel);

    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const auto pos = (i + offset) % juce::jmax (1, periodSamples);
        d[i] = pos < lengthSamples ? amp * (1.0f - (float) pos / (float) juce::jmax (1, lengthSamples)) : 0.0f;
    }
}

inline float maxAbs (const juce::AudioBuffer<float>& b, int channel)
{
    return b.getMagnitude (channel, 0, b.getNumSamples());
}

inline bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const auto* d = b.getReadPointer (ch);

        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (d[i]))
                return false;
    }

    return true;
}

inline void setParam (sf::SideForgeProcessor& p, const char* id, float value)
{
    auto* param = p.getAPVTS().getParameter (id);
    SF_CHECK_MSG (param != nullptr, std::string ("unknown parameter: ") + id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

inline float getParam (sf::SideForgeProcessor& p, const char* id)
{
    auto* v = p.getAPVTS().getRawParameterValue (id);
    SF_CHECK_MSG (v != nullptr, std::string ("unknown parameter: ") + id);
    return v->load();
}

inline float dbToGain (float db) { return juce::Decibels::decibelsToGain (db); }
inline float gainToDb (float g)  { return juce::Decibels::gainToDecibels (g, -120.0f); }
} // namespace sft
