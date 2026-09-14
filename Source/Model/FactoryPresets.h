#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CurveModel.h"

namespace sf::FactoryPresets
{
int getNumPresets();
juce::String getName (int index);
juce::String getDescription (int index);
juce::StringArray getNames();

/** Message thread. Resets every parameter to its default, then applies the
    preset's overrides and curve shape, so a preset is always a complete state
    rather than a diff against whatever happened to be loaded. */
void apply (int index, juce::AudioProcessorValueTreeState& apvts, CurveModel& curve);
} // namespace sf::FactoryPresets
