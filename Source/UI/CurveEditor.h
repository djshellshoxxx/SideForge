#pragma once
#include "../Model/CurveModel.h"
#include "Theme.h"
#include <functional>
#include <vector>

namespace sf
{
/**
    The drawable gain envelope.

    Interaction, all of it discoverable from the tooltip and the right-click menu:

      - **drag a node** to move it; the two end nodes are pinned to the edges,
      - **double-click empty space** to add a node, **double-click a node** to remove it,
      - **drag the middle of a segment** vertically to bend it,
      - **right-click** for segment type, node removal and shape presets,
      - **Ctrl+Z / Ctrl+Shift+Z** for undo and redo, held locally because curve
        edits are structural rather than parameter changes.

    Edits happen on the message thread against the `CurveModel`; the component
    then calls `onCurveChanged`, and the processor re-renders the lookup table the
    audio thread reads. The audio thread never sees this class.
*/
class CurveEditor : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    CurveEditor();

    void setModel (CurveModel* model);

    /** Called after any edit, so the owner can publish a new table. */
    std::function<void()> onCurveChanged;

    /** Grid resolution in divisions across the cycle, and whether to snap to it. */
    void setGrid (int divisions, bool snapEnabled);

    /** The engine's current position through the curve, 0..1, or negative to hide. */
    void setPlayheadPhase (float phase);

    /** Replaces the curve with one of `CurveModel::getShapeNames()`. */
    void applyShape (int shapeIndex);

    bool canUndo() const noexcept { return undoIndex > 0; }
    bool canRedo() const noexcept { return undoIndex + 1 < (int) undoStack.size(); }
    void undo();
    void redo();

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> plotBounds() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> toCurve (juce::Point<float> screen) const;
    float snapX (float x) const;

    int nodeAt (juce::Point<float> position) const;
    int segmentAt (juce::Point<float> position) const;

    void beginEdit();
    void commitEdit();
    void pushUndoState();
    void showContextMenu (juce::Point<int> position);

    static constexpr float kNodeRadius = 5.0f;
    static constexpr float kGrabRadius = 10.0f;
    static constexpr int   kMaxUndoStates = 64;

    CurveModel* model = nullptr;

    int gridDivisions = 16;
    bool snapEnabled = true;
    float playheadPhase = -1.0f;

    int draggingNode = -1;
    int draggingSegment = -1;
    float dragStartTension = 0.0f;
    juce::Point<float> dragStartPosition;
    int hoverNode = -1;
    int hoverSegment = -1;
    bool editInProgress = false;

    std::vector<std::vector<CurveNode>> undoStack;
    int undoIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};
} // namespace sf
