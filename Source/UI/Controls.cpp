#include "Controls.h"
#include "LookAndFeel.h"

namespace sf
{
namespace
{
juce::String nameOf (juce::RangedAudioParameter* parameter, const juce::String& fallback)
{
    if (fallback.isNotEmpty())
        return fallback;

    return parameter != nullptr ? parameter->getName (32) : juce::String ("--");
}

/** The tooltip every control gets, with the interactions spelled out once. */
juce::String buildTooltip (juce::RangedAudioParameter* parameter, const juce::String& extra)
{
    juce::StringArray lines;

    if (parameter != nullptr)
        lines.add (parameter->getName (64));

    if (extra.isNotEmpty())
        lines.add (extra);

    lines.add ("Drag to adjust  -  Shift-drag for fine  -  Double-click to reset");
    lines.add ("Right-click for MIDI learn and more");

    return lines.joinIntoString ("\n");
}

void paintBadge (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text,
                 bool learning)
{
    if (text.isEmpty())
        return;

    const auto font = theme::labelFont (9.0f);
    const auto width = juce::GlyphArrangement::getStringWidthInt (font, text) + 8;
    const auto badge = area.removeFromRight (width).withHeight (12).toFloat();

    g.setColour (learning ? theme::learnActive : theme::accent.withAlpha (0.8f));
    g.fillRoundedRectangle (badge, 3.0f);

    g.setColour (theme::background);
    g.setFont (font);
    g.drawText (text, badge, juce::Justification::centred, false);
}
} // namespace

// ===========================================================================
//  ParameterSlider
// ===========================================================================

ParameterSlider::ParameterSlider (const juce::String& parameterIDToUse,
                                  const ControlContext& contextToUse)
    : parameterID (parameterIDToUse), context (contextToUse)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                         juce::MathConstants<float>::pi * 2.8f, true);
    setMouseDragSensitivity (kNormalSensitivity);
    setWantsKeyboardFocus (false);

    if (auto* parameter = context.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setVelocityBasedMode (false);
    }
}

void ParameterSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (auto* parameter = context.getParameter (parameterID))
        {
            auto* self = this;
            ParameterMenu::show (*this, *parameter, context,
                                 [self] { if (self->onTextEntryRequested) self->onTextEntryRequested(); },
                                 [self] { self->repaint(); });
        }

        return;
    }

    setMouseDragSensitivity (e.mods.isShiftDown() ? kFineSensitivity : kNormalSensitivity);
    juce::Slider::mouseDown (e);
}

void ParameterSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    // Re-read the modifier every drag so fine mode can be engaged mid-gesture.
    setMouseDragSensitivity (e.mods.isShiftDown() ? kFineSensitivity : kNormalSensitivity);
    juce::Slider::mouseDrag (e);
}

void ParameterSlider::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    juce::Slider::mouseUp (e);
    setMouseDragSensitivity (kNormalSensitivity);
}

// ===========================================================================
//  SFKnob
// ===========================================================================

SFKnob::SFKnob (const ControlContext& contextToUse, const juce::String& parameterIDToUse,
                const juce::String& displayNameToUse, const juce::String& tooltip)
    : context (contextToUse),
      parameterID (parameterIDToUse),
      slider (parameterIDToUse, contextToUse)
{
    auto* parameter = context.getParameter (parameterID);
    displayName = nameOf (parameter, displayNameToUse);

    jassert (parameter != nullptr);   // a control with no parameter behind it is a bug

    addAndMakeVisible (slider);
    slider.setTooltip (buildTooltip (parameter, tooltip));

    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (theme::labelFont (10.0f));
    nameLabel.setColour (juce::Label::textColourId, theme::textDim);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (theme::valueFont (11.0f));
    valueLabel.setColour (juce::Label::textColourId, theme::text);
    valueLabel.setEditable (false, true, false);   // double-click the readout to type
    valueLabel.setTooltip (buildTooltip (parameter, tooltip));
    addAndMakeVisible (valueLabel);

    valueLabel.onEditorHide = [this] { commitTextEntry(); };
    slider.onTextEntryRequested = [this] { openTextEntry(); };

    // The labels and the gaps around them are part of the control as far as the
    // user is concerned, so a right-click on any of it opens the same menu.
    nameLabel.addMouseListener (this, false);
    valueLabel.addMouseListener (this, false);

    slider.onValueChange = [this]
    {
        if (auto* p = context.getParameter (parameterID))
            valueLabel.setText (p->getCurrentValueAsText(), juce::dontSendNotification);
    };

    if (context.apvts != nullptr && parameter != nullptr)
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            *context.apvts, parameterID, slider);

    if (parameter != nullptr)
        valueLabel.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
}

