#include "SpectrumView.h"
#include "LookAndFeel.h"

namespace sf
{
namespace
{
/** How fast the display catches up with the analysis, per refresh. */
constexpr float kRiseRate = 0.55f;
constexpr float kFallRate = 0.22f;

float approach (float current, float target, float rate)
{
    return current + (target - current) * rate;
}
} // namespace

SpectrumView::SpectrumView()
{
    setTooltip ("Destination and sidechain spectra. Yellow to red marks where they collide;\n"
                "the violet curve is what the spectral stage is taking out.");
    setInterceptsMouseClicks (true, false);
}

void SpectrumView::setFrequencyRange (float lowHz, float highHz)
{
    rangeLow = juce::jlimit (kMinHz, kMaxHz, lowHz);
    rangeHigh = juce::jlimit (rangeLow, kMaxHz, highHz);
    repaint();
}

void SpectrumView::setCrossovers (bool threeBand, float xover1Hz, float xover2Hz)
{
    showCrossovers = threeBand;
    crossover1 = xover1Hz;
    crossover2 = xover2Hz;
    repaint();
}

void SpectrumView::refresh()
{
    if (source == nullptr || frozen)
        return;

    if (reduced && (++refreshCounter % 2) != 0)
        return;

    const auto& snapshot = source->readSnapshot();

    if (snapshot.numBins <= 1)
        return;

    if (snapshot.numBins != numBins)
    {
        numBins = snapshot.numBins;
        mainDb.assign ((size_t) numBins, kFloorDb);
        sideDb.assign ((size_t) numBins, kFloorDb);
        collision.assign ((size_t) numBins, 0.0f);
        gainDb.assign ((size_t) numBins, 0.0f);
    }

    sampleRate = snapshot.sampleRate;
    fftSize = snapshot.fftSize;

    for (int k = 0; k < numBins; ++k)
    {
        const auto targetMain = juce::jmax (kFloorDb, snapshot.mainDb[(size_t) k]);
        const auto targetSide = juce::jmax (kFloorDb, snapshot.sideDb[(size_t) k]);

        // Rise quickly, fall slowly: the eye needs the peaks to register.
        mainDb[(size_t) k] = approach (mainDb[(size_t) k], targetMain,
                                       targetMain > mainDb[(size_t) k] ? kRiseRate : kFallRate);
        sideDb[(size_t) k] = approach (sideDb[(size_t) k], targetSide,
                                       targetSide > sideDb[(size_t) k] ? kRiseRate : kFallRate);

        collision[(size_t) k] = approach (collision[(size_t) k], snapshot.collision[(size_t) k], kRiseRate);
        gainDb[(size_t) k]    = approach (gainDb[(size_t) k], snapshot.gainDb[(size_t) k], kRiseRate);
    }

    repaint();
}

float SpectrumView::frequencyToX (float hz, juce::Rectangle<float> area) const
{
    const auto clamped = juce::jlimit (kMinHz, kMaxHz, hz);
    const auto position = std::log (clamped / kMinHz) / std::log (kMaxHz / kMinHz);

    return area.getX() + area.getWidth() * (float) position;
}

float SpectrumView::xToFrequency (float x, juce::Rectangle<float> area) const
{
    const auto position = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / juce::jmax (1.0f, area.getWidth()));

    return kMinHz * std::pow (kMaxHz / kMinHz, position);
}

float SpectrumView::dbToY (float db, juce::Rectangle<float> area) const
{
    const auto normalised = juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kTopDb - kFloorDb));

    return area.getBottom() - area.getHeight() * normalised;
}

void SpectrumView::drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float decades[] = { 30.0f, 50.0f, 100.0f, 200.0f, 300.0f, 500.0f, 1000.0f,
                              2000.0f, 3000.0f, 5000.0f, 10000.0f, 15000.0f };

    g.setFont (theme::labelFont (8.5f));

    for (const auto hz : decades)
    {
        const auto x = frequencyToX (hz, area);

        g.setColour (theme::outline.withAlpha (0.4f));
        g.fillRect (juce::Rectangle<float> (x, area.getY(), 0.5f, area.getHeight()));

        const auto label = hz >= 1000.0f ? juce::String (hz / 1000.0f, hz < 10000.0f ? 1 : 0) + "k"
                                         : juce::String ((int) hz);

        g.setColour (theme::textFaint);
        g.drawText (label, juce::Rectangle<float> (x - 16.0f, area.getBottom() - 11.0f, 32.0f, 10.0f),
                    juce::Justification::centred, false);
    }

    for (const float db : { 0.0f, -24.0f, -48.0f, -72.0f })
    {
        const auto y = dbToY (db, area);
        g.setColour (theme::outline.withAlpha (db == 0.0f ? 0.6f : 0.3f));
        g.fillRect (juce::Rectangle<float> (area.getX(), y, area.getWidth(), 0.5f));
    }
}

