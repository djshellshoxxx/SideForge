#include "CurveEditor.h"
#include "LookAndFeel.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace sf
{
namespace
{
enum CurveMenuId
{
    idNone = 0,
    idTypeCurve = 1,
    idTypeSCurve,
    idTypeStep,
    idRemoveNode,
    idResetTension,
    idUndo,
    idRedo,
    idShapeBase = 100,
};
} // namespace

CurveEditor::CurveEditor()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    setTooltip ("Drag nodes to shape the curve.  Double-click to add or remove a node.\n"
                "Drag the middle of a segment to bend it.  Right-click for segment types and shapes.\n"
                "Ctrl+Z undo, Ctrl+Shift+Z redo.");
}

void CurveEditor::setModel (CurveModel* newModel)
{
    model = newModel;
    undoStack.clear();
    undoIndex = -1;

    if (model != nullptr)
        pushUndoState();

    repaint();
}

void CurveEditor::setGrid (int divisions, bool shouldSnap)
{
    const auto clamped = juce::jlimit (1, 64, divisions);

    if (clamped != gridDivisions || shouldSnap != snapEnabled)
    {
        gridDivisions = clamped;
        snapEnabled = shouldSnap;
        repaint();
    }
}

void CurveEditor::setPlayheadPhase (float phase)
{
    // Only repaint when the playhead has moved by at least a pixel's worth.
    const auto width = juce::jmax (1.0f, plotBounds().getWidth());

    if (std::abs (phase - playheadPhase) * width < 1.0f)
        return;

    playheadPhase = phase;
    repaint();
}

juce::Rectangle<float> CurveEditor::plotBounds() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 10.0f);
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto area = plotBounds();

    return { area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, x),
             area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, y) };
}

juce::Point<float> CurveEditor::toCurve (juce::Point<float> screen) const
{
    const auto area = plotBounds();

    return { juce::jlimit (0.0f, 1.0f, (screen.x - area.getX()) / juce::jmax (1.0f, area.getWidth())),
             juce::jlimit (0.0f, 1.0f, (area.getBottom() - screen.y) / juce::jmax (1.0f, area.getHeight())) };
}

float CurveEditor::snapX (float x) const
{
    if (! snapEnabled || gridDivisions < 1)
        return x;

    const auto step = 1.0f / (float) gridDivisions;

    return juce::jlimit (0.0f, 1.0f, std::round (x / step) * step);
}

int CurveEditor::nodeAt (juce::Point<float> position) const
{
    if (model == nullptr)
        return -1;

    for (int i = 0; i < model->getNumNodes(); ++i)
    {
        const auto& node = model->getNode (i);

        if (toScreen (node.x, node.y).getDistanceFrom (position) <= kGrabRadius)
            return i;
    }

    return -1;
}

int CurveEditor::segmentAt (juce::Point<float> position) const
{
    if (model == nullptr || model->getNumNodes() < 2)
        return -1;

    const auto curvePoint = toCurve (position);

    for (int i = 0; i + 1 < model->getNumNodes(); ++i)
    {
        const auto& a = model->getNode (i);
        const auto& b = model->getNode (i + 1);

        if (curvePoint.x < a.x || curvePoint.x > b.x)
            continue;

        // Only the middle third of a segment grabs, so the ends stay available
        // for their nodes.
        const auto span = b.x - a.x;

        if (span < 1.0e-4f)
            continue;

        const auto position01 = (curvePoint.x - a.x) / span;

        if (position01 < 0.25f || position01 > 0.75f)
            continue;

        const auto onCurve = toScreen (curvePoint.x, model->evaluate (curvePoint.x));

        if (std::abs (onCurve.y - position.y) <= kGrabRadius * 1.6f)
            return i;
    }

    return -1;
}

void CurveEditor::beginEdit()
{
    editInProgress = true;
}

void CurveEditor::commitEdit()
{
    if (! editInProgress)
        return;

    editInProgress = false;
    pushUndoState();

    if (onCurveChanged != nullptr)
        onCurveChanged();
}