void SFKnob::showMenu()
{
    if (auto* parameter = context.getParameter (parameterID))
        ParameterMenu::show (*this, *parameter, context,
                             [this] { openTextEntry(); },
                             [this] { repaint(); });
}

void SFKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
}

void SFKnob::openTextEntry()
{
    valueLabel.showEditor();

    if (auto* editor = valueLabel.getCurrentTextEditor())
    {
        if (auto* parameter = context.getParameter (parameterID))
            editor->setText (parameter->getCurrentValueAsText()
                                 .retainCharacters ("0123456789.,-+")
                                 .trim(),
                             false);

        editor->selectAll();
    }
}

void SFKnob::commitTextEntry()
{
    auto* parameter = context.getParameter (parameterID);

    if (parameter == nullptr)
        return;

    const auto typed = valueLabel.getText().trim();

    // Let the parameter interpret the text, so "-6 dB", "1/4" and "2:1" all work
    // wherever they make sense; fall back to the plain number otherwise.
    auto normalised = parameter->getValueForText (typed);

    if (! std::isfinite (normalised))
        normalised = parameter->getValue();

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    parameter->endChangeGesture();

    valueLabel.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
}

void SFKnob::setRelevant (bool isRelevant)
{
    if (slider.isEnabled() == isRelevant)
        return;

    slider.setEnabled (isRelevant);
    valueLabel.setEnabled (isRelevant);
    nameLabel.setColour (juce::Label::textColourId, isRelevant ? theme::textDim : theme::textFaint);
    valueLabel.setColour (juce::Label::textColourId, isRelevant ? theme::text : theme::textFaint);
    repaint();
}

void SFKnob::refreshBadge()
{
    const auto next = ParameterMenu::midiBadgeText (parameterID, context);
    const auto nextLearning = ParameterMenu::isLearning (parameterID, context);

    if (next != badge || nextLearning != learning)
    {
        badge = next;
        learning = nextLearning;
        repaint();
    }
}

void SFKnob::paint (juce::Graphics& g)
{
    paintBadge (g, getLocalBounds().removeFromTop (12).reduced (2, 0), badge, learning);
}

void SFKnob::resized()
{
    auto area = getLocalBounds();
    nameLabel.setBounds (area.removeFromTop (13));
    valueLabel.setBounds (area.removeFromBottom (14));
    slider.setBounds (area.reduced (2));
}

// ===========================================================================
//  SFChoice
// ===========================================================================

void SFChoice::Box::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (auto* parameter = owner.context.getParameter (owner.parameterID))
        {
            auto* target = &owner;
            ParameterMenu::show (*this, *parameter, owner.context, {},
                                 [target] { target->repaint(); });
        }

        return;
    }

    juce::ComboBox::mouseDown (e);
}

SFChoice::SFChoice (const ControlContext& contextToUse, const juce::String& parameterIDToUse,
                    const juce::String& displayNameToUse, const juce::String& tooltip)
    : context (contextToUse), parameterID (parameterIDToUse)
{
    auto* parameter = context.getParameter (parameterID);
    displayName = nameOf (parameter, displayNameToUse);

    jassert (parameter != nullptr);

    nameLabel.setText (displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setFont (theme::labelFont (10.0f));
    nameLabel.setColour (juce::Label::textColourId, theme::textDim);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    addAndMakeVisible (box);
    box.setTooltip (buildTooltip (parameter, tooltip));

    // ComboBoxAttachment matches on item index and does not add the items
    // itself, so the choices have to be populated before it is created.
    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*> (parameter))
        box.addItemList (choiceParameter->choices, 1);

    if (context.apvts != nullptr && parameter != nullptr)
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            *context.apvts, parameterID, box);
}

