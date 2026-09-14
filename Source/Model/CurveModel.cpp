#include "CurveModel.h"
#include <algorithm>
#include <cmath>

namespace sf
{
namespace
{
constexpr float kMinNodeGap = 1.0e-4f;

/** Tension warp: 0 -> straight ramp, +/-1 -> strongly convex / concave. */
inline float warp (float t, float tension) noexcept
{
    if (std::abs (tension) < 1.0e-5f)
        return t;

    const auto k = tension * 8.0f;
    return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
}

/** Symmetric ease. tension 0 -> quadratic S, -1 -> near linear, +1 -> very steep. */
inline float sCurve (float t, float tension) noexcept
{
    const auto p = std::pow (2.0f, 1.0f + tension * 2.0f);
    return t < 0.5f ? 0.5f * std::pow (2.0f * t, p)
                    : 1.0f - 0.5f * std::pow (2.0f * (1.0f - t), p);
}

const juce::Identifier kCurveTag { "Curve" };
const juce::Identifier kNodeTag  { "Node" };
const juce::Identifier kX { "x" }, kY { "y" }, kT { "t" }, kS { "s" };
} // namespace

CurveModel::CurveModel()
{
    setPresetShape (0);
}

float CurveModel::evaluate (float x) const noexcept
{
    if (nodes.empty())
        return 1.0f;

    x = juce::jlimit (0.0f, 1.0f, x);

    if (nodes.size() == 1 || x <= nodes.front().x)
        return nodes.front().y;

    if (x >= nodes.back().x)
        return nodes.back().y;

    // Nodes are kept sorted, so a binary search is valid.
    size_t lo = 0, hi = nodes.size() - 1;
    while (hi - lo > 1)
    {
        const auto mid = (lo + hi) / 2;
        (nodes[mid].x <= x ? lo : hi) = mid;
    }

    const auto& a = nodes[lo];
    const auto& b = nodes[hi];
    const auto span = b.x - a.x;

    if (span <= kMinNodeGap)
        return b.y;

    const auto t = (x - a.x) / span;

    switch (a.type)
    {
        case SegmentType::Step:   return a.y;
        case SegmentType::SCurve: return a.y + (b.y - a.y) * sCurve (t, a.tension);
        case SegmentType::Curve:
        default:                  return a.y + (b.y - a.y) * warp (t, a.tension);
    }
}

void CurveModel::renderTo (CurveTable& table) const
{
    for (int i = 0; i <= kCurveTableSize; ++i)
        table.samples[(size_t) i] = juce::jlimit (0.0f, 1.0f,
                                                  evaluate ((float) i / (float) kCurveTableSize));
}

void CurveModel::sortAndClamp()
{
    for (auto& n : nodes)
    {
        n.x = juce::jlimit (0.0f, 1.0f, n.x);
        n.y = juce::jlimit (0.0f, 1.0f, n.y);
        n.tension = juce::jlimit (-1.0f, 1.0f, n.tension);
        if (n.type != SegmentType::Curve && n.type != SegmentType::SCurve && n.type != SegmentType::Step)
            n.type = SegmentType::Curve;
    }

    std::stable_sort (nodes.begin(), nodes.end(),
                      [] (const CurveNode& a, const CurveNode& b) { return a.x < b.x; });

    if (nodes.empty())
    {
        nodes.push_back ({ 0.0f, 1.0f, 0.0f, SegmentType::Curve });
        nodes.push_back ({ 1.0f, 1.0f, 0.0f, SegmentType::Curve });
        return;
    }

    if (nodes.size() == 1)
        nodes.push_back ({ 1.0f, nodes.front().y, 0.0f, SegmentType::Curve });

    nodes.front().x = 0.0f;
    nodes.back().x  = 1.0f;

    if ((int) nodes.size() > kMaxNodes)
        nodes.resize ((size_t) kMaxNodes);
}

int CurveModel::addNode (float x, float y)
{
    if ((int) nodes.size() >= kMaxNodes)
        return -1;

    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);

    // Inherit the shape of the segment being split so inserting a node does not
    // change the curve's character.
    auto type = SegmentType::Curve;
    for (size_t i = 0; i + 1 < nodes.size(); ++i)
        if (nodes[i].x <= x && x <= nodes[i + 1].x)
            type = nodes[i].type;

    nodes.push_back ({ x, y, 0.0f, type });
    sortAndClamp();

    for (size_t i = 0; i < nodes.size(); ++i)
        if (std::abs (nodes[i].x - x) < kMinNodeGap && std::abs (nodes[i].y - y) < kMinNodeGap)
            return (int) i;

    return -1;
}

void CurveModel::removeNode (int index)
{
    // The two endpoints define the curve's domain and cannot be removed.
    if (index <= 0 || index >= (int) nodes.size() - 1)
        return;

    nodes.erase (nodes.begin() + index);
    sortAndClamp();
}

void CurveModel::moveNode (int index, float x, float y)
{
    if (! juce::isPositiveAndBelow (index, (int) nodes.size()))
        return;

    const auto isFirst = (index == 0);
    const auto isLast  = (index == (int) nodes.size() - 1);

    if (isFirst)      x = 0.0f;
    else if (isLast)  x = 1.0f;
    else
    {
        const auto lo = nodes[(size_t) index - 1].x + kMinNodeGap;
        const auto hi = nodes[(size_t) index + 1].x - kMinNodeGap;
        x = juce::jlimit (lo, juce::jmax (lo, hi), x);
    }

    nodes[(size_t) index].x = x;
    nodes[(size_t) index].y = juce::jlimit (0.0f, 1.0f, y);
}

