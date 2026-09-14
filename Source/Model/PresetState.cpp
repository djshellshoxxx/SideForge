#include "PresetState.h"
#include <cmath>

namespace sf::PresetState
{
const juce::Identifier rootTag    { "SideForgeState" };
const juce::Identifier paramsTag  { "SideForgeParams" };
const juce::Identifier editorTag  { "Editor" };
const juce::Identifier versionProp{ "version" };
const juce::Identifier nameProp   { "name" };

namespace
{
const juce::Identifier kWidth { "width" }, kHeight { "height" };
const juce::Identifier kPanel { "openPanel" }, kFreeze { "freezeViz" };
const juce::Identifier kCurveTag { "Curve" };
const juce::Identifier kValueProp { "value" };
} // namespace

juce::ValueTree capture (juce::AudioProcessorValueTreeState& apvts,
                         const CurveModel& curve,
                         const EditorState& editor,
                         const juce::String& presetName)
{
    juce::ValueTree root { rootTag };
    root.setProperty (versionProp, kStateVersion, nullptr);

    if (presetName.isNotEmpty())
        root.setProperty (nameProp, presetName, nullptr);

    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (curve.toValueTree(), nullptr);

    juce::ValueTree ed { editorTag };
    ed.setProperty (kWidth,  editor.width,     nullptr);
    ed.setProperty (kHeight, editor.height,    nullptr);
    ed.setProperty (kPanel,  editor.openPanel, nullptr);
    ed.setProperty (kFreeze, editor.freezeViz, nullptr);
    root.appendChild (ed, nullptr);

    return root;
}

void sanitiseParameters (juce::ValueTree& params)
{
    if (! params.isValid())
        return;

    for (int i = params.getNumChildren(); --i >= 0;)
    {
        auto child = params.getChild (i);

        if (! child.hasProperty (kValueProp))
            continue;

        const auto v = (double) child.getProperty (kValueProp);

        if (! std::isfinite (v))
            params.removeChild (i, nullptr);   // parameter keeps its default
    }
}

void migrate (juce::ValueTree& state)
{
    if (! state.isValid() || ! state.hasType (rootTag))
        return;

    auto version = (int) state.getProperty (versionProp, 0);

    // Version 0 = pre-release trees written before the wrapper existed. They are
    // handled by the caller (which wraps a bare APVTS tree), so there is nothing
    // to do here yet. Future migrations chain from here:
    //
    //   if (version < 2) { ...rewrite...; version = 2; }

    if (version < kStateVersion)
        version = kStateVersion;

    state.setProperty (versionProp, version, nullptr);
}

bool restore (const juce::ValueTree& stateIn,
              juce::AudioProcessorValueTreeState& apvts,
              CurveModel& curve,
              EditorState& editor)
{
    if (! stateIn.isValid())
        return false;

    auto state = stateIn.createCopy();

    // Accept a bare APVTS tree (older builds / hosts that stored only that).
    if (state.hasType (paramsTag))
    {
        juce::ValueTree wrapper { rootTag };
        wrapper.setProperty (versionProp, 0, nullptr);
        wrapper.appendChild (state, nullptr);
        state = wrapper;
    }

    if (! state.hasType (rootTag))
        return false;

    migrate (state);

    auto params = state.getChildWithName (paramsTag);

    if (params.isValid())
    {
        sanitiseParameters (params);
        apvts.replaceState (params);
    }

    auto curveTree = state.getChildWithName (kCurveTag);

    if (curveTree.isValid())
    {
        if (! curve.fromValueTree (curveTree))
            curve.setPresetShape (0);
    }

    if (auto ed = state.getChildWithName (editorTag); ed.isValid())
    {
        editor.width     = juce::jlimit (820,  4096, (int) ed.getProperty (kWidth,  editor.width));
        editor.height    = juce::jlimit (560,  2600, (int) ed.getProperty (kHeight, editor.height));
        editor.openPanel = juce::jlimit (-1,     16, (int) ed.getProperty (kPanel,  editor.openPanel));
        editor.freezeViz = (bool) ed.getProperty (kFreeze, editor.freezeViz);
    }

    return true;
}
} // namespace sf::PresetState
