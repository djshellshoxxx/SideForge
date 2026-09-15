#pragma once
#include "Crossover.h"
#include "EnvelopeFollower.h"
#include <array>

namespace sf
{
/** The most bands the Band Matrix supports, and therefore the most crossovers. */
inline constexpr int kMaxMatrixBands = 4;
inline constexpr int kMaxCrossovers  = kMaxMatrixBands - 1;

/**
    An N-way Linkwitz-Riley 4th-order splitter, for 1 to 4 bands.

    The split is a cascade — each crossover divides the remainder above the last
    one — so a band that skipped a crossover is phase-shifted differently from
    one that went through it. Every lower band therefore passes through the
    **allpass** equivalent of each crossover it did not cross:

        x ─LR1─┬─ low ──AP2──AP3──> band 0
               └─ high ─LR2─┬─ low ──AP3──> band 1
                            └─ high ─LR3─┬─ low ──> band 2
                                         └─ high ─> band 3

    With that correction the bands sum back to the input with flat magnitude,
    which is the property the whole Band Matrix depends on: switching into it
    with every band unprocessed must not change the tone.

    Crossover frequencies are **rate limited** rather than filtered. Dragging a
    handle retargets the cutoff and it glides there at a fixed number of octaves
    per second, so however far the target jumps, no single block can move the
    filter by more than a bounded ratio — which is the property that actually
    keeps a drag from clicking, and the one the tests can pin down. A one-pole
    would move fastest exactly when the jump is largest, which is backwards.
    Nothing here allocates after `prepare()`.
*/
template <int MaxChannels>
class LinkwitzRileyCrossover
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        const juce::dsp::ProcessSpec spec { sr,
                                            (juce::uint32) juce::jmax (1, maxBlockSize),
                                            (juce::uint32) juce::jlimit (1, MaxChannels, numChannels) };

        for (auto& split : splits)
            split.prepare (spec);

        for (auto& row : allpasses)
            for (auto& allpass : row)
                allpass.prepare (spec);

        juce::ignoreUnused (maxBlockSize);

        for (int i = 0; i < kMaxCrossovers; ++i)
            currentHz[i] = targetHz[i];

