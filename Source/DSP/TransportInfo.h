#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace sf
{
/** The subset of the host's playhead state the engines actually use.

    Kept as a plain struct so tests can drive transport-dependent behaviour
    (tempo changes, loop jumps, mid-bar starts) without a host. */
struct TransportInfo
{
    bool   valid        = false;   // the host gave us a playhead at all
    bool   isPlaying    = false;
    double bpm          = 120.0;
    double ppqPosition  = 0.0;     // quarter notes at the first sample of the block
    int    timeSigNumerator   = 4;
    int    timeSigDenominator = 4;

    /** Quarter notes advanced per sample at the current tempo. */
    double ppqPerSample (double sampleRate) const noexcept
    {
        if (sampleRate <= 0.0)
            return 0.0;

        return bpm / 60.0 / sampleRate;
    }

    /** True when the position jumped in a way that means "re-anchor, do not glide". */
    bool hasDiscontinuity (const TransportInfo& previous, double blockQuarters) const noexcept
    {
        if (! valid || ! previous.valid)
            return true;

        if (isPlaying != previous.isPlaying)
            return true;

        const auto expected = previous.ppqPosition + blockQuarters;
        const auto slack    = juce::jmax (0.05, blockQuarters * 0.5);

        return std::abs (ppqPosition - expected) > slack;
    }
};

/** Reads the host playhead, falling back to a sane idle state. */
inline TransportInfo readTransport (juce::AudioPlayHead* playHead) noexcept
{
    TransportInfo info;

    if (playHead == nullptr)
        return info;

    const auto position = playHead->getPosition();

    if (! position.hasValue())
        return info;

    info.valid = true;
    info.isPlaying = position->getIsPlaying();

    if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0 && *bpm < 1000.0)
        info.bpm = *bpm;

    if (const auto ppq = position->getPpqPosition(); ppq.hasValue() && std::isfinite (*ppq))
        info.ppqPosition = *ppq;

    if (const auto sig = position->getTimeSignature(); sig.hasValue())
    {
        info.timeSigNumerator   = juce::jlimit (1, 64, sig->numerator);
        info.timeSigDenominator = juce::jlimit (1, 64, sig->denominator);
    }

    return info;
}
} // namespace sf
