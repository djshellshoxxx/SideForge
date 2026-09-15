#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CurveModel.h"

namespace sf
{
/** Non-automatable editor preferences that still belong in the session. */
struct EditorState
{
    int  width  = 1180;
    int  height = 760;
    int  openPanel = 0;     // index of the expanded lower panel, -1 = none
    bool freezeViz = false;
};

/**
    Versioned whole-plugin state.

    Layout:
      <SideForgeState version="1" name="...">
        <SideForgeParams .../>   <- APVTS
        <Curve>...</Curve>       <- CurveModel
        <Editor .../>            <- EditorState
      </SideForgeState>

    Anything unknown in a restored tree is ignored; anything malformed is replaced
    with the default, so a corrupt session can never crash or de-tune the plugin.
*/
namespace PresetState
{
/** 1 = the original full-range plugin.
    2 = adds the Band Matrix. Nothing from version 1 was renamed or repurposed,
        so a version-1 tree simply has no values for the new parameters and they
        take their defaults - which is Full Range, sounding exactly as before. */
inline constexpr int kStateVersion = 2;

extern const juce::Identifier rootTag;
extern const juce::Identifier paramsTag;
extern const juce::Identifier editorTag;
extern const juce::Identifier versionProp;
extern const juce::Identifier nameProp;

juce::ValueTree capture (juce::AudioProcessorValueTreeState& apvts,
                         const CurveModel& curve,
                         const EditorState& editor,
                         const juce::String& presetName = {});

/** Restores into the given targets. Returns false if the tree was unusable, in
    which case the targets are left untouched. */
bool restore (const juce::ValueTree& state,
              juce::AudioProcessorValueTreeState& apvts,
              CurveModel& curve,
              EditorState& editor);

/** Brings an older tree up to kStateVersion in place. Safe to call on any tree. */
void migrate (juce::ValueTree& state);

/** Removes non-finite / structurally invalid parameter entries. */
void sanitiseParameters (juce::ValueTree& params);
} // namespace PresetState
} // namespace sf
