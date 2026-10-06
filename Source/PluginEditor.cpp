// SideForge™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace sf
{
SideForgeEditor::SideForgeEditor (SideForgeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lookAndFeel);

    // Built here rather than in the initialiser list so it can be handed `this`:
    // the host context menu is reached through the editor.
    mainView = std::make_unique<MainView> (p, this);
    addAndMakeVisible (*mainView);

    setResizable (true, true);

    // Free resizing, but never smaller than the point at which the panels stop
    // being legible.
    if (auto* limits = getConstrainer())
    {
        limits->setSizeLimits (kMinWidth, kMinHeight, 3840, 2400);
        limits->setFixedAspectRatio (0.0);
    }

    auto& editorState = proc.getEditorState();
    setSize (juce::jlimit (kMinWidth, 3840, editorState.width),
             juce::jlimit (kMinHeight, 2400, editorState.height));
}

SideForgeEditor::~SideForgeEditor()
{
    setLookAndFeel (nullptr);
}

void SideForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SideForgeEditor::resized()
{
    auto& editorState = proc.getEditorState();
    editorState.width = getWidth();
    editorState.height = getHeight();

    if (mainView == nullptr)
        return;

    // Scale rather than just stretch. The layout is authored at the design size;
    // once the window is larger than that in *both* directions the whole view is
    // drawn at a matching scale, so text and controls grow with it instead of
    // becoming a row of tiny knobs on a big monitor. Widening the window alone
    // leaves the scale at 1 and simply gives the panels more room to flow into.
    const auto scale = juce::jlimit (1.0f, 3.0f,
                                     juce::jmin ((float) getWidth()  / (float) kDesignWidth,
                                                 (float) getHeight() / (float) kDesignHeight));

    mainView->setTransform (scale > 1.0f ? juce::AffineTransform::scale (scale)
                                         : juce::AffineTransform());

    mainView->setBounds (0, 0,
                         juce::roundToInt ((float) getWidth()  / scale),
                         juce::roundToInt ((float) getHeight() / scale));
}
} // namespace sf
