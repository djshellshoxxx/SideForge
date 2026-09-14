#include "Visualizer.h"
#include "LookAndFeel.h"

namespace sf
{
Visualizer::Visualizer()
{
    history.assign ((size_t) kHistorySize, WaveformPoint {});
    setInterceptsMouseClicks (true, false);
    setTooltip ("Click to freeze or unfreeze the display. Double-click to reset the view.\n"
                "Right-click for the history length and which layers are drawn.\n"
                "Cyan: destination.  Orange: sidechain.  Violet: gain reduction.  Green: output.");
}

void Visualizer::setFrozen (bool shouldFreeze)
{
    if (frozen == shouldFreeze)
        return;

    frozen = shouldFreeze;
    repaint();

    if (onFreezeToggled != nullptr)
        onFreezeToggled();
}

void Visualizer::setEnabled (bool shouldBeEnabled)
{
    if (enabled == shouldBeEnabled)
        return;

    enabled = shouldBeEnabled;
    repaint();
}

void Visualizer::setTimeWindowSeconds (double seconds)
{
    windowSeconds = juce::jlimit (0.25, 8.0, seconds);
    repaint();
}

void Visualizer::refresh()
{
    if (source == nullptr)
        return;

    // The bus is drained even while frozen or disabled: leaving it to fill up
    // would make the audio thread's pushes fail and put a gap in the timeline the
    // moment the display came back.
    WaveformPoint point;
    auto received = 0;

    while (source->pop (point))
    {
        if (! frozen)
        {
            history[(size_t) writeIndex] = point;
            writeIndex = (writeIndex + 1) % kHistorySize;
            filled = juce::jmin (filled + 1, kHistorySize);
        }

        ++received;
    }

    if (! enabled || frozen || received == 0)
        return;

    // Reduced animation repaints every other tick, which halves the compositing
    // work without making the motion look broken.
    if (reduced && (++refreshCounter % 2) != 0)
        return;

    repaint();
}

const WaveformPoint& Visualizer::columnAt (int index) const
{
    const auto wrapped = ((index % kHistorySize) + kHistorySize) % kHistorySize;
    return history[(size_t) wrapped];
}

void Visualizer::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
    else
        setFrozen (! frozen);
}

void Visualizer::showContextMenu()
{
    enum { idNone = 0, idFreeze = 1, idSidechain, idOutput, idWindowBase = 100 };

    const double windows[] = { 0.5, 1.0, 2.0, 4.0, 8.0 };

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    menu.addSectionHeader ("Display");
    menu.addItem (idFreeze, "Freeze", true, frozen);

    juce::PopupMenu window;

    for (int i = 0; i < (int) (sizeof (windows) / sizeof (double)); ++i)
        window.addItem (idWindowBase + i,
                        juce::String (windows[i], windows[i] < 1.0 ? 1 : 0) + " s",
                        true, std::abs (windowSeconds - windows[i]) < 0.01);

    menu.addSubMenu ("History shown", window);

    menu.addSeparator();
    menu.addItem (idSidechain, "Show sidechain", true, showSidechain);
    menu.addItem (idOutput, "Show output", true, showOutput);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [this, windows] (int result)
    {
        if (result == idNone)
            return;

        if (result >= idWindowBase)
        {
            setTimeWindowSeconds (windows[result - idWindowBase]);
            return;
        }

        switch (result)
        {
            case idFreeze:    setFrozen (! frozen); break;
            case idSidechain: setShowSidechain (! showSidechain); break;
            case idOutput:    setShowOutput (! showOutput); break;
            default: break;
        }
    });
}

void Visualizer::mouseDoubleClick (const juce::MouseEvent&)
{
    setFrozen (false);
    setTimeWindowSeconds (2.0);
}

void Visualizer::drawGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour (theme::outline.withAlpha (0.45f));

    // Horizontal: centre line plus +/- half scale.
    for (const float fraction : { 0.25f, 0.5f, 0.75f })
    {
        const auto y = area.getY() + area.getHeight() * fraction;
        g.fillRect (juce::Rectangle<float> (area.getX(), y,
                                            area.getWidth(), fraction == 0.5f ? 1.0f : 0.5f));
    }

    // Vertical: one line per half second of the visible window.
    const auto divisions = juce::jlimit (2, 16, (int) std::round (windowSeconds * 2.0));

    for (int i = 1; i < divisions; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) divisions;
        g.fillRect (juce::Rectangle<float> (x, area.getY(), 0.5f, area.getHeight()));
    }
}