void SFChoice::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    if (auto* parameter = context.getParameter (parameterID))
        ParameterMenu::show (*this, *parameter, context, {}, [this] { repaint(); });
}

void SFChoice::setRelevant (bool isRelevant)
{
    if (box.isEnabled() == isRelevant)
        return;

    box.setEnabled (isRelevant);
    nameLabel.setColour (juce::Label::textColourId, isRelevant ? theme::textDim : theme::textFaint);
    repaint();
}

void SFChoice::refreshBadge()
{
    const auto next = ParameterMenu::midiBadgeText (parameterID, context);

    if (next != badge)
    {
        badge = next;
        repaint();
    }
}

void SFChoice::paint (juce::Graphics& g)
{
    paintBadge (g, getLocalBounds().removeFromTop (12), badge,
                ParameterMenu::isLearning (parameterID, context));
}

void SFChoice::resized()
{
    auto area = getLocalBounds();
    nameLabel.setBounds (area.removeFromTop (13));
    box.setBounds (area.reduced (0, 1));
}

// ===========================================================================
//  SFToggle
// ===========================================================================

void SFToggle::Button::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (auto* parameter = owner.context.getParameter (owner.parameterID))
        {
            auto* target = &owner;
            ParameterMenu::show (*this, *parameter, owner.context, {},
                                 [target] { target->repaint(); });
        }

        return;
    }

    juce::TextButton::mouseDown (e);
}

SFToggle::SFToggle (const ControlContext& contextToUse, const juce::String& parameterIDToUse,
                    const juce::String& displayNameToUse, const juce::String& tooltip,
                    juce::Colour onColourToUse)
    : context (contextToUse), parameterID (parameterIDToUse), onColour (onColourToUse)
{
    auto* parameter = context.getParameter (parameterID);
    jassert (parameter != nullptr);

    button.setButtonText (nameOf (parameter, displayNameToUse));
    button.setClickingTogglesState (true);
    button.setTooltip (buildTooltip (parameter, tooltip));
    button.setColour (juce::TextButton::buttonOnColourId, onColour);
    button.setColour (juce::TextButton::textColourOnId, theme::background);
    addAndMakeVisible (button);

    if (context.apvts != nullptr && parameter != nullptr)
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            *context.apvts, parameterID, button);
}

void SFToggle::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    if (auto* parameter = context.getParameter (parameterID))
        ParameterMenu::show (*this, *parameter, context, {}, [this] { repaint(); });
}

void SFToggle::setRelevant (bool isRelevant)
{
    if (button.isEnabled() != isRelevant)
    {
        button.setEnabled (isRelevant);
        repaint();
    }
}

void SFToggle::refreshBadge()
{
    const auto next = ParameterMenu::midiBadgeText (parameterID, context);

    if (next != badge)
    {
        badge = next;
        repaint();
    }
}

void SFToggle::paint (juce::Graphics& g)
{
    paintBadge (g, getLocalBounds().removeFromTop (12).reduced (2, 0), badge,
                ParameterMenu::isLearning (parameterID, context));
}

void SFToggle::resized()
{
    button.setBounds (getLocalBounds());
}

// ===========================================================================
//  SFSegmentedControl
// ===========================================================================

SFSegmentedControl::SFSegmentedControl (const ControlContext& contextToUse,
                                        const juce::String& parameterIDToUse,
                                        juce::Colour activeColourToUse)
    : context (contextToUse), parameterID (parameterIDToUse), activeColour (activeColourToUse)
{
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (context.getParameter (parameterID)))
        options = parameter->choices;

    optionEnabled.assign ((size_t) juce::jmax (1, options.size()), true);
    disabledTooltips.assign ((size_t) juce::jmax (1, options.size()), juce::String());

    setWantsKeyboardFocus (false);
    startTimerHz (20);
}

