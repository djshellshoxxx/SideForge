#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace sf
{
/**
    Fixed integer-sample delay for a small number of channels.

    Used for the lookahead path and for delaying the dry signal so that mix and
    bypass stay phase-coherent with the processed path. The whole worst-case buffer
    is allocated in `prepare()`; changing the delay afterwards only moves a read
    index, so it is safe from the audio thread.
*/
class DelayLine
{
public:
    void prepare (int numChannels, int maxDelaySamples)
    {
        channels = juce::jmax (1, numChannels);
        capacity = juce::jmax (1, maxDelaySamples) + 1;
        buffer.setSize (channels, capacity, false, true, true);
        reset();
    }

    void reset()
    {
        buffer.clear();
        writePos = 0;
    }

    /** Clamped to the capacity chosen in `prepare()`. Does not allocate. */
    void setDelay (int samples) noexcept
    {
        const auto clamped = juce::jlimit (0, capacity - 1, samples);

        if (clamped != delaySamples)
        {
            delaySamples = clamped;
            // Old content is now at the wrong distance behind the write head; the
            // cheapest correct answer is to fade in from silence rather than emit
            // whatever happens to sit at the new read offset.
            buffer.clear();
        }
    }

    int getDelay() const noexcept { return delaySamples; }
    int getCapacity() const noexcept { return capacity - 1; }

    /** Delays `block` in place. `block` may have fewer channels than prepared. */
    void process (juce::AudioBuffer<float>& block, int numSamples) noexcept
    {
        const auto numCh = juce::jmin (channels, block.getNumChannels());

        if (delaySamples == 0)
            return;

        auto w = writePos;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto r = (w + capacity - delaySamples) % capacity;

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* line = buffer.getWritePointer (ch);
                auto* data = block.getWritePointer (ch);
                const auto in = data[i];
                data[i] = line[r];
                line[w] = in;
            }

            w = (w + 1) % capacity;
        }

        writePos = w;
    }

    /** Pushes `numSamples` of silence through, keeping the line aligned when a
        caller skips processing for a block. */
    void advanceSilently (int numSamples) noexcept
    {
        if (delaySamples == 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
                buffer.getWritePointer (ch)[writePos] = 0.0f;

            writePos = (writePos + 1) % capacity;
        }
    }

private:
    juce::AudioBuffer<float> buffer;
    int channels = 0;
    int capacity = 1;
    int delaySamples = 0;
    int writePos = 0;
};
} // namespace sf