void SpectrumView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area,
                                 const std::vector<float>& dbValues, juce::Colour colour,
                                 float fillAlpha) const
{
    if ((int) dbValues.size() < 2 || numBins < 2)
        return;

    const auto binHz = sampleRate / (float) juce::jmax (1, fftSize);
    const auto pixels = juce::jmax (1, (int) area.getWidth());

    juce::Path path;
    path.preallocateSpace (pixels * 3 + 8);

    auto started = false;

    for (int x = 0; x < pixels; ++x)
    {
        // One pixel spans many bins at the top of the range and fewer than one at
        // the bottom, so take the loudest bin in the pixel's frequency span.
        const auto fromHz = xToFrequency (area.getX() + (float) x, area);
        const auto toHz   = xToFrequency (area.getX() + (float) x + 1.0f, area);

        const auto firstBin = juce::jlimit (0, numBins - 1, (int) std::floor (fromHz / binHz));
        const auto lastBin  = juce::jlimit (firstBin, numBins - 1, (int) std::ceil (toHz / binHz));

        auto loudest = kFloorDb;

        for (int k = firstBin; k <= lastBin; ++k)
            loudest = juce::jmax (loudest, dbValues[(size_t) k]);

        const auto px = area.getX() + (float) x;
        const auto py = dbToY (loudest, area);

        if (! started)
        {
            path.startNewSubPath (px, py);
            started = true;
        }
        else
        {
            path.lineTo (px, py);
        }
    }

    if (! started)
        return;

    if (fillAlpha > 0.0f)
    {
        auto filled = path;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        juce::ColourGradient gradient (colour.withAlpha (fillAlpha), area.getX(), area.getY(),
                                       colour.withAlpha (fillAlpha * 0.1f), area.getX(),
                                       area.getBottom(), false);
        g.setGradientFill (gradient);
        g.fillPath (filled);
    }

    g.setColour (colour.withAlpha (0.95f));
    g.strokePath (path, juce::PathStrokeType (1.2f));
}

void SpectrumView::drawCollision (juce::Graphics& g, juce::Rectangle<float> area) const
{
    if (numBins < 2)
        return;

    const auto binHz = sampleRate / (float) juce::jmax (1, fftSize);
    const auto pixels = juce::jmax (1, (int) area.getWidth());

    for (int x = 0; x < pixels; ++x)
    {
        const auto fromHz = xToFrequency (area.getX() + (float) x, area);
        const auto toHz   = xToFrequency (area.getX() + (float) x + 1.0f, area);

        const auto firstBin = juce::jlimit (0, numBins - 1, (int) std::floor (fromHz / binHz));
        const auto lastBin  = juce::jlimit (firstBin, numBins - 1, (int) std::ceil (toHz / binHz));

        auto strongest = 0.0f;

        for (int k = firstBin; k <= lastBin; ++k)
            strongest = juce::jmax (strongest, collision[(size_t) k]);

        if (strongest < 0.02f)
            continue;

        // Yellow where the two merely overlap, red where the sidechain dominates.
        const auto colour = theme::collision.interpolatedWith (theme::collisionHot, strongest);

        g.setColour (colour.withAlpha (juce::jlimit (0.0f, 0.55f, strongest * 0.65f)));
        g.fillRect (juce::Rectangle<float> (area.getX() + (float) x, area.getY(),
                                            1.0f, area.getHeight()));
    }
}

void SpectrumView::drawAttenuation (juce::Graphics& g, juce::Rectangle<float> area) const
{
    if (numBins < 2)
        return;

    const auto binHz = sampleRate / (float) juce::jmax (1, fftSize);
    const auto pixels = juce::jmax (1, (int) area.getWidth());

    juce::Path path;
    path.preallocateSpace (pixels * 3 + 8);

    auto started = false;
    auto anyAttenuation = false;

    for (int x = 0; x < pixels; ++x)
    {
        const auto fromHz = xToFrequency (area.getX() + (float) x, area);
        const auto toHz   = xToFrequency (area.getX() + (float) x + 1.0f, area);

        const auto firstBin = juce::jlimit (0, numBins - 1, (int) std::floor (fromHz / binHz));
        const auto lastBin  = juce::jlimit (firstBin, numBins - 1, (int) std::ceil (toHz / binHz));

        auto deepest = 0.0f;

        for (int k = firstBin; k <= lastBin; ++k)
            deepest = juce::jmin (deepest, gainDb[(size_t) k]);

        if (deepest < -0.25f)
            anyAttenuation = true;

        // Drawn against the top of the box, falling as the bin is attenuated.
        const auto normalised = juce::jlimit (0.0f, 1.0f, -deepest / 36.0f);
        const auto px = area.getX() + (float) x;
        const auto py = area.getY() + area.getHeight() * 0.42f * normalised;

        if (! started)
        {
            path.startNewSubPath (px, py);
            started = true;
        }
        else
        {
            path.lineTo (px, py);
        }
    }

    if (! started || ! anyAttenuation)
        return;

    g.setColour (theme::reduction.withAlpha (0.9f));
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    g.setColour (theme::reduction);
    g.setFont (theme::labelFont (9.0f));
    g.drawText ("ATTENUATION", area.removeFromTop (12.0f).reduced (6.0f, 0.0f),
                juce::Justification::topLeft, false);
}

