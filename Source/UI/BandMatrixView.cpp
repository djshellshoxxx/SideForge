#include "BandMatrixView.h"
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace sf
{
namespace
{
constexpr int kCardHeight = 118;
constexpr float kSpectrumRise = 0.6f;
constexpr float kSpectrumFall = 0.2f;

/** A small square button drawn inside a band card. */
void drawCardButton (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
                     bool on, juce::Colour onColour, bool hovered)
{
    const auto area = bounds.toFloat().reduced (0.5f);

    g.setColour (on ? onColour : (hovered ? theme::panelRaised : theme::panelSunken));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (on ? onColour.brighter (0.3f) : theme::outline);
    g.drawRoundedRectangle (area, 3.0f, 1.0f);

    g.setColour (on ? theme::background : theme::textDim);
    g.setFont (theme::labelFont (9.0f));
    g.drawText (text, bounds, juce::Justification::centred, false);
}

juce::String frequencyLabel (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, hz < 10000.0f ? 1 : 0) + "k";

    return juce::String ((int) hz);
}
} // namespace

juce::Colour BandMatrixView::bandColour (int index)
{
    // Low is cyan, mid violet, high orange, and the optional fourth band a warm
    // yellow, so a glance at the display says which region is moving.
    switch (juce::jlimit (0, kMaxMatrixBands - 1, index))
    {
        case 0:  return theme::destination;
        case 1:  return theme::reduction;
        case 2:  return theme::sidechain;
        default: return theme::collision;
    }
}

BandMatrixView::BandMatrixView (SideForgeProcessor& processorToUse, const ControlContext& contextToUse)
    : processor (processorToUse), context (contextToUse)
{
    scratch.assign ((size_t) kFftSize * 2, 0.0f);
    originalDb.assign ((size_t) kNumBins, kFloorDb);
    processedDb.assign ((size_t) kNumBins, kFloorDb);
    sidechainDb.assign ((size_t) kNumBins, kFloorDb);

    setInterceptsMouseClicks (true, false);
    setTooltip ("Drag a crossover handle to move it; double-click one to reset it.\n"
                "Click a band card to open its controls below. E enable, B bypass, S solo, M mute.");
}

BandMatrixView::~BandMatrixView() = default;

void BandMatrixView::setSelectedBand (int index)
{
    const auto clamped = juce::jlimit (0, kMaxMatrixBands - 1, index);

    if (clamped == selectedBand)
        return;

    selectedBand = clamped;
    repaint();

    if (onBandSelected != nullptr)
        onBandSelected (selectedBand);
}

void BandMatrixView::setAnalysisEnabled (bool shouldAnalyse)
{
    if (analysisEnabled == shouldAnalyse)
        return;

    analysisEnabled = shouldAnalyse;
    repaint();
}

int BandMatrixView::activeBandCount() const
{
    return juce::jlimit (1, kMaxMatrixBands,
                         readInt (processor.getBandMatrixParams().bandCount) + 1);
}

// ---------------------------------------------------------------------------
//  Analysis
// ---------------------------------------------------------------------------

void BandMatrixView::analyse()
{
    auto& tap = processor.getAnalysisBus().getRawTap();

    // Three transforms - original, processed, sidechain - all here on the
    // message thread, never in processBlock.
    const RawTap::Channel channels[] = { RawTap::Channel::Original,
                                         RawTap::Channel::Processed,
                                         RawTap::Channel::Sidechain };

    for (int pass = 0; pass < 3; ++pass)
    {
        std::fill (scratch.begin(), scratch.end(), 0.0f);
        tap.readLatest (channels[pass], scratch.data(), kFftSize);

        window.multiplyWithWindowingTable (scratch.data(), (size_t) kFftSize);
        fft.performFrequencyOnlyForwardTransform (scratch.data());

        auto& target = pass == 0 ? originalDb : pass == 1 ? processedDb : sidechainDb;
        const auto scale = 2.0f / (float) kFftSize;

        for (int k = 0; k < kNumBins; ++k)
        {
            const auto db = juce::Decibels::gainToDecibels (scratch[(size_t) k] * scale, kFloorDb);
            const auto rate = db > target[(size_t) k] ? kSpectrumRise : kSpectrumFall;
            target[(size_t) k] += (db - target[(size_t) k]) * rate;
        }
    }
}

