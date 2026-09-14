#pragma once
#include <juce_dsp/juce_dsp.h>

namespace sf
{
/**
    Two-way Linkwitz-Riley 4th-order crossover for up to `MaxChannels` channels.

    LR4 is used because its low and high outputs sum to an allpass: recombining the
    bands with no processing gives back the input with flat magnitude, which is
    exactly the property the band stage needs (and which the crossover tests check
    to within a fraction of a dB).
*/
template <int MaxChannels>
class TwoWayCrossover
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        filter.prepare (spec);
        filter.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        reset();
    }

    void reset() { filter.reset(); }

    void setCutoff (float hz, double sampleRate)
    {
        const auto limit = (float) (sampleRate * 0.49);
        const auto clamped = juce::jlimit (20.0f, juce::jmax (21.0f, limit), hz);

        if (clamped != cutoff)
        {
            cutoff = clamped;
            filter.setCutoffFrequency (cutoff);
        }
    }

    float getCutoff() const noexcept { return cutoff; }

    /** Splits one sample into its low and high parts. */
    void split (int channel, float in, float& low, float& high) noexcept
    {
        filter.processSample (channel, in, low, high);
    }

private:
    juce::dsp::LinkwitzRileyFilter<float> filter;
    float cutoff = 1000.0f;
};

/**
    The allpass that an LR4 crossover is equivalent to when its two halves are
    summed. The low band of a three-way split must pass through this so that it
    stays phase-aligned with the two bands that went through the second crossover.
*/
template <int MaxChannels>
class CrossoverAllpass
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        filter.prepare (spec);
        filter.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        reset();
    }

    void reset() { filter.reset(); }

    void setCutoff (float hz, double sampleRate)
    {
        const auto limit = (float) (sampleRate * 0.49);
        const auto clamped = juce::jlimit (20.0f, juce::jmax (21.0f, limit), hz);

        if (clamped != cutoff)
        {
            cutoff = clamped;
            filter.setCutoffFrequency (cutoff);
        }
    }

    float process (int channel, float in) noexcept
    {
        return filter.processSample (channel, in);
    }

private:
    juce::dsp::LinkwitzRileyFilter<float> filter;
    float cutoff = 1000.0f;
};
} // namespace sf