void Visualizer::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                               int first, int count, bool useOutput) const
{
    if (count <= 0 || area.getWidth() < 2.0f)
        return;

    const auto centre = area.getCentreY();
    const auto halfHeight = area.getHeight() * 0.46f;
    const auto pixels = juce::jmax (1, (int) area.getWidth());

    juce::Path path;
    path.preallocateSpace (pixels * 6 + 8);

    // Forward along the maxima...
    for (int x = 0; x < pixels; ++x)
    {
        const auto from = first + (int) ((juce::int64) x * count / pixels);
        const auto to   = first + (int) ((juce::int64) (x + 1) * count / pixels);

        auto highest = -1.0f;

        for (int i = from; i < juce::jmax (to, from + 1); ++i)
        {
            const auto& column = columnAt (i);
            highest = juce::jmax (highest, useOutput ? column.outMax : column.mainMax);
        }

        const auto px = area.getX() + (float) x;
        const auto py = centre - juce::jlimit (-1.0f, 1.0f, highest) * halfHeight;

        if (x == 0)
            path.startNewSubPath (px, py);
        else
            path.lineTo (px, py);
    }

    // ...and back along the minima, so the result is a filled body.
    for (int x = pixels - 1; x >= 0; --x)
    {
        const auto from = first + (int) ((juce::int64) x * count / pixels);
        const auto to   = first + (int) ((juce::int64) (x + 1) * count / pixels);

        auto lowest = 1.0f;

        for (int i = from; i < juce::jmax (to, from + 1); ++i)
        {
            const auto& column = columnAt (i);
            lowest = juce::jmin (lowest, useOutput ? column.outMin : column.mainMin);
        }

        path.lineTo (area.getX() + (float) x,
                     centre - juce::jlimit (-1.0f, 1.0f, lowest) * halfHeight);
    }

    path.closeSubPath();

    const auto colour = useOutput ? theme::output : theme::destination;

    if (! reduced)
    {
        juce::ColourGradient gradient (colour.withAlpha (0.42f), area.getX(), area.getY(),
                                       theme::destinationDeep.withAlpha (0.12f),
                                       area.getX(), area.getBottom(), false);
        g.setGradientFill (gradient);
        g.fillPath (path);
    }
    else
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillPath (path);
    }

    g.setColour (colour.withAlpha (useOutput ? 0.75f : 0.95f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void Visualizer::drawSidechain (juce::Graphics& g, juce::Rectangle<float> area,
                                int first, int count) const
{
    if (count <= 0 || area.getWidth() < 2.0f)
        return;

    // A rectified silhouette hanging from the top of its lane.
    const auto baseline = area.getY();
    const auto height = area.getHeight() * 0.92f;
    const auto pixels = juce::jmax (1, (int) area.getWidth());

    juce::Path path;
    path.preallocateSpace (pixels * 3 + 8);

    for (int x = 0; x < pixels; ++x)
    {
        const auto from = first + (int) ((juce::int64) x * count / pixels);
        const auto to   = first + (int) ((juce::int64) (x + 1) * count / pixels);

        auto highest = 0.0f;

        for (int i = from; i < juce::jmax (to, from + 1); ++i)
        {
            const auto& column = columnAt (i);
            highest = juce::jmax (highest, std::abs (column.sideMax), std::abs (column.sideMin));
        }

        const auto px = area.getX() + (float) x;
        const auto py = baseline + juce::jlimit (0.0f, 1.0f, highest) * height;

        if (x == 0)
        {
            path.startNewSubPath (px, baseline);
            path.lineTo (px, py);
        }
        else
        {
            path.lineTo (px, py);
        }
    }

    path.lineTo (area.getRight(), baseline);
    path.closeSubPath();

    // Orange through magenta, so the sidechain reads as a separate voice.
    juce::ColourGradient gradient (theme::sidechain.withAlpha (0.55f), area.getX(), baseline,
                                   theme::sidechainDeep.withAlpha (0.15f),
                                   area.getX(), area.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillPath (path);

    g.setColour (theme::sidechain.withAlpha (0.95f));
    g.strokePath (path, juce::PathStrokeType (1.2f));
}

void Visualizer::drawReductionEnvelope (juce::Graphics& g, juce::Rectangle<float> area,
                                        int first, int count) const
{
    if (count <= 0 || area.getWidth() < 2.0f)
        return;

    const auto pixels = juce::jmax (1, (int) area.getWidth());

    juce::Path path;
    path.preallocateSpace (pixels * 3 + 8);

    for (int x = 0; x < pixels; ++x)
    {
        const auto from = first + (int) ((juce::int64) x * count / pixels);
        const auto to   = first + (int) ((juce::int64) (x + 1) * count / pixels);

        auto lowest = 1.0f;

        for (int i = from; i < juce::jmax (to, from + 1); ++i)
            lowest = juce::jmin (lowest, columnAt (i).gain);

        // Top of the box is unity gain; the line falls as the plugin ducks.
        const auto px = area.getX() + (float) x;
        const auto py = area.getY() + area.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, lowest));

        if (x == 0)
            path.startNewSubPath (px, py);
        else
            path.lineTo (px, py);
    }

    if (! reduced)
    {
        auto filledPath = path;
        filledPath.lineTo (area.getRight(), area.getY());
        filledPath.lineTo (area.getX(), area.getY());
        filledPath.closeSubPath();

        g.setColour (theme::reduction.withAlpha (0.16f));
        g.fillPath (filledPath);
    }

    g.setColour (theme::reduction);
    g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void Visualizer::drawTriggerMarkers (juce::Graphics& g, juce::Rectangle<float> area,
                                     int first, int count) const
{
    if (count <= 0)
        return;

    const auto pixels = juce::jmax (1, (int) area.getWidth());

    for (int x = 0; x < pixels; ++x)
    {
        const auto from = first + (int) ((juce::int64) x * count / pixels);
        const auto to   = first + (int) ((juce::int64) (x + 1) * count / pixels);

        auto triggered = false;

        for (int i = from; i < juce::jmax (to, from + 1) && ! triggered; ++i)
            triggered = columnAt (i).trigger;

        if (! triggered)
            continue;

        const auto px = area.getX() + (float) x;

        g.setColour (theme::trigger.withAlpha (0.28f));
        g.fillRect (juce::Rectangle<float> (px - 0.5f, area.getY(), 1.0f, area.getHeight()));

        g.setColour (theme::trigger.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (px - 1.5f, area.getY(), 3.0f, 4.0f));
    }
}

void Visualizer::drawLegend (juce::Graphics& g, juce::Rectangle<float> area) const
{
    struct Entry { const char* name; juce::Colour colour; bool shown; };

    const Entry entries[] =
    {
        { "DEST",      theme::destination, true },
        { "SIDECHAIN", theme::sidechain,   showSidechain },
        { "REDUCTION", theme::reduction,   true },
        { "OUT",       theme::output,      showOutput },
    };

    auto x = area.getX();

    for (const auto& entry : entries)
    {
        if (! entry.shown)
            continue;

        g.setColour (entry.colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (x, area.getCentreY() - 3.0f, 8.0f, 6.0f), 1.5f);
        x += 12.0f;

        const auto font = theme::labelFont (9.0f);
        g.setColour (theme::textDim);
        g.setFont (font);

        const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, entry.name);
        g.drawText (entry.name, juce::Rectangle<float> (x, area.getY(), width + 2.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += width + 14.0f;
    }
}

void Visualizer::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    SideForgeLookAndFeel::drawPanel (g, bounds, theme::panelSunken, theme::outline);

    auto area = bounds.reduced (1.0f);
    const auto legendArea = area.removeFromBottom (16.0f).reduced (10.0f, 0.0f);
    area = area.reduced (6.0f, 6.0f);

    if (! enabled)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::valueFont (13.0f));
        g.drawText ("Visualiser off", bounds, juce::Justification::centred, false);
        return;
    }

    // The sidechain gets its own lane along the top. Drawn into the same space as
    // the destination it would simply sit behind it, and the whole point of
    // showing it is to see how it lines up against what it is ducking.
    const auto fullArea = area;
    auto sidechainLane = showSidechain ? area.removeFromTop (area.getHeight() * 0.26f)
                                       : juce::Rectangle<float>();

    if (showSidechain)
        area.removeFromTop (3.0f);

    drawGrid (g, area);

    // How many columns fit the requested time window.
    const auto columnsWanted = (int) std::round (windowSeconds * AnalysisBus::kColumnsPerSecond);
    const auto count = juce::jlimit (1, juce::jmin (filled, kHistorySize), columnsWanted);
    const auto first = writeIndex - count;

    if (filled > 0)
    {
        if (showSidechain)
        {
            g.setColour (theme::outline.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (sidechainLane.getX(), sidechainLane.getBottom(),
                                                sidechainLane.getWidth(), 1.0f));

            drawSidechain (g, sidechainLane, first, count);

            g.setColour (theme::sidechain.withAlpha (0.65f));
            g.setFont (theme::labelFont (8.5f));
            g.drawText ("SIDECHAIN", sidechainLane.reduced (4.0f, 2.0f),
                        juce::Justification::topLeft, false);
        }

        drawWaveform (g, area, first, count, false);

        if (showOutput)
            drawWaveform (g, area, first, count, true);

        drawReductionEnvelope (g, area, first, count);
        drawTriggerMarkers (g, fullArea, first, count);
    }
    else
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::valueFont (12.0f));
        g.drawText ("Waiting for audio", fullArea, juce::Justification::centred, false);
    }

    drawLegend (g, legendArea);

    if (frozen)
    {
        g.setColour (theme::warning.withAlpha (0.85f));
        g.setFont (theme::labelFont (10.0f));
        g.drawText ("FROZEN", area.removeFromTop (14.0f).reduced (4.0f, 0.0f),
                    juce::Justification::topRight, false);

        g.setColour (theme::warning.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), theme::corner, 1.5f);
    }
}
} // namespace sf
