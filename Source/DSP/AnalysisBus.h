#pragma once
#include "../Utilities/LockFreeFifo.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace sf
{
/** One decimated column of the scrolling visualiser. */
struct WaveformPoint
{
    float mainMin = 0.0f, mainMax = 0.0f;   // destination, post-input-gain
    float sideMin = 0.0f, sideMax = 0.0f;   // filtered sidechain
    float outMin  = 0.0f, outMax  = 0.0f;   // final output
    float gain    = 1.0f;                   // broadband gain applied, 0..1
    bool  trigger = false;                  // a trigger fired inside this column
};

/**
    The audio thread's only channel to the visualiser.

    It accumulates min/max over a fixed number of samples and pushes one small
    struct per column into a lock-free ring. Everything expensive — drawing, the
    display FFT, colour work — happens on the GUI thread from these columns, which
    is what keeps editor-open and editor-closed CPU use effectively identical.
*/
class AnalysisBus
{
public:
    /** ~800 columns per second: fine enough for a musical waveform at any window
        width, coarse enough that the ring never comes close to overflowing. */
    static constexpr double kColumnsPerSecond = 800.0;
    static constexpr size_t kCapacity = 8192;

    void prepare (double sampleRate) noexcept
    {
        samplesPerColumn = juce::jmax (1, (int) std::lround (sampleRate / kColumnsPerSecond));
        reset();
    }

    void reset() noexcept
    {
        fifo.clear();
        beginColumn();
        counter = 0;
    }

    /** Audio thread. One sample's worth of every signal the visualiser shows. */
    void pushSample (float main, float side, float out, float gain, bool triggered) noexcept
    {
        current.mainMin = juce::jmin (current.mainMin, main);
        current.mainMax = juce::jmax (current.mainMax, main);
        current.sideMin = juce::jmin (current.sideMin, side);
        current.sideMax = juce::jmax (current.sideMax, side);
        current.outMin  = juce::jmin (current.outMin,  out);
        current.outMax  = juce::jmax (current.outMax,  out);
        current.gain    = juce::jmin (current.gain, gain);
        current.trigger = current.trigger || triggered;

        if (++counter >= samplesPerColumn)
        {
            fifo.push (current);   // dropping a column beats stalling the audio thread
            beginColumn();
            counter = 0;
        }
    }

    /** Audio thread: keeps the timeline moving while the plugin is silent. */
    void pushSilence (int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            pushSample (0.0f, 0.0f, 0.0f, 1.0f, false);
    }

    /** GUI thread. */
    bool pop (WaveformPoint& out) noexcept { return fifo.pop (out); }
    void discardQueued() noexcept { fifo.clear(); }
    size_t getNumReady() const noexcept { return fifo.getNumReady(); }

    int getSamplesPerColumn() const noexcept { return samplesPerColumn; }

private:
    void beginColumn() noexcept
    {
        current = {};
        current.mainMin = current.sideMin = current.outMin = 0.0f;
        current.mainMax = current.sideMax = current.outMax = 0.0f;
        current.gain = 1.0f;
        current.trigger = false;
    }

    LockFreeFifo<WaveformPoint, kCapacity> fifo;
    WaveformPoint current;
    int samplesPerColumn = 60;
    int counter = 0;
};
} // namespace sf
