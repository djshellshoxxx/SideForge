#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <vector>

namespace sf
{
/** Resolution of the rendered curve lookup table.
    The audio thread never evaluates the node list directly: the message thread
    renders the model into a table and publishes it, so evaluation is a
    branch-free interpolated table read. */
inline constexpr int kCurveTableSize = 4096;

/** One rendered curve, plus a guard sample so interpolation never reads past the end. */
struct CurveTable
{
    std::array<float, kCurveTableSize + 1> samples {};

    /** phase in [0,1) -> value in [0,1]. Linear interpolation. */
    float lookup (double phase) const noexcept
    {
        phase -= std::floor (phase);
        const auto pos = phase * (double) kCurveTableSize;
        const auto i   = (int) pos;
        const auto f   = (float) (pos - (double) i);
        const auto a   = samples[(size_t) i];
        const auto b   = samples[(size_t) i + 1];
        return a + (b - a) * f;
    }
};

/** Shape of the segment that starts at a node.

    `Curve` with tension 0 is a straight ramp, which is why "straight", "ramp" and
    "curved" are all expressed by this one type plus its tension value. */
enum class SegmentType { Curve = 0, SCurve, Step };

struct CurveNode
{
    float       x = 0.0f;        // position in [0,1]
    float       y = 1.0f;        // value in [0,1]; 1 = unity gain, 0 = full attenuation
    float       tension = 0.0f;  // -1..1, shape of the segment starting here
    SegmentType type = SegmentType::Curve;
};

/**
    Editable curve. Message-thread only. Not real-time safe by design: the audio
    thread consumes `CurveTable`s published through `CurveTableExchange`.
*/
class CurveModel
{
public:
    CurveModel();

    // -- queries -------------------------------------------------------------
    int  getNumNodes() const noexcept                 { return (int) nodes.size(); }
    const CurveNode& getNode (int i) const noexcept   { return nodes[(size_t) i]; }
    const std::vector<CurveNode>& getNodes() const noexcept { return nodes; }

    /** Evaluates the curve at phase x in [0,1]. Used for rendering and tests. */
    float evaluate (float x) const noexcept;

    /** Renders the whole curve into a table (kCurveTableSize + 1 samples). */
    void renderTo (CurveTable& table) const;

    // -- editing -------------------------------------------------------------
    /** Inserts a node, keeping the list sorted. Returns the new node's index.
        The first and last nodes are pinned to x = 0 and x = 1. */
    int  addNode (float x, float y);
    void removeNode (int index);
    void moveNode (int index, float x, float y);
    void setTension (int index, float tension);
    void setSegmentType (int index, SegmentType type);
    void setNodes (std::vector<CurveNode> newNodes);
    void clear();

    /** Replaces the curve with a common starting shape. */
    void setPresetShape (int shapeIndex);
    static juce::StringArray getShapeNames();

    // -- serialisation -------------------------------------------------------
    juce::ValueTree toValueTree() const;
    /** Returns false and leaves the model untouched if the tree is unusable. */
    bool fromValueTree (const juce::ValueTree& tree);

    static constexpr int kMaxNodes = 64;

private:
    void sortAndClamp();

    std::vector<CurveNode> nodes;
};

/**
    Lock-free hand-off of rendered curves to the audio thread.

    Triple buffered: the message thread renders into a slot that the audio thread
    is provably not reading, then publishes it with a release store. The audio
    thread acquires the published index once per block.
*/
class CurveTableExchange
{
public:
    /** Message thread: render `model` and publish it. Allocates nothing. */
    void publish (const CurveModel& model);

    /** Audio thread: the most recently published table. Never null. */
    const CurveTable& acquire() noexcept
    {
        readIndex = published.load (std::memory_order_acquire);
        return tables[(size_t) readIndex];
    }

    /** Audio thread: table already acquired this block. */
    const CurveTable& current() const noexcept { return tables[(size_t) readIndex]; }

private:
    std::array<CurveTable, 3> tables {};
    std::atomic<int> published { 0 };
    int writeIndex = 0;
    int readIndex  = 0;
};
} // namespace sf