void CurveEditor::pushUndoState()
{
    if (model == nullptr)
        return;

    // Drop anything that was redoable: a new edit forks the history.
    if (undoIndex >= 0 && undoIndex + 1 < (int) undoStack.size())
        undoStack.erase (undoStack.begin() + undoIndex + 1, undoStack.end());

    if (! undoStack.empty() && undoStack.back() .size() == model->getNodes().size())
    {
        // Skip states that are identical to the last one, so holding a node still
        // and releasing it does not fill the history.
        auto identical = true;
        const auto& previous = undoStack.back();

        for (size_t i = 0; i < previous.size() && identical; ++i)
        {
            const auto& a = previous[i];
            const auto& b = model->getNodes()[i];
            identical = std::abs (a.x - b.x) < 1.0e-6f
                     && std::abs (a.y - b.y) < 1.0e-6f
                     && std::abs (a.tension - b.tension) < 1.0e-6f
                     && a.type == b.type;
        }

        if (identical)
            return;
    }

    undoStack.push_back (model->getNodes());

    if ((int) undoStack.size() > kMaxUndoStates)
        undoStack.erase (undoStack.begin());

    undoIndex = (int) undoStack.size() - 1;
}

void CurveEditor::undo()
{
    if (! canUndo() || model == nullptr)
        return;

    --undoIndex;
    model->setNodes (undoStack[(size_t) undoIndex]);

    if (onCurveChanged != nullptr)
        onCurveChanged();

    repaint();
}

void CurveEditor::redo()
{
    if (! canRedo() || model == nullptr)
        return;

    ++undoIndex;
    model->setNodes (undoStack[(size_t) undoIndex]);

    if (onCurveChanged != nullptr)
        onCurveChanged();

    repaint();
}

void CurveEditor::applyShape (int shapeIndex)
{
    if (model == nullptr)
        return;

    model->setPresetShape (shapeIndex);
    pushUndoState();

    if (onCurveChanged != nullptr)
        onCurveChanged();

    repaint();
}