void BandMatrixView::refresh()
{
    auto changed = false;

    auto& matrix = processor.getBandMatrix();

    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        const auto reduction = matrix.getBand (b).getGainReductionDb();

        if (std::abs (reduction - bandReduction[(size_t) b]) > 0.15f)
        {
            bandReduction[(size_t) b] = reduction;
            changed = true;
        }
    }

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const auto hz = matrix.getCrossoverHz (i);

        if (std::abs (hz - shownCrossover[(size_t) i]) > shownCrossover[(size_t) i] * 0.002f)
        {
            shownCrossover[(size_t) i] = hz;
            changed = true;
        }
    }

    if (analysisEnabled && ! (reduced && (++refreshCounter % 2) != 0))
    {
        analyse();
        changed = true;
    }

    if (changed)
        repaint();
}

// ---------------------------------------------------------------------------
//  Geometry
// ---------------------------------------------------------------------------

float BandMatrixView::frequencyToX (float hz, juce::Rectangle<float> area) const
{
    const auto clamped = juce::jlimit (kMinHz, kMaxHz, hz);
    const auto position = std::log (clamped / kMinHz) / std::log (kMaxHz / kMinHz);

    return area.getX() + area.getWidth() * position;
}

float BandMatrixView::xToFrequency (float x, juce::Rectangle<float> area) const
{
    const auto position = juce::jlimit (0.0f, 1.0f,
                                        (x - area.getX()) / juce::jmax (1.0f, area.getWidth()));

    return kMinHz * std::pow (kMaxHz / kMinHz, position);
}

int BandMatrixView::crossoverHandleAt (juce::Point<int> position) const
{
    if (! spectrumArea.contains (position))
        return -1;

    const auto area = spectrumArea.toFloat();
    const auto count = activeBandCount() - 1;

    for (int i = 0; i < count; ++i)
        if (std::abs (frequencyToX (shownCrossover[(size_t) i], area) - (float) position.x) < 8.0f)
            return i;

    return -1;
}

int BandMatrixView::cardAt (juce::Point<int> position) const
{
    for (int b = 0; b < activeBandCount(); ++b)
        if (cards[(size_t) b].bounds.contains (position))
            return b;

    return -1;
}

void BandMatrixView::resized()
{
    auto area = getLocalBounds().reduced (1);

    cardArea = area.removeFromBottom (kCardHeight);
    spectrumArea = area.reduced (6, 6);

    const auto count = activeBandCount();
    const auto cardWidth = cardArea.getWidth() / juce::jmax (1, count);

    for (int b = 0; b < kMaxMatrixBands; ++b)
    {
        auto& card = cards[(size_t) b];

        if (b >= count)
        {
            card = {};
            continue;
        }

        card.bounds = juce::Rectangle<int> (cardArea.getX() + b * cardWidth, cardArea.getY(),
                                            cardWidth - 4, cardArea.getHeight()).reduced (2);

        auto buttons = card.bounds.reduced (6).removeFromBottom (18);
        const auto buttonWidth = juce::jmax (18, (buttons.getWidth() - 9) / 4);

        card.enableButton = buttons.removeFromLeft (buttonWidth);
        buttons.removeFromLeft (3);
        card.bypassButton = buttons.removeFromLeft (buttonWidth);
        buttons.removeFromLeft (3);
        card.soloButton = buttons.removeFromLeft (buttonWidth);
        buttons.removeFromLeft (3);
        card.muteButton = buttons.removeFromLeft (buttonWidth);
    }
}

// ---------------------------------------------------------------------------
//  Interaction
// ---------------------------------------------------------------------------

void BandMatrixView::toggleBandParameter (int band, const char* parameterId)
{
    if (auto* parameter = context.getParameter (parameterId))
    {
        const auto next = parameter->getValue() < 0.5f ? 1.0f : 0.0f;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (next);
        parameter->endChangeGesture();
    }

    juce::ignoreUnused (band);
    repaint();
}