void SpectrumView::drawRangeMarkers (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // Outside the spectral range nothing is ever carved, so shade it out.
    const auto lowX = frequencyToX (rangeLow, area);
    const auto highX = frequencyToX (rangeHigh, area);

    g.setColour (theme::background.withAlpha (0.45f));

    if (lowX > area.getX())
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), lowX - area.getX(), area.getHeight()));

    if (highX < area.getRight())
        g.fillRect (juce::Rectangle<float> (highX, area.getY(), area.getRight() - highX, area.getHeight()));

    g.setColour (theme::outlineBright.withAlpha (0.7f));
    g.fillRect (juce::Rectangle<float> (lowX, area.getY(), 1.0f, area.getHeight()));
    g.fillRect (juce::Rectangle<float> (highX, area.getY(), 1.0f, area.getHeight()));

    if (showCrossovers)
    {
        g.setColour (theme::textDim.withAlpha (0.65f));

        for (const auto hz : { crossover1, crossover2 })
        {
            const auto x = frequencyToX (hz, area);

            for (float y = area.getY(); y < area.getBottom(); y += 6.0f)
                g.fillRect (juce::Rectangle<float> (x, y, 1.0f, 3.0f));
        }
    }
}

void SpectrumView::drawReadout (juce::Graphics& g, juce::Rectangle<float> area) const
{
    if (hoverX < area.getX() || hoverX > area.getRight() || numBins < 2)
        return;

    const auto hz = xToFrequency (hoverX, area);
    const auto binHz = sampleRate / (float) juce::jmax (1, fftSize);
    const auto bin = juce::jlimit (0, numBins - 1, (int) std::round (hz / binHz));

    g.setColour (theme::text.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (hoverX, area.getY(), 1.0f, area.getHeight()));

    const auto text = (hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                                     : juce::String ((int) hz) + " Hz")
                    + "   dest " + juce::String (mainDb[(size_t) bin], 1) + " dB"
                    + "   sc " + juce::String (sideDb[(size_t) bin], 1) + " dB"
                    + "   duck " + juce::String (gainDb[(size_t) bin], 1) + " dB";

    const auto font = theme::valueFont (10.0f);
    const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 12.0f;

    auto box = juce::Rectangle<float> (width, 16.0f)
                   .withPosition (juce::jlimit (area.getX(), area.getRight() - width, hoverX + 6.0f),
                                  area.getY() + 4.0f);

    g.setColour (theme::panelRaised.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (theme::text);
    g.setFont (font);
    g.drawText (text, box, juce::Justification::centred, false);
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    hoverX = (float) e.position.x;
    repaint();
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    SideForgeLookAndFeel::drawPanel (g, bounds, theme::panelSunken, theme::outline);

    auto area = bounds.reduced (6.0f);
    area.removeFromBottom (2.0f);

    if (numBins < 2)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::valueFont (12.0f));
        g.drawText ("Spectral analysis runs in Spectral and Hybrid modes",
                    bounds, juce::Justification::centred, false);
        return;
    }

    drawFrequencyGrid (g, area);
    drawCollision (g, area);
    drawRangeMarkers (g, area);

    drawSpectrum (g, area, sideDb, theme::sidechain, 0.16f);
    drawSpectrum (g, area, mainDb, theme::destination, 0.22f);

    if (showDelta)
    {
        // The removed signal: the destination spectrum with the applied gain,
        // which is exactly what "listen to what you took out" means.
        std::vector<float> delta ((size_t) numBins, kFloorDb);

        for (int k = 0; k < numBins; ++k)
            delta[(size_t) k] = juce::jmax (kFloorDb, mainDb[(size_t) k] + gainDb[(size_t) k]);

        drawSpectrum (g, area, delta, theme::collisionHot, 0.0f);
    }

    drawAttenuation (g, area);
    drawReadout (g, area);

    if (frozen)
    {
        g.setColour (theme::warning.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), theme::corner, 1.5f);
    }
}
} // namespace sf