void CurveModel::setTension (int index, float tension)
{
    if (juce::isPositiveAndBelow (index, (int) nodes.size()))
        nodes[(size_t) index].tension = juce::jlimit (-1.0f, 1.0f, tension);
}

void CurveModel::setSegmentType (int index, SegmentType type)
{
    if (juce::isPositiveAndBelow (index, (int) nodes.size()))
        nodes[(size_t) index].type = type;
}

void CurveModel::setNodes (std::vector<CurveNode> newNodes)
{
    nodes = std::move (newNodes);
    sortAndClamp();
}

void CurveModel::clear()
{
    nodes.clear();
    sortAndClamp();
}

juce::StringArray CurveModel::getShapeNames()
{
    return { "Flat", "Classic Pump", "Sine", "Saw Down", "Saw Up",
             "Gate", "1/8 Gate", "Deep Duck" };
}

void CurveModel::setPresetShape (int shapeIndex)
{
    nodes.clear();
    const auto C = SegmentType::Curve;
    const auto S = SegmentType::SCurve;
    const auto T = SegmentType::Step;

    switch (shapeIndex)
    {
        case 1: // Classic pump: instant duck, curved recovery
            nodes = { { 0.0f, 0.0f, 0.55f, C }, { 0.85f, 1.0f, 0.0f, C }, { 1.0f, 1.0f, 0.0f, C } };
            break;

        case 2: // Sine-like dip
            nodes = { { 0.0f, 1.0f, 0.0f, S }, { 0.5f, 0.0f, 0.0f, S }, { 1.0f, 1.0f, 0.0f, S } };
            break;

        case 3: // Saw down
            nodes = { { 0.0f, 1.0f, 0.0f, C }, { 1.0f, 0.0f, 0.0f, C } };
            break;

        case 4: // Saw up
            nodes = { { 0.0f, 0.0f, 0.0f, C }, { 1.0f, 1.0f, 0.0f, C } };
            break;

        case 5: // Gate (half open, half closed)
            nodes = { { 0.0f, 1.0f, 0.0f, T }, { 0.5f, 0.0f, 0.0f, T }, { 1.0f, 0.0f, 0.0f, T } };
            break;

        case 6: // 1/8 gate: four on / off pairs across the cycle
            for (int i = 0; i < 8; ++i)
                nodes.push_back ({ (float) i / 8.0f, (i % 2 == 0) ? 1.0f : 0.0f, 0.0f, T });
            nodes.push_back ({ 1.0f, 1.0f, 0.0f, T });
            break;

        case 7: // Deep duck with a long tail
            nodes = { { 0.0f, 0.0f, 0.8f, C }, { 0.6f, 0.75f, 0.3f, C }, { 1.0f, 1.0f, 0.0f, C } };
            break;

        case 0:
        default:
            nodes = { { 0.0f, 1.0f, 0.0f, C }, { 1.0f, 1.0f, 0.0f, C } };
            break;
    }

    sortAndClamp();
}

juce::ValueTree CurveModel::toValueTree() const
{
    juce::ValueTree tree { kCurveTag };

    for (const auto& n : nodes)
    {
        juce::ValueTree nodeTree { kNodeTag };
        nodeTree.setProperty (kX, n.x, nullptr);
        nodeTree.setProperty (kY, n.y, nullptr);
        nodeTree.setProperty (kT, n.tension, nullptr);
        nodeTree.setProperty (kS, (int) n.type, nullptr);
        tree.appendChild (nodeTree, nullptr);
    }

    return tree;
}

bool CurveModel::fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType (kCurveTag) || tree.getNumChildren() < 1)
        return false;

    std::vector<CurveNode> parsed;
    parsed.reserve ((size_t) juce::jmin (tree.getNumChildren(), kMaxNodes));

    for (const auto& child : tree)
    {
        if (! child.hasType (kNodeTag))
            continue;

        CurveNode n;
        n.x = (float) (double) child.getProperty (kX, 0.0);
        n.y = (float) (double) child.getProperty (kY, 1.0);
        n.tension = (float) (double) child.getProperty (kT, 0.0);
        n.type = (SegmentType) (int) child.getProperty (kS, 0);

        if (std::isfinite (n.x) && std::isfinite (n.y) && std::isfinite (n.tension))
            parsed.push_back (n);

        if ((int) parsed.size() >= kMaxNodes)
            break;
    }

    if (parsed.size() < 2)
        return false;

    nodes = std::move (parsed);
    sortAndClamp();
    return true;
}

// ---------------------------------------------------------------------------

void CurveTableExchange::publish (const CurveModel& model)
{
    // Write to the slot that is neither currently published nor the one published
    // immediately before it, so the audio thread can safely hold either of those.
    const auto cur  = published.load (std::memory_order_relaxed);
    const auto prev = writeIndex;
    int slot = 0;
    for (int i = 0; i < 3; ++i)
        if (i != cur && i != prev)
            slot = i;

    model.renderTo (tables[(size_t) slot]);
    writeIndex = cur;
    published.store (slot, std::memory_order_release);
}
} // namespace sf