void BandMatrixView::mouseDown (const juce::MouseEvent& e)
{
    const auto position = e.getPosition();

    // Crossover handles first: they sit on top of the spectrum.
    const auto handle = crossoverHandleAt (position);

    if (handle >= 0 && ! e.mods.isPopupMenu())
    {
        draggingHandle = handle;

        if (auto* parameter = context.getParameter (handle == 0 ? pid::bmXover1
                                                   : handle == 1 ? pid::bmXover2 : pid::bmXover3))
            parameter->beginChangeGesture();

        return;
    }

    const auto card = cardAt (position);

    if (card < 0)
        return;

    const auto& ids = pid::band[card];
    const auto& shape = cards[(size_t) card];

    if (e.mods.isPopupMenu())
    {
        if (auto* parameter = context.getParameter (ids.mode))
            ParameterMenu::show (*this, *parameter, context, {}, [this] { repaint(); });

        return;
    }

    if (shape.enableButton.contains (position))      toggleBandParameter (card, ids.enable);
    else if (shape.bypassButton.contains (position)) toggleBandParameter (card, ids.bypass);
    else if (shape.soloButton.contains (position))   toggleBandParameter (card, ids.solo);
    else if (shape.muteButton.contains (position))   toggleBandParameter (card, ids.mute);
    else                                             setSelectedBand (card);
}

void BandMatrixView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingHandle < 0)
        return;

    const auto hz = xToFrequency ((float) e.position.x, spectrumArea.toFloat());

    const auto* id = draggingHandle == 0 ? pid::bmXover1
                   : draggingHandle == 1 ? pid::bmXover2 : pid::bmXover3;

    if (auto* parameter = context.getParameter (id))
    {
        // Shift is the same fine-adjust modifier the knobs use.
        const auto target = e.mods.isShiftDown()
                                ? parameter->convertFrom0to1 (parameter->getValue())
                                      + (hz - parameter->convertFrom0to1 (parameter->getValue())) * 0.15f
                                : hz;

        parameter->setValueNotifyingHost (parameter->convertTo0to1 (target));
    }

    repaint();
}

void BandMatrixView::mouseUp (const juce::MouseEvent&)
{
    if (draggingHandle < 0)
        return;

    const auto* id = draggingHandle == 0 ? pid::bmXover1
                   : draggingHandle == 1 ? pid::bmXover2 : pid::bmXover3;

    if (auto* parameter = context.getParameter (id))
        parameter->endChangeGesture();

    draggingHandle = -1;
    repaint();
}

void BandMatrixView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto handle = crossoverHandleAt (e.getPosition());

    if (handle < 0)
        return;

    const auto* id = handle == 0 ? pid::bmXover1 : handle == 1 ? pid::bmXover2 : pid::bmXover3;

    if (auto* parameter = context.getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
        parameter->endChangeGesture();
    }
}