        applyCutoffs();
        reset();
    }

    void reset()
    {
        for (auto& split : splits)
            split.reset();

        for (auto& row : allpasses)
            for (auto& allpass : row)
                allpass.reset();
    }

    void setNumBands (int bands) noexcept
    {
        numBands = juce::jlimit (1, kMaxMatrixBands, bands);
    }

    int getNumBands() const noexcept { return numBands; }

    /** Sets the crossover targets. They are ordered and spaced here, so the UI
        cannot produce an inverted or degenerate split however it is driven. */
    void setCrossovers (const float* hz, int count) noexcept
    {
        const auto nyquistLimit = (float) (sr * 0.45);
        auto previous = kMinCrossoverHz;

        for (int i = 0; i < kMaxCrossovers; ++i)
        {
            auto wanted = (i < count && hz != nullptr) ? hz[i] : defaultCrossover (i);

            if (! std::isfinite (wanted))
                wanted = defaultCrossover (i);

            // Ordered, and never closer together than a useful fraction of an
            // octave, so a band can never collapse to nothing.
            wanted = juce::jlimit (previous * kMinSpacing, nyquistLimit, wanted);
            targetHz[i] = wanted;
            previous = wanted;
        }
    }

    /** Advances the glide by one block of `numSamples` and pushes the result to
        the filters. Call once per block, before `process`. */
    void updateSmoothing (int numSamples) noexcept
    {
        // The step is measured in octaves so a glide sounds like a constant
        // sweep rather than dashing through the low end, and it is scaled by the
        // block's duration so the glide takes the same wall-clock time whatever
        // block size the host hands us.
        const auto maxStep = (float) (kOctavesPerSecond * (double) juce::jmax (1, numSamples) / sr);

        auto changed = false;

        for (int i = 0; i < kMaxCrossovers; ++i)
        {
            if (currentHz[i] == targetHz[i])
                continue;

            const auto from = std::log2 (juce::jmax (1.0f, currentHz[i]));
            const auto to   = std::log2 (juce::jmax (1.0f, targetHz[i]));
            const auto delta = juce::jlimit (-maxStep, maxStep, to - from);

            currentHz[i] = (std::abs (to - from) <= maxStep) ? targetHz[i]
                                                             : std::exp2 (from + delta);
            changed = true;
        }

        if (changed)
            applyCutoffs();
    }

    /** True while a crossover is still gliding towards its target. */
    bool isSmoothing() const noexcept
    {
        for (int i = 0; i < kMaxCrossovers; ++i)
            if (currentHz[i] != targetHz[i])
                return true;

        return false;
    }

    /** The largest ratio a single block of `numSamples` can move a cutoff by.
        Bounded by construction, which is what makes "no clicks" testable. */
    float maxRatioPerBlock (int numSamples) const noexcept
    {
        return std::exp2 ((float) (kOctavesPerSecond * (double) juce::jmax (1, numSamples) / sr));
    }

    float getCrossoverHz (int index) const noexcept
    {
        return currentHz[juce::jlimit (0, kMaxCrossovers - 1, index)];
    }

    /** Splits `input` into `bands[0 .. numBands-1]`. Bands above that are cleared
        so a stale buffer can never leak into the sum. */
    void process (const juce::AudioBuffer<float>& input,
                  std::array<juce::AudioBuffer<float>, kMaxMatrixBands>& bands,
                  int numChannels, int numSamples) noexcept
    {
        const auto channels = juce::jlimit (1, MaxChannels, numChannels);

        if (numBands <= 1)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto* src = input.getReadPointer (juce::jmin (ch, input.getNumChannels() - 1));
                auto* dst = bands[0].getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                    dst[i] = sanitise (src[i]);
            }
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto* src = input.getReadPointer (juce::jmin (ch, input.getNumChannels() - 1));

                float* out[kMaxMatrixBands] {};

                for (int b = 0; b < numBands; ++b)
                    out[b] = bands[(size_t) b].getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    auto remainder = sanitise (src[i]);

                    // Peel one band off the bottom at a time; whatever is left
                    // after the last crossover is the top band.
                    for (int b = 0; b + 1 < numBands; ++b)
                    {
                        float low = 0.0f, high = 0.0f;
                        splits[(size_t) b].split (ch, remainder, low, high);

                        // ...then re-align it with every crossover still to come.
                        for (int later = b + 1; later + 1 < numBands; ++later)
                            low = allpasses[(size_t) b][(size_t) later].process (ch, low);

                        out[b][i] = low;
                        remainder = high;
                    }

                    out[numBands - 1][i] = remainder;
                }
            }
        }

        for (int b = numBands; b < kMaxMatrixBands; ++b)
            bands[(size_t) b].clear (0, numSamples);
    }

    /** The crossover frequencies a fresh instance starts from. */
    static float defaultCrossover (int index) noexcept
    {
        constexpr float defaults[kMaxCrossovers] = { 120.0f, 2500.0f, 8000.0f };
        return defaults[juce::jlimit (0, kMaxCrossovers - 1, index)];
    }

    static constexpr float kMinCrossoverHz = 20.0f;
    /** Roughly a third of an octave; below this the bands stop being distinct. */
    static constexpr float kMinSpacing = 1.25f;
    /** Glide rate. Fast enough that dragging a handle feels direct, slow enough
        that no block steps the cutoff far enough to be heard as a click. */
    static constexpr double kOctavesPerSecond = 12.0;

private:
    void applyCutoffs() noexcept
    {
        for (int i = 0; i < kMaxCrossovers; ++i)
        {
            splits[(size_t) i].setCutoff (currentHz[i], sr);

            // Band `b` needs the allpass of every crossover above it.
            for (int b = 0; b < kMaxCrossovers; ++b)
                allpasses[(size_t) b][(size_t) i].setCutoff (currentHz[i], sr);
        }
    }

    double sr = 44100.0;
    int numBands = 3;

    float targetHz[kMaxCrossovers]  { 120.0f, 2500.0f, 8000.0f };
    float currentHz[kMaxCrossovers] { 120.0f, 2500.0f, 8000.0f };

    std::array<TwoWayCrossover<MaxChannels>, kMaxCrossovers> splits;
    // allpasses[band][crossover] — only the upper triangle is ever used.
    std::array<std::array<CrossoverAllpass<MaxChannels>, kMaxCrossovers>, kMaxCrossovers> allpasses;
};
} // namespace sf
