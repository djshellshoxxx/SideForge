#pragma once
#include "../Utilities/LockFreeFifo.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace sf
{
/**
    A rolling window of raw samples for the GUI to analyse.

    The Band Matrix display needs a spectrum, and visualisation FFTs must not run
    on the audio thread. So the audio thread does the cheapest possible thing —
    two float stores per sample into a power-of-two ring — and the GUI thread
    copies the newest window out and transforms it itself.

    The copy is deliberately unsynchronised. The writer laps the ring roughly
    every 170 ms at 48 kHz while the reader takes microseconds, so a torn read is
    both rare and harmless: the worst case is one frame of the display showing a
    seam. Paying for a lock here to protect a picture would be the wrong trade.
*/
class RawTap
{
public:
    static constexpr int kSize = 8192;   // power of two; ~170 ms at 48 kHz
    static constexpr int kMask = kSize - 1;

    /** Which signal to read back. */
    enum class Channel { Original = 0, Processed, Sidechain };

    void reset() noexcept
    {
        for (auto& channel : data)
            channel.fill (0.0f);

        writePos.store (0, std::memory_order_release);
    }

    /** Audio thread. Three stores per sample and nothing else. */
    void push (float original, float processed, float side) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        data[0][(size_t) w] = original;
        data[1][(size_t) w] = processed;
        data[2][(size_t) w] = side;
        writePos.store ((w + 1) & kMask, std::memory_order_release);
    }

    /** GUI thread: the newest `count` samples of one channel, oldest first. */
    void readLatest (Channel channel, float* out, int count) const noexcept
    {
        if (out == nullptr)
            return;

        count = juce::jlimit (0, kSize, count);
        const auto end = writePos.load (std::memory_order_acquire);
        const auto& source = data[(size_t) juce::jlimit (0, 2, (int) channel)];

        for (int i = 0; i < count; ++i)
            out[i] = source[(size_t) ((end - count + i + kSize * 2) & kMask)];
    }

private:
    std::array<std::array<float, kSize>, 3> data {};
    std::atomic<int> writePos { 0 };
};

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
        rawTap.reset();
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

    /** Raw samples for the GUI's own spectrum analysis. */
    RawTap& getRawTap() noexcept { return rawTap; }

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

    RawTap rawTap;
    LockFreeFifo<WaveformPoint, kCapacity> fifo;
    WaveformPoint current;
    int samplesPerColumn = 60;
    int counter = 0;
};
} // namespace sf
