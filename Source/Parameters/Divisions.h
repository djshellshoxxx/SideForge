#pragma once
#include <juce_core/juce_core.h>

namespace sf
{
/** A musical division. Either a fixed number of quarter notes, or a number of
    bars (which is resolved against the host time signature at runtime). */
struct Division
{
    const char* name;
    double      quarters;  // used when bars == 0
    int         bars;      // > 0 => length is bars * (4 * num / den) quarters
};

inline constexpr Division kDivisions[] = {
    { "1/64T", 4.0 / 64.0 * 2.0 / 3.0, 0 }, { "1/64", 4.0 / 64.0, 0 }, { "1/64.", 4.0 / 64.0 * 1.5, 0 },
    { "1/32T", 4.0 / 32.0 * 2.0 / 3.0, 0 }, { "1/32", 4.0 / 32.0, 0 }, { "1/32.", 4.0 / 32.0 * 1.5, 0 },
    { "1/16T", 4.0 / 16.0 * 2.0 / 3.0, 0 }, { "1/16", 4.0 / 16.0, 0 }, { "1/16.", 4.0 / 16.0 * 1.5, 0 },
    { "1/8T",  4.0 /  8.0 * 2.0 / 3.0, 0 }, { "1/8",  4.0 /  8.0, 0 }, { "1/8.",  4.0 /  8.0 * 1.5, 0 },
    { "1/4T",  4.0 /  4.0 * 2.0 / 3.0, 0 }, { "1/4",  4.0 /  4.0, 0 }, { "1/4.",  4.0 /  4.0 * 1.5, 0 },
    { "1/2T",  4.0 /  2.0 * 2.0 / 3.0, 0 }, { "1/2",  4.0 /  2.0, 0 }, { "1/2.",  4.0 /  2.0 * 1.5, 0 },
    { "1/1T",  4.0        * 2.0 / 3.0, 0 }, { "1/1",  4.0,        0 }, { "1/1.",  4.0 * 1.5,        0 },
    { "1 Bar", 4.0, 1 }, { "2 Bars", 8.0, 2 }, { "4 Bars", 16.0, 4 }, { "8 Bars", 32.0, 8 }
};

inline constexpr int kNumDivisions = (int) (sizeof (kDivisions) / sizeof (Division));
inline constexpr int kDefaultDivision = 13; // 1/4

/** Length of a division in quarter notes, honouring the host time signature for bar values. */
inline double divisionInQuarters (int index, int tsNumerator = 4, int tsDenominator = 4) noexcept
{
    index = juce::jlimit (0, kNumDivisions - 1, index);
    const auto& d = kDivisions[index];

    if (d.bars > 0)
    {
        const auto num = (double) juce::jlimit (1, 64, tsNumerator);
        const auto den = (double) juce::jlimit (1, 64, tsDenominator);
        return (double) d.bars * 4.0 * num / den;
    }

    return d.quarters;
}

inline juce::StringArray divisionNames()
{
    juce::StringArray names;
    for (auto& d : kDivisions)
        names.add (d.name);
    return names;
}
} // namespace sf
