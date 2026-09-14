#pragma once
#include "ParameterContextMenu.h"
#include "Theme.h"

namespace sf
{
/**
    Behaviour every parameter control shares, so that right-clicking anything in
    the plugin does the same thing:

      - **Right-click** opens the parameter menu (host commands, MIDI learn,
        type / reset / copy / paste).
      - **Double-click** returns the control to its default.
      - **Shift-drag** is fine adjustment.
      - The control shows a small badge when a MIDI controller is bound to it,
        and pulses while it is waiting to learn one.
*/
class ParameterSlider : public juce::Slider
{
public:
    ParameterSlider (const juce::String& parameterIDToUse, const ControlContext& contextToUse);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onTextEntryRequested;

    const juce::String& getParameterID() const noexcept { return parameterID; }

private:
    static constexpr int kNormalSensitivity = 240;
    static constexpr int kFineSensitivity   = 1400;

    juce::String parameterID;
    ControlContext context;
};

/**
    A labelled rotary control: name above, arc in the middle, exact value below.

    The value readout is editable — double-click it, or choose "Type value" from
    the right-click menu — so every parameter can be dialled in numerically as
    well as by ear.
*/
class SFKnob : public juce::Component
{
public:
    SFKnob (const ControlContext&, const juce::String& parameterID,
            const juce::String& displayName = {}, const juce::String& tooltip = {});

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Right-clicking anywhere on the control - the arc, the name, the value
        readout or the padding between them - opens the same menu. */
    void mouseDown (const juce::MouseEvent&) override;

    /** Grey the control out when the active engine does not use it. */
    void setRelevant (bool isRelevant);

    /** Repaints the MIDI badge if it changed. Called from the editor's timer. */
    void refreshBadge();

    const juce::String& getParameterID() const noexcept { return parameterID; }

private:
    void openTextEntry();
    void commitTextEntry();
    void showMenu();

    ControlContext context;
    juce::String parameterID;
    juce::String displayName;

    ParameterSlider slider;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    juce::String badge;
    bool learning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFKnob)
};

/** A labelled combo box bound to a choice parameter. */
class SFChoice : public juce::Component
{
public:
    SFChoice (const ControlContext&, const juce::String& parameterID,
              const juce::String& displayName = {}, const juce::String& tooltip = {});

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void setRelevant (bool isRelevant);
    void refreshBadge();

    juce::ComboBox& getComboBox() noexcept { return box; }
    const juce::String& getParameterID() const noexcept { return parameterID; }

private:
    class Box : public juce::ComboBox
    {
    public:
        Box (SFChoice& ownerToUse) : owner (ownerToUse) {}
        void mouseDown (const juce::MouseEvent&) override;

    private:
        SFChoice& owner;
    };

    friend class Box;

    ControlContext context;
    juce::String parameterID;
    juce::String displayName;

    juce::Label nameLabel;
    Box box { *this };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    juce::String badge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFChoice)
};

/** A toggle bound to a bool parameter, drawn as a filled pill when on. */
class SFToggle : public juce::Component
{
public:
    SFToggle (const ControlContext&, const juce::String& parameterID,
              const juce::String& displayName = {}, const juce::String& tooltip = {},
              juce::Colour onColour = theme::accent);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void setRelevant (bool isRelevant);
    void refreshBadge();

    juce::Button& getButton() noexcept { return button; }
    const juce::String& getParameterID() const noexcept { return parameterID; }

private:
    class Button : public juce::TextButton
    {
    public:
        Button (SFToggle& ownerToUse) : owner (ownerToUse) {}
        void mouseDown (const juce::MouseEvent&) override;

    private:
        SFToggle& owner;
    };

    friend class Button;

    ControlContext context;
    juce::String parameterID;
    juce::Colour onColour;

    Button button { *this };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    juce::String badge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFToggle)
};

/**
    A horizontal row of mutually exclusive buttons bound to a choice parameter —
    used for Mode and Quality, where a drop-down would hide the options that
    matter most.
*/
class SFSegmentedControl : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::Timer
{
public:
    SFSegmentedControl (const ControlContext&, const juce::String& parameterID,
                        juce::Colour activeColour = theme::accent);
    ~SFSegmentedControl() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Disables one option and explains why in its tooltip. */
    void setOptionEnabled (int index, bool shouldBeEnabled, const juce::String& disabledTooltip = {});

    /** Called when the user picks an option that is currently disabled. */
    std::function<void (int)> onDisabledOptionClicked;

    const juce::String& getParameterID() const noexcept { return parameterID; }

private:
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    int  indexAt (juce::Point<int>) const;
    juce::Rectangle<float> boundsForIndex (int) const;

    ControlContext context;
    juce::String parameterID;
    juce::Colour activeColour;
    juce::StringArray options;
    std::vector<bool> optionEnabled;
    std::vector<juce::String> disabledTooltips;
    int hoverIndex = -1;
    int shownIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFSegmentedControl)
};
} // namespace sf
