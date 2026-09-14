#include "MeterComponent.h"
#include "LookAndFeel.h"

namespace sf
{
namespace
{
constexpr float kRepaintThresholdDb = 0.15f;
} // namespace

MeterComponent::MeterComponent (const juce::String& labelToUse, juce::Colour colourToUse,
                                bool isHorizontal)
    : label (labelToUse), colour (colourToUse), horizontal (isHorizontal)
{
    setInterceptsMouseClicks (true, false);
    setTooltip (label + " level. Click to clear the clip indicator.");
}

void MeterComponent::setSource (const LevelMeter* left, const LevelMeter* right)
{
    sources[0] = left;
    sources[1] = right;
}

void MeterComponent::refresh()
{
    auto needsRepaint = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        if (sources[ch] == nullptr)
            continue;

        const auto newPeak = juce::jmax (theme::meterFloorDb, sources[ch]->getPeakDb());
        const auto newRms  = juce::jmax (theme::meterFloorDb, sources[ch]->getRmsDb());

        if (std::abs (newPeak - peakDb[ch]) > kRepaintThresholdDb
            || std::abs (newRms - rmsDb[ch]) > kRepaintThresholdDb)
            needsRepaint = true;

        peakDb[ch] = newPeak;
        rmsDb[ch] = newRms;
    }

    const auto nowClipped = (sources[0] != nullptr && sources[0]->isClipped())
                         || (sources[1] != nullptr && sources[1]->isClipped());

    if (nowClipped != clipped)
    {
        clipped = nowClipped;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void MeterComponent::mouseDown (const juce::MouseEvent&)
{
    for (auto* source : sources)
        if (source != nullptr)
            const_cast<LevelMeter*> (source)->clearClip();

    clipped = false;
    repaint();
}

juce::Rectangle<float> MeterComponent::barBounds (juce::Rectangle<float> area, int channel,
                                                  int numChannels) const
{
    if (numChannels <= 1)
        return area;

    if (horizontal)
    {
        const auto h = (area.getHeight() - 2.0f) * 0.5f;
        return { area.getX(), area.getY() + (h + 2.0f) * (float) channel, area.getWidth(), h };
    }

    const auto w = (area.getWidth() - 2.0f) * 0.5f;
    return { area.getX() + (w + 2.0f) * (float) channel, area.getY(), w, area.getHeight() };
}

void MeterComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Label strip.
    if (horizontal)
    {
        const auto labelArea = area.removeFromLeft (34.0f);
        g.setColour (theme::textDim);
        g.setFont (theme::labelFont (9.5f));
        g.drawText (label, labelArea, juce::Justification::centredLeft, false);
    }
    else
    {
        const auto labelArea = area.removeFromBottom (13.0f);
        g.setColour (theme::textDim);
        g.setFont (theme::labelFont (9.5f));
        g.drawText (label, labelArea, juce::Justification::centred, false);
    }

    // Clip indicator lives at the hot end of the scale.
    auto clipArea = horizontal ? area.removeFromRight (6.0f) : area.removeFromTop (5.0f);
    g.setColour (clipped ? theme::danger : theme::panelSunken);
    g.fillRoundedRectangle (clipArea.reduced (0.5f), 2.0f);

    if (horizontal)
        area.removeFromRight (2.0f);
    else
        area.removeFromTop (2.0f);

    g.setColour (theme::panelSunken);
    g.fillRoundedRectangle (area, 2.0f);

    const auto numChannels = (sources[1] != nullptr) ? 2 : 1;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (sources[ch] == nullptr)
            continue;

        const auto bar = barBounds (area, ch, numChannels);
        const auto rmsNorm = theme::dbToNorm (rmsDb[ch]);
        const auto peakNorm = theme::dbToNorm (peakDb[ch]);

        // RMS body, with a gradient so loud reads hot without changing hue.
        auto filled = bar;

        if (horizontal)
            filled = filled.withWidth (bar.getWidth() * rmsNorm);
        else
            filled = filled.withTop (bar.getBottom() - bar.getHeight() * rmsNorm);

        juce::ColourGradient gradient (colour.withAlpha (0.55f),
                                       horizontal ? bar.getX() : bar.getX(),
                                       horizontal ? bar.getY() : bar.getBottom(),
                                       colour,
                                       horizontal ? bar.getRight() : bar.getX(),
                                       horizontal ? bar.getY() : bar.getY(),
                                       false);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (filled, 2.0f);

        // Peak line.
        g.setColour (colour.brighter (0.4f));

        if (horizontal)
        {
            const auto x = bar.getX() + bar.getWidth() * peakNorm;
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
        }
        else
        {
            const auto y = bar.getBottom() - bar.getHeight() * peakNorm;
            g.fillRect (juce::Rectangle<float> (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f));
        }
    }

    // -12 dBFS reference tick.
    g.setColour (theme::outline);
    const auto refNorm = theme::dbToNorm (-12.0f);

    if (horizontal)
    {
        const auto x = area.getX() + area.getWidth() * refNorm;
        g.fillRect (juce::Rectangle<float> (x, area.getY(), 1.0f, area.getHeight()));
    }
    else
    {
        const auto y = area.getBottom() - area.getHeight() * refNorm;
        g.fillRect (juce::Rectangle<float> (area.getX(), y, area.getWidth(), 1.0f));
    }
}

// ===========================================================================

GainReductionMeterComponent::GainReductionMeterComponent()
{
    setTooltip ("Gain reduction, in dB. The line is the peak hold.");
}

void GainReductionMeterComponent::refresh()
{
    if (meter == nullptr)
        return;

    const auto newCurrent = meter->getCurrentDb();
    const auto newHeld = meter->getHeldDb();

    if (std::abs (newCurrent - currentDb) > kRepaintThresholdDb
        || std::abs (newHeld - heldDb) > kRepaintThresholdDb)
    {
        currentDb = newCurrent;
        heldDb = newHeld;
        repaint();
    }
}

void GainReductionMeterComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    const auto labelArea = area.removeFromBottom (13.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (9.5f));
    g.drawText ("GR", labelArea, juce::Justification::centred, false);

    g.setColour (theme::panelSunken);
    g.fillRoundedRectangle (area, 2.0f);

    const auto normalised = juce::jlimit (0.0f, 1.0f, currentDb / kRangeDb);

    if (normalised > 0.0f)
    {
        // Downward from the top: more reduction, more bar.
        const auto filled = area.withHeight (area.getHeight() * normalised);

        juce::ColourGradient gradient (theme::reduction, filled.getX(), filled.getY(),
                                       theme::reduction.withAlpha (0.55f),
                                       filled.getX(), filled.getBottom(), false);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (filled, 2.0f);
    }

    const auto heldNorm = juce::jlimit (0.0f, 1.0f, heldDb / kRangeDb);

    if (heldNorm > 0.001f)
    {
        g.setColour (theme::reduction.brighter (0.5f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * heldNorm - 1.0f,
                                            area.getWidth(), 2.0f));
    }

    // Numeric readout, because "about this much" is not good enough when you are
    // matching two tracks.
    g.setColour (currentDb > 0.2f ? theme::text : theme::textFaint);
    g.setFont (theme::valueFont (10.0f));
    g.drawText (juce::String (currentDb, 1), area.removeFromBottom (14.0f),
                juce::Justification::centred, false);
}
} // namespace sf