void BandMatrixView::mouseMove (const juce::MouseEvent& e)
{
    const auto handle = crossoverHandleAt (e.getPosition());
    const auto card = cardAt (e.getPosition());

    if (handle != hoverHandle || card != hoverCard)
    {
        hoverHandle = handle;
        hoverCard = card;
        setMouseCursor (handle >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                    : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BandMatrixView::mouseExit (const juce::MouseEvent&)
{
    if (hoverHandle >= 0 || hoverCard >= 0)
    {
        hoverHandle = -1;
        hoverCard = -1;
        repaint();
    }
}

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------

void BandMatrixView::drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float ticks[] = { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                            2000.0f, 5000.0f, 10000.0f, 15000.0f };

    g.setFont (theme::labelFont (8.5f));

    for (const auto hz : ticks)
    {
        const auto x = frequencyToX (hz, area);

        g.setColour (theme::outline.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x, area.getY(), 0.5f, area.getHeight()));

        g.setColour (theme::textFaint);
        g.drawText (frequencyLabel (hz),
                    juce::Rectangle<float> (x - 16.0f, area.getBottom() - 11.0f, 32.0f, 10.0f),
                    juce::Justification::centred, false);
    }
}

void BandMatrixView::drawBandRegions (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto count = activeBandCount();
    const auto& refs = processor.getBandMatrixParams();
    const auto anySolo = processor.getBandMatrix().isAnyBandSoloed();

    for (int b = 0; b < count; ++b)
    {
        const auto lowHz  = (b == 0) ? kMinHz : shownCrossover[(size_t) (b - 1)];
        const auto highHz = (b == count - 1) ? kMaxHz : shownCrossover[(size_t) b];

        const auto left = frequencyToX (lowHz, area);
        const auto right = frequencyToX (highHz, area);
        auto region = juce::Rectangle<float> (left, area.getY(), right - left, area.getHeight());

        const auto mode = readEnum<BandProcessorMode> (refs.bands[b].mode);
        const auto enabled = readBool (refs.bands[b].enable);
        const auto bypassed = readBool (refs.bands[b].bypass);
        const auto muted = readBool (refs.bands[b].mute);
        const auto soloed = readBool (refs.bands[b].solo);
        const auto heard = anySolo ? soloed : ! muted;
        const auto working = enabled && ! bypassed && mode != BandProcessorMode::Off;

        auto colour = bandColour (b);

        if (! working || ! heard)
            colour = theme::outline;   // dim neutral: this region is not being processed

        // The band's floor rises as it ducks, so a hard low-end pull is a deep
        // contraction and a light high-end one barely moves.
        const auto reduction = juce::jlimit (0.0f, 24.0f, bandReduction[(size_t) b]);
        const auto contraction = working && heard ? reduction / 24.0f : 0.0f;

        g.setColour (colour.withAlpha (b == selectedBand ? 0.14f : 0.07f));
        g.fillRect (region);

        if (contraction > 0.001f)
        {
            auto active = region;
            active = active.withTop (region.getY() + region.getHeight() * (1.0f - contraction));

            juce::ColourGradient gradient (colour.withAlpha (0.55f), active.getX(), active.getBottom(),
                                           colour.withAlpha (0.10f), active.getX(), active.getY(),
                                           false);
            g.setGradientFill (gradient);
            g.fillRect (active);
        }

        // Edges.
        g.setColour (colour.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (left, area.getY(), 1.0f, area.getHeight()));

        // Label.
        auto label = region.withHeight (14.0f).reduced (4.0f, 0.0f);

        if (label.getWidth() > 30.0f)
        {
            g.setColour (colour.withAlpha (working && heard ? 0.95f : 0.4f));
            g.setFont (theme::labelFont (9.5f));

            auto text = defaultBandName (b, count).toUpperCase();

            if (! heard)          text += muted ? "  MUTE" : "  --";
            else if (bypassed)    text += "  BYP";
            else if (! enabled)   text += "  OFF";
            else                  text += "  " + bandModeNames()[(int) mode].toUpperCase();

            g.drawText (text, label, juce::Justification::topLeft, false);

            // Only when there is room: squeezed against the next band's label it
            // reads as one run-on string.
            if (working && heard && reduction > 0.2f && label.getWidth() > 96.0f)
            {
                g.setColour (theme::danger.withAlpha (0.9f));
                g.drawText ("-" + juce::String (reduction, 1) + " dB",
                            label.withTrimmedRight (6.0f), juce::Justification::topRight, false);
            }
        }
    }
}

void BandMatrixView::drawCrossoverHandles (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto count = activeBandCount() - 1;

    for (int i = 0; i < count; ++i)
    {
        const auto x = frequencyToX (shownCrossover[(size_t) i], area);
        const auto hot = (i == hoverHandle || i == draggingHandle);

        g.setColour (hot ? theme::text : theme::outlineBright);
        g.fillRect (juce::Rectangle<float> (x - (hot ? 1.0f : 0.5f), area.getY(),
                                            hot ? 2.0f : 1.0f, area.getHeight()));

        // Grip at the top.
        const auto grip = juce::Rectangle<float> (14.0f, 10.0f)
                              .withCentre ({ x, area.getY() + 5.0f });
        g.setColour (hot ? theme::accent : theme::panelRaised);
        g.fillRoundedRectangle (grip, 2.0f);
        g.setColour (hot ? theme::accent.brighter (0.4f) : theme::outlineBright);
        g.drawRoundedRectangle (grip, 2.0f, 1.0f);

        if (hot)
        {
            const auto text = frequencyLabel (shownCrossover[(size_t) i]) + "Hz";
            const auto font = theme::valueFont (10.0f);
            const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 10.0f;

            auto box = juce::Rectangle<float> (width, 15.0f)
                           .withCentre ({ x, area.getY() + 24.0f })
                           .constrainedWithin (area);

            g.setColour (theme::panelRaised.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (theme::text);
            g.setFont (font);
            g.drawText (text, box, juce::Justification::centred, false);
        }
    }
}

void BandMatrixView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area,
                                   const std::vector<float>& magnitudesDb,
                                   juce::Colour colour, float fillAlpha) const
{
    if ((int) magnitudesDb.size() < 2)
        return;

    const auto sampleRate = (float) juce::jmax (8000.0, processor.getSampleRate());
    const auto binHz = sampleRate / (float) kFftSize;
    const auto pixels = juce::jmax (1, (int) area.getWidth());

    juce::Path path;
    path.preallocateSpace (pixels * 3 + 8);

    auto started = false;

    for (int x = 0; x < pixels; ++x)
    {
        const auto fromHz = xToFrequency (area.getX() + (float) x, area);
        const auto toHz   = xToFrequency (area.getX() + (float) x + 1.0f, area);

        const auto exactFirst = fromHz / binHz;
        const auto exactLast  = toHz / binHz;

        float loudest;

        if (exactLast - exactFirst < 1.0f)
        {
            // Fewer than one bin per pixel, which is the whole bottom of a log
            // axis. Interpolating between neighbours draws a curve; taking the
            // nearest bin draws a staircase.
            const auto centre = juce::jlimit (0.0f, (float) (kNumBins - 1),
                                              (exactFirst + exactLast) * 0.5f);
            const auto lower = (int) centre;
            const auto upper = juce::jmin (kNumBins - 1, lower + 1);
            const auto fraction = centre - (float) lower;

            loudest = magnitudesDb[(size_t) lower]
                        + (magnitudesDb[(size_t) upper] - magnitudesDb[(size_t) lower]) * fraction;
        }
        else
        {
            const auto firstBin = juce::jlimit (0, kNumBins - 1, (int) std::floor (exactFirst));
            const auto lastBin  = juce::jlimit (firstBin, kNumBins - 1, (int) std::ceil (exactLast));

            loudest = kFloorDb;

            for (int k = firstBin; k <= lastBin; ++k)
                loudest = juce::jmax (loudest, magnitudesDb[(size_t) k]);
        }

        const auto normalised = juce::jlimit (0.0f, 1.0f,
                                              (loudest - kFloorDb) / (kTopDb - kFloorDb));
        const auto px = area.getX() + (float) x;
        const auto py = area.getBottom() - area.getHeight() * normalised;

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

    if (fillAlpha > 0.0f && ! reduced)
    {
        auto filled = path;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setColour (colour.withAlpha (fillAlpha));
        g.fillPath (filled);
    }

    g.setColour (colour.withAlpha (0.9f));
    g.strokePath (path, juce::PathStrokeType (1.1f));
}

void BandMatrixView::drawCard (juce::Graphics& g, int band) const
{
    const auto& card = cards[(size_t) band];

    if (card.bounds.isEmpty())
        return;

    const auto& refs = processor.getBandMatrixParams();
    const auto& ids = pid::band[band];
    const auto colour = bandColour (band);
    const auto selected = (band == selectedBand);

    const auto mode = readEnum<BandProcessorMode> (refs.bands[band].mode);
    const auto enabled = readBool (refs.bands[band].enable);
    const auto bypassed = readBool (refs.bands[band].bypass);
    const auto soloed = readBool (refs.bands[band].solo);
    const auto muted = readBool (refs.bands[band].mute);

    auto bounds = card.bounds.toFloat();

    g.setColour (selected ? theme::panelRaised : theme::panel);
    g.fillRoundedRectangle (bounds, theme::cornerSmall);
    g.setColour (selected ? colour : theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerSmall, selected ? 1.5f : 1.0f);

    // Colour stripe down the left edge.
    g.setColour (colour.withAlpha (enabled && ! bypassed ? 0.9f : 0.3f));
    g.fillRoundedRectangle (bounds.removeFromLeft (3.0f).reduced (0.0f, 4.0f), 1.5f);

    auto inner = card.bounds.reduced (8, 6);

    // Name and frequency range.
    const auto count = activeBandCount();
    const auto lowHz  = (band == 0) ? kMinHz : shownCrossover[(size_t) (band - 1)];
    const auto highHz = (band == count - 1) ? kMaxHz : shownCrossover[(size_t) band];

    auto header = inner.removeFromTop (14);
    g.setColour (theme::text);
    g.setFont (theme::labelFont (11.0f));
    g.drawText (defaultBandName (band, count), header, juce::Justification::centredLeft, false);

    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (8.5f));
    g.drawText (frequencyLabel (lowHz) + " - " + frequencyLabel (highHz),
                header, juce::Justification::centredRight, false);

    // Mode.
    auto modeRow = inner.removeFromTop (14);
    g.setColour (colour.withAlpha (enabled && ! bypassed ? 0.95f : 0.4f));
    g.setFont (theme::labelFont (10.0f));
    g.drawText (bandModeNames()[(int) mode], modeRow, juce::Justification::centredLeft, false);

    // Depth / attack / release at a glance.
    auto stats = inner.removeFromTop (13);
    g.setColour (theme::textDim);
    g.setFont (theme::valueFont (9.0f));
    g.drawText ("D " + juce::String (readFloat (refs.bands[band].depth), 0) + "%   "
                    + "A " + juce::String (readFloat (refs.bands[band].attack), 1) + "   "
                    + "R " + juce::String (readFloat (refs.bands[band].release), 0),
                stats, juce::Justification::centredLeft, false);

    // Gain-reduction meter.
    auto meter = inner.removeFromTop (10).reduced (0, 2);
    g.setColour (theme::panelSunken);
    g.fillRoundedRectangle (meter.toFloat(), 2.0f);

    const auto reduction = juce::jlimit (0.0f, 24.0f, bandReduction[(size_t) band]);

    if (reduction > 0.05f)
    {
        auto filled = meter.toFloat();
        filled = filled.withWidth (filled.getWidth() * (reduction / 24.0f));

        g.setColour (theme::danger.withAlpha (0.85f));
        g.fillRoundedRectangle (filled, 2.0f);
    }

    g.setColour (reduction > 0.2f ? theme::text : theme::textFaint);
    g.setFont (theme::labelFont (8.5f));
    g.drawText ("-" + juce::String (reduction, 1) + " dB",
                inner.removeFromTop (11), juce::Justification::centredLeft, false);

    // Buttons.
    const auto hovered = (band == hoverCard);
    drawCardButton (g, card.enableButton, "E", enabled, theme::ok, hovered);
    drawCardButton (g, card.bypassButton, "B", bypassed, theme::warning, hovered);
    drawCardButton (g, card.soloButton, "S", soloed, theme::collision, hovered);
    drawCardButton (g, card.muteButton, "M", muted, theme::danger, hovered);

    juce::ignoreUnused (ids);
}

void BandMatrixView::paint (juce::Graphics& g)
{
    SideForgeLookAndFeel::drawPanel (g, getLocalBounds().toFloat(), theme::panelSunken, theme::outline);

    const auto area = spectrumArea.toFloat();

    if (processor.getRoutingMode() != RoutingMode::BandMatrix)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::valueFont (13.0f));
        g.drawText ("Switch Routing to Band Matrix to split the track into bands",
                    spectrumArea, juce::Justification::centred, false);
    }

    drawFrequencyGrid (g, area);
    drawBandRegions (g, area);

    if (analysisEnabled)
    {
        // The original first and faint, then the detector, then the processed
        // result brightly on top: the gap between the first and the last is the
        // carve the matrix is making.
        drawSpectrum (g, area, originalDb, theme::textFaint, 0.0f);
        drawSpectrum (g, area, sidechainDb, theme::ok, 0.10f);
        drawSpectrum (g, area, processedDb, theme::text, 0.07f);
    }

    drawCrossoverHandles (g, area);

    // Legend, clear of the frequency labels along the bottom.
    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (8.5f));
    g.drawText ("GREY: ORIGINAL    WHITE: PROCESSED    GREEN: SIDECHAIN",
                spectrumArea.reduced (6, 4).withTrimmedBottom (13),
                juce::Justification::bottomRight, false);

    for (int b = 0; b < activeBandCount(); ++b)
        drawCard (g, b);
}
} // namespace sf