SFSegmentedControl::~SFSegmentedControl() { stopTimer(); }

void SFSegmentedControl::setOptionEnabled (int index, bool shouldBeEnabled,
                                           const juce::String& disabledTooltip)
{
    if (! juce::isPositiveAndBelow (index, (int) optionEnabled.size()))
        return;

    if (optionEnabled[(size_t) index] == shouldBeEnabled)
        return;

    optionEnabled[(size_t) index] = shouldBeEnabled;
    disabledTooltips[(size_t) index] = disabledTooltip;
    repaint();
}

void SFSegmentedControl::timerCallback()
{
    auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (context.getParameter (parameterID));
    const auto current = parameter != nullptr ? parameter->getIndex() : 0;

    if (current != shownIndex)
    {
        shownIndex = current;
        repaint();
    }
}

juce::Rectangle<float> SFSegmentedControl::boundsForIndex (int index) const
{
    const auto count = juce::jmax (1, options.size());
    const auto width = (float) getWidth() / (float) count;

    return { width * (float) index, 0.0f, width, (float) getHeight() };
}

int SFSegmentedControl::indexAt (juce::Point<int> position) const
{
    const auto count = juce::jmax (1, options.size());
    const auto width = (float) getWidth() / (float) count;

    return juce::jlimit (0, count - 1, (int) ((float) position.x / juce::jmax (1.0f, width)));
}

void SFSegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    auto* parameter = context.getParameter (parameterID);

    if (parameter == nullptr)
        return;

    if (e.mods.isPopupMenu())
    {
        auto* self = this;
        ParameterMenu::show (*this, *parameter, context, {}, [self] { self->repaint(); });
        return;
    }

    const auto index = indexAt (e.getPosition());

    if (! optionEnabled[(size_t) index])
    {
        // Rather than silently ignoring the click, tell the owner so it can put
        // the plugin into a state where the option makes sense.
        if (onDisabledOptionClicked != nullptr)
            onDisabledOptionClicked (index);

        return;
    }

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
    parameter->endChangeGesture();
}

void SFSegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());

    if (index != hoverIndex)
    {
        hoverIndex = index;

        if (! optionEnabled[(size_t) index] && disabledTooltips[(size_t) index].isNotEmpty())
            setTooltip (disabledTooltips[(size_t) index]);
        else
            setTooltip ("Right-click for MIDI learn and more");

        repaint();
    }
}

void SFSegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hoverIndex = -1;
    repaint();
}

void SFSegmentedControl::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Read the selection here rather than trusting the timer's cached copy, so a
    // paint triggered by anything else still shows the right option.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (context.getParameter (parameterID)))
        shownIndex = parameter->getIndex();

    g.setColour (theme::panelSunken);
    g.fillRoundedRectangle (bounds, theme::cornerSmall);

    for (int i = 0; i < options.size(); ++i)
    {
        const auto segment = boundsForIndex (i).reduced (2.0f);
        const auto selected = (i == shownIndex);
        const auto enabled = optionEnabled[(size_t) i];

        if (selected)
        {
            g.setColour (enabled ? activeColour : activeColour.withAlpha (0.35f));
            g.fillRoundedRectangle (segment, theme::cornerSmall - 1.0f);
        }
        else if (i == hoverIndex && enabled)
        {
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (segment, theme::cornerSmall - 1.0f);
        }

        g.setColour (! enabled ? theme::textFaint
                     : selected ? theme::background
                                : theme::textDim);
        g.setFont (theme::labelFont (11.0f));
        g.drawText (options[i], segment, juce::Justification::centred, false);
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerSmall, 1.0f);
}

void SFSegmentedControl::resized() {}
} // namespace sf
