#pragma once
#include "../DSP/Metering.h"
#include "Theme.h"

namespace sf
{
/**
    A stereo level meter: a filled RMS bar with a brighter peak line over it, a
    latching clip indicator, and a label.

    It only ever *reads* the atomics the audio thread publishes, and it is
    repainted by the editor's single timer rather than one of its own, so the
    number of timers in the editor stays constant however many meters there are.
*/
class MeterComponent : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    MeterComponent (const juce::String& label, juce::Colour colour, bool horizontal = false);

    /** Points the meter at the channels it should display. Null clears it. */
    void setSource (const LevelMeter* left, const LevelMeter* right);

    /** Samples the atomics and repaints only if something moved perceptibly. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> barBounds (juce::Rectangle<float> area, int channel, int numChannels) const;

    juce::String label;
    juce::Colour colour;
    bool horizontal;

    const LevelMeter* sources[2] { nullptr, nullptr };

    float peakDb[2] { theme::meterFloorDb, theme::meterFloorDb };
    float rmsDb[2]  { theme::meterFloorDb, theme::meterFloorDb };
    bool  clipped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterComponent)
};

/**
    Gain reduction, drawn downward from the top because that is the direction the
    ear hears it moving. Shows the instantaneous value filled and the peak hold as
    a line.
*/
class GainReductionMeterComponent : public juce::Component,
                                    public juce::SettableTooltipClient
{
public:
    GainReductionMeterComponent();

    void setSource (const GainReductionMeter* source) { meter = source; }
    void refresh();

    void paint (juce::Graphics&) override;

    /** Maximum reduction the scale shows. */
    static constexpr float kRangeDb = 24.0f;

private:
    const GainReductionMeter* meter = nullptr;
    float currentDb = 0.0f;
    float heldDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainReductionMeterComponent)
};
} // namespace sf
