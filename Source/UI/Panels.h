#pragma once
#include "Controls.h"
#include "../Parameters/MacroMapping.h"
#include <memory>
#include <vector>

namespace sf
{
/**
    Shared plumbing for the lower panels.

    Panels declare their controls in the constructor and get a flowing, wrapping
    layout with group headings for free, so adding a parameter is one line and
    never a layout problem. `setRelevance` is what lets a panel grey itself out
    when the active engine does not use it, instead of offering controls that
    would silently do nothing.
*/
class PanelBase : public juce::Component
{
public:
    PanelBase (const ControlContext&, juce::String title);
    ~PanelBase() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Updates every control's enabled state for the current engine setup. */
    virtual void updateRelevance (const EngineSettings&) {}

    /** Refreshes the MIDI badges on every control this panel owns. */
    void refreshBadges();

    const juce::String& getTitle() const noexcept { return title; }

    /** Height this panel needs at the given width. */
    int getPreferredHeight (int width) const;

protected:
    SFKnob&   addKnob (const juce::String& parameterID, const juce::String& name = {},
                       const juce::String& tooltip = {});
    SFChoice& addChoice (const juce::String& parameterID, const juce::String& name = {},
                         const juce::String& tooltip = {}, int width = 118);
    SFToggle& addToggle (const juce::String& parameterID, const juce::String& name = {},
                         const juce::String& tooltip = {},
                         juce::Colour onColour = theme::accent);
    void addGroup (const juce::String& name);

    /** The controls added since the last `addGroup`, so a subclass can show or
        hide a whole group at once. */
    std::vector<juce::Component*> componentsInLastGroup() const;

    /** Adds a component the panel owns but which is not a parameter control. */
    void addCustom (std::unique_ptr<juce::Component>, int width, int height);

    void setRelevance (juce::Component& control, bool isRelevant);

    ControlContext context;

private:
    struct Item
    {
        juce::Component* component = nullptr;
        int width = 0;
        int height = 0;
        int group = 0;
    };

    struct Group
    {
        juce::String name;
        juce::Rectangle<int> labelBounds;
    };

    /** Runs the flow layout; when `apply` is false it only measures. */
    int layout (int width, bool apply);

    static constexpr int kKnobWidth  = 68;
    static constexpr int kKnobHeight = 74;
    static constexpr int kRowGap     = 10;
    static constexpr int kGroupGap   = 18;

    juce::String title;
    std::vector<Item> items;
    std::vector<Group> groups;
    std::vector<std::unique_ptr<juce::Component>> owned;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelBase)
};

// ---------------------------------------------------------------------------

class CompressorPanel : public PanelBase
{
public:
    explicit CompressorPanel (const ControlContext&);
    void updateRelevance (const EngineSettings&) override;

private:
    std::vector<juce::Component*> controls;
};

class TriggerPanel : public PanelBase
{
public:
    TriggerPanel (const ControlContext&, std::function<void()> onTestTrigger);
    void updateRelevance (const EngineSettings&) override;

private:
    std::vector<juce::Component*> audioControls, midiControls;
};

class CurvePanel : public PanelBase
{
public:
    explicit CurvePanel (const ControlContext&);
    void updateRelevance (const EngineSettings&) override;

private:
    juce::Component* divisionControl = nullptr;
    juce::Component* rateControl = nullptr;
    std::vector<juce::Component*> triggeredOnlyControls;
    std::vector<juce::Component*> controls;
};

class SpectrumPanel : public PanelBase
{
public:
    explicit SpectrumPanel (const ControlContext&);
    void updateRelevance (const EngineSettings&) override;

private:
    std::vector<juce::Component*> controls;
};

class BandsPanel : public PanelBase
{
public:
    explicit BandsPanel (const ControlContext&);
    void updateRelevance (const EngineSettings&) override;

private:
    std::vector<juce::Component*> crossoverControls;
    std::vector<juce::Component*> upperBandControls;
    juce::Component* channelLinkControl = nullptr;
    juce::Component* monoLowFreqControl = nullptr;
};

class RoutingPanel : public PanelBase
{
public:
    explicit RoutingPanel (const ControlContext&);
    void updateRelevance (const EngineSettings&) override;

private:
    juce::Component* lookaheadControl = nullptr;
};

class OutputPanel : public PanelBase
{
public:
    explicit OutputPanel (const ControlContext&);
    void updateRelevance (const EngineSettings&) override;

private:
    juce::Component* autoGainControl = nullptr;
};
} // namespace sf