bool CurveEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        if (key.getKeyCode() == 'Z')
        {
            key.getModifiers().isShiftDown() ? redo() : undo();
            return true;
        }

        if (key.getKeyCode() == 'Y')
        {
            redo();
            return true;
        }
    }

    return false;
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    if (model == nullptr)
        return;

    if (e.mods.isPopupMenu())
    {
        showContextMenu (e.getPosition());
        return;
    }

    const auto position = e.position;

    draggingNode = nodeAt (position);
    draggingSegment = -1;

    if (draggingNode < 0)
    {
        draggingSegment = segmentAt (position);

        if (draggingSegment >= 0)
        {
            dragStartTension = model->getNode (draggingSegment).tension;
            dragStartPosition = position;
        }
    }

    if (draggingNode >= 0 || draggingSegment >= 0)
        beginEdit();
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (model == nullptr || e.mods.isPopupMenu())
        return;

    if (draggingNode >= 0)
    {
        auto target = toCurve (e.position);

        // Shift bypasses the grid for a fine placement, matching the modifier the
        // knobs use for the same purpose.
        const auto x = e.mods.isShiftDown() ? target.x : snapX (target.x);

        model->moveNode (draggingNode, x, target.y);
        repaint();
    }
    else if (draggingSegment >= 0)
    {
        const auto delta = (dragStartPosition.y - e.position.y) / juce::jmax (40.0f, plotBounds().getHeight() * 0.5f);
        const auto& a = model->getNode (draggingSegment);
        const auto& b = model->getNode (juce::jmin (draggingSegment + 1, model->getNumNodes() - 1));

        // Bending "up" should always look like up, whichever way the segment runs.
        const auto sign = (b.y >= a.y) ? 1.0f : -1.0f;

        model->setTension (draggingSegment, juce::jlimit (-1.0f, 1.0f, dragStartTension + delta * sign * 1.5f));
        repaint();
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggingNode >= 0 || draggingSegment >= 0)
        commitEdit();

    draggingNode = -1;
    draggingSegment = -1;
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto node = nodeAt (e.position);
    const auto segment = node < 0 ? segmentAt (e.position) : -1;

    if (node != hoverNode || segment != hoverSegment)
    {
        hoverNode = node;
        hoverSegment = segment;
        setMouseCursor (node >= 0 ? juce::MouseCursor::DraggingHandCursor
                      : segment >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                     : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CurveEditor::mouseExit (const juce::MouseEvent&)
{
    hoverNode = -1;
    hoverSegment = -1;
    repaint();
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (model == nullptr || e.mods.isPopupMenu())
        return;

    const auto node = nodeAt (e.position);

    if (node > 0 && node < model->getNumNodes() - 1)
    {
        model->removeNode (node);
    }
    else if (node < 0)
    {
        const auto target = toCurve (e.position);
        model->addNode (snapX (target.x), target.y);
    }
    else
    {
        return;   // an end node: nothing sensible to do
    }

    pushUndoState();

    if (onCurveChanged != nullptr)
        onCurveChanged();

    repaint();
}

void CurveEditor::showContextMenu (juce::Point<int> position)
{
    if (model == nullptr)
        return;

    const auto node = nodeAt (position.toFloat());
    const auto segment = node < 0 ? segmentAt (position.toFloat()) : -1;
    const auto segmentForType = segment >= 0 ? segment : juce::jmax (0, node);

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    const auto currentType = model->getNode (segmentForType).type;

    menu.addSectionHeader (node >= 0 ? "Node " + juce::String (node + 1)
                                     : "Segment " + juce::String (segmentForType + 1));

    menu.addItem (idTypeCurve,  "Curve",   true, currentType == SegmentType::Curve);
    menu.addItem (idTypeSCurve, "S-Curve", true, currentType == SegmentType::SCurve);
    menu.addItem (idTypeStep,   "Step",    true, currentType == SegmentType::Step);
    menu.addItem (idResetTension, "Straighten segment");

    if (node > 0 && node < model->getNumNodes() - 1)
    {
        menu.addSeparator();
        menu.addItem (idRemoveNode, "Remove node");
    }

    menu.addSeparator();

    juce::PopupMenu shapes;
    const auto shapeNames = CurveModel::getShapeNames();

    for (int i = 0; i < shapeNames.size(); ++i)
        shapes.addItem (idShapeBase + i, shapeNames[i]);

    menu.addSubMenu ("Replace with shape", shapes);

    menu.addSeparator();
    menu.addItem (idUndo, "Undo", canUndo());
    menu.addItem (idRedo, "Redo", canRedo());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [this, node, segmentForType] (int result)
    {
        if (result == idNone || model == nullptr)
            return;

        if (result >= idShapeBase)
        {
            applyShape (result - idShapeBase);
            return;
        }

        switch (result)
        {
            case idTypeCurve:   model->setSegmentType (segmentForType, SegmentType::Curve); break;
            case idTypeSCurve:  model->setSegmentType (segmentForType, SegmentType::SCurve); break;
            case idTypeStep:    model->setSegmentType (segmentForType, SegmentType::Step); break;
            case idResetTension: model->setTension (segmentForType, 0.0f); break;
            case idRemoveNode:  model->removeNode (node); break;
            case idUndo:        undo(); return;
            case idRedo:        redo(); return;
            default:            return;
        }

        pushUndoState();

        if (onCurveChanged != nullptr)
            onCurveChanged();

        repaint();
    });
}

void CurveEditor::resized() {}

void CurveEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    SideForgeLookAndFeel::drawPanel (g, bounds, theme::panelSunken, theme::outline);

    const auto area = plotBounds();

    // --- grid ---------------------------------------------------------------
    g.setColour (theme::outline.withAlpha (snapEnabled ? 0.55f : 0.3f));

    for (int i = 1; i < gridDivisions; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) gridDivisions;
        const auto emphasised = (gridDivisions % 4 == 0) && (i % (gridDivisions / 4) == 0);

        g.setColour (theme::outline.withAlpha (emphasised ? 0.75f : (snapEnabled ? 0.4f : 0.22f)));
        g.fillRect (juce::Rectangle<float> (x, area.getY(), emphasised ? 1.0f : 0.5f, area.getHeight()));
    }

    for (const float fraction : { 0.25f, 0.5f, 0.75f })
    {
        const auto y = area.getY() + area.getHeight() * fraction;
        g.setColour (theme::outline.withAlpha (fraction == 0.5f ? 0.6f : 0.3f));
        g.fillRect (juce::Rectangle<float> (area.getX(), y, area.getWidth(), 0.5f));
    }

    // Unity and silence.
    g.setColour (theme::outlineBright.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), area.getWidth(), 1.0f));
    g.setColour (theme::textFaint);
    g.setFont (theme::labelFont (8.5f));
    g.drawText ("0 dB", area.getX() + 3.0f, area.getY() + 1.0f, 40.0f, 10.0f,
                juce::Justification::topLeft, false);
    g.drawText ("silence", area.getX() + 3.0f, area.getBottom() - 11.0f, 50.0f, 10.0f,
                juce::Justification::bottomLeft, false);

    if (model == nullptr)
        return;

    // --- the curve ----------------------------------------------------------
    const auto steps = juce::jmax (2, (int) area.getWidth());

    juce::Path path;
    path.preallocateSpace (steps * 3 + 8);

    for (int i = 0; i <= steps; ++i)
    {
        const auto x = (float) i / (float) steps;
        const auto point = toScreen (x, model->evaluate (x));

        if (i == 0)
            path.startNewSubPath (point);
        else
            path.lineTo (point);
    }

    auto filled = path;
    filled.lineTo (area.getRight(), area.getY());
    filled.lineTo (area.getX(), area.getY());
    filled.closeSubPath();

    juce::ColourGradient gradient (theme::destination.withAlpha (0.30f), area.getX(), area.getBottom(),
                                   theme::destination.withAlpha (0.06f), area.getX(), area.getY(), false);
    g.setGradientFill (gradient);
    g.fillPath (filled);

    g.setColour (theme::destination);
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // --- playhead -----------------------------------------------------------
    if (playheadPhase >= 0.0f)
    {
        const auto x = area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, playheadPhase);

        g.setColour (theme::output.withAlpha (0.65f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, area.getY(), 1.0f, area.getHeight()));

        const auto y = toScreen (playheadPhase, model->evaluate (playheadPhase)).y;
        g.setColour (theme::output);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, y }));
    }

    // --- segment handles ----------------------------------------------------
    for (int i = 0; i + 1 < model->getNumNodes(); ++i)
    {
        const auto& a = model->getNode (i);
        const auto& b = model->getNode (i + 1);
        const auto midX = (a.x + b.x) * 0.5f;

        if (b.x - a.x < 0.02f)
            continue;

        const auto point = toScreen (midX, model->evaluate (midX));
        const auto hot = (i == hoverSegment || i == draggingSegment);

        g.setColour (theme::text.withAlpha (hot ? 0.8f : 0.22f));
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (point), 1.2f);

        if (a.type == SegmentType::Step)
        {
            g.setColour (theme::warning.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (5.0f, 2.0f).withCentre (point.translated (0.0f, -9.0f)));
        }
        else if (a.type == SegmentType::SCurve)
        {
            g.setColour (theme::collision.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (point.translated (0.0f, -9.0f)));
        }
    }

    // --- nodes --------------------------------------------------------------
    for (int i = 0; i < model->getNumNodes(); ++i)
    {
        const auto& node = model->getNode (i);
        const auto point = toScreen (node.x, node.y);
        const auto hot = (i == hoverNode || i == draggingNode);
        const auto radius = hot ? kNodeRadius + 1.5f : kNodeRadius;

        g.setColour (theme::background);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 3.0f, radius * 2.0f + 3.0f).withCentre (point));

        g.setColour (hot ? theme::text : theme::destination);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (point));

        if (hot)
        {
            const auto text = juce::String (node.x, 3) + " / "
                            + juce::String (juce::Decibels::gainToDecibels (node.y, -60.0f), 1) + " dB";
            const auto font = theme::valueFont (10.0f);
            const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 10.0f;

            auto box = juce::Rectangle<float> (width, 15.0f)
                           .withCentre (point.translated (0.0f, -18.0f))
                           .constrainedWithin (area);

            g.setColour (theme::panelRaised.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (theme::text);
            g.setFont (font);
            g.drawText (text, box, juce::Justification::centred, false);
        }
    }
}
} // namespace sf
