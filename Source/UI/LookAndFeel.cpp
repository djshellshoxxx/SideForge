#include "LookAndFeel.h"

namespace sf
{
SideForgeLookAndFeel::SideForgeLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);

    setColour (juce::Slider::rotarySliderFillColourId,    theme::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, theme::outline);
    setColour (juce::Slider::thumbColourId,               theme::text);
    setColour (juce::Slider::trackColourId,               theme::accent);
    setColour (juce::Slider::backgroundColourId,          theme::panelSunken);
    setColour (juce::Slider::textBoxTextColourId,         theme::text);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,    theme::accent.withAlpha (0.3f));

    setColour (juce::Label::textColourId,                 theme::text);
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
    setColour (juce::Label::outlineWhenEditingColourId,   theme::accent);

    setColour (juce::TextButton::buttonColourId,          theme::panelRaised);
    setColour (juce::TextButton::buttonOnColourId,        theme::accent);
    setColour (juce::TextButton::textColourOffId,         theme::textDim);
    setColour (juce::TextButton::textColourOnId,          theme::background);

    setColour (juce::ToggleButton::textColourId,          theme::text);
    setColour (juce::ToggleButton::tickColourId,          theme::accent);
    setColour (juce::ToggleButton::tickDisabledColourId,  theme::outline);

    setColour (juce::ComboBox::backgroundColourId,        theme::panelSunken);
    setColour (juce::ComboBox::textColourId,              theme::text);
    setColour (juce::ComboBox::outlineColourId,           theme::outline);
    setColour (juce::ComboBox::arrowColourId,             theme::textDim);
    setColour (juce::ComboBox::buttonColourId,            juce::Colours::transparentBlack);

    setColour (juce::PopupMenu::backgroundColourId,       theme::panelRaised);
    setColour (juce::PopupMenu::textColourId,             theme::text);
    setColour (juce::PopupMenu::headerTextColourId,       theme::accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId,  theme::text);

    setColour (juce::TooltipWindow::backgroundColourId,   theme::panelRaised);
    setColour (juce::TooltipWindow::textColourId,         theme::text);
    setColour (juce::TooltipWindow::outlineColourId,      theme::outlineBright);

    setColour (juce::CaretComponent::caretColourId,       theme::accent);
    setColour (juce::TextEditor::backgroundColourId,      theme::panelSunken);
    setColour (juce::TextEditor::textColourId,            theme::text);
    setColour (juce::TextEditor::outlineColourId,         theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId,  theme::accent);
    setColour (juce::TextEditor::highlightColourId,       theme::accent.withAlpha (0.3f));
}

void SideForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto thickness = juce::jmax (2.5f, radius * 0.16f);
    const auto arcRadius = radius - thickness * 0.5f - 1.0f;

    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto enabled = slider.isEnabled();

    auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);

    if (! enabled)
        fill = fill.withSaturation (0.15f).withAlpha (0.35f);

    // Track.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc. Bipolar controls fill outwards from the centre, which is what
    // makes a "0 dB" or "no swing" setting read as neutral at a glance.
    const auto range = slider.getNormalisableRange();
    const auto isBipolar = range.start < 0.0 && range.end > 0.0
                        && std::abs (range.start + range.end) < std::abs (range.end) * 0.5;

    const auto originAngle = isBipolar
                                 ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
                                 : rotaryStartAngle;

    if (std::abs (angle - originAngle) > 1.0e-4f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);
        g.setColour (fill);
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Pointer.
    const auto pointerLength = arcRadius * 0.62f;
    const auto pointerInner = arcRadius * 0.18f;
    juce::Point<float> tip { centre.x + pointerLength * std::sin (angle),
                             centre.y - pointerLength * std::cos (angle) };
    juce::Point<float> root { centre.x + pointerInner * std::sin (angle),
                              centre.y - pointerInner * std::cos (angle) };

    g.setColour (enabled ? theme::text : theme::textFaint);
    g.drawLine ({ root, tip }, juce::jmax (1.5f, thickness * 0.5f));

    // A soft glow on the tip while the control is being dragged.
    if (slider.isMouseButtonDown())
    {
        g.setColour (fill.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (thickness * 2.0f, thickness * 2.0f).withCentre (tip));
    }
}

void SideForgeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto horizontal = (style == juce::Slider::LinearHorizontal
                          || style == juce::Slider::LinearBar);

    auto fill = slider.findColour (juce::Slider::trackColourId);

    if (! slider.isEnabled())
        fill = fill.withSaturation (0.15f).withAlpha (0.35f);

    const auto trackThickness = juce::jmin (6.0f, (horizontal ? bounds.getHeight() : bounds.getWidth()) * 0.35f);

    auto track = horizontal
                     ? bounds.withSizeKeepingCentre (bounds.getWidth(), trackThickness)
                     : bounds.withSizeKeepingCentre (trackThickness, bounds.getHeight());

    g.setColour (theme::panelSunken);
    g.fillRoundedRectangle (track, trackThickness * 0.5f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (track, trackThickness * 0.5f, 1.0f);

    auto filled = track;

    if (horizontal)
        filled = filled.withRight (sliderPos);
    else
        filled = filled.withTop (sliderPos);

    g.setColour (fill);
    g.fillRoundedRectangle (filled, trackThickness * 0.5f);

    const auto thumbSize = juce::jmax (10.0f, trackThickness * 2.0f);
    const juce::Point<float> thumbCentre { horizontal ? sliderPos : track.getCentreX(),
                                           horizontal ? track.getCentreY() : sliderPos };

    g.setColour (slider.isEnabled() ? theme::text : theme::textFaint);
    g.fillEllipse (juce::Rectangle<float> (thumbSize, thumbSize).withCentre (thumbCentre));
}

void SideForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawButtonAsHighlighted,
                                                 bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();

    auto fill = on ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.15f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.08f);

    if (! button.isEnabled())
        fill = fill.withSaturation (0.1f).withAlpha (0.4f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, theme::cornerSmall);

    g.setColour (on ? fill.brighter (0.3f) : theme::outline);
    g.drawRoundedRectangle (bounds, theme::cornerSmall, 1.0f);
}

void SideForgeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                             bool shouldDrawButtonAsHighlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin (16.0f, bounds.getHeight() - 2.0f);
    const auto box = juce::Rectangle<float> (boxSize, boxSize)
                         .withCentre ({ bounds.getX() + boxSize * 0.5f + 1.0f, bounds.getCentreY() });

    const auto on = button.getToggleState();
    const auto enabled = button.isEnabled();

    g.setColour (on ? (enabled ? theme::accent : theme::accent.withAlpha (0.35f)) : theme::panelSunken);
    g.fillRoundedRectangle (box, theme::cornerSmall);

    g.setColour (shouldDrawButtonAsHighlighted ? theme::outlineBright : theme::outline);
    g.drawRoundedRectangle (box, theme::cornerSmall, 1.0f);

    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.26f, box.getCentreY());
        tick.lineTo (box.getCentreX() - box.getWidth() * 0.02f, box.getBottom() - box.getHeight() * 0.28f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.28f);

        g.setColour (theme::background);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    const auto textArea = bounds.withLeft (box.getRight() + 6.0f);

    if (textArea.getWidth() > 4.0f)
    {
        g.setColour (enabled ? button.findColour (juce::ToggleButton::textColourId) : theme::textFaint);
        g.setFont (theme::valueFont (12.0f));
        g.drawText (button.getButtonText(), textArea, juce::Justification::centredLeft, true);
    }
}

void SideForgeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    const auto enabled = box.isEnabled();

    g.setColour (enabled ? theme::panelSunken : theme::panelSunken.withAlpha (0.5f));
    g.fillRoundedRectangle (bounds, theme::cornerSmall);

    g.setColour (box.isMouseOver() && enabled ? theme::outlineBright : theme::outline);
    g.drawRoundedRectangle (bounds, theme::cornerSmall, 1.0f);

    juce::Path arrow;
    const auto cx = bounds.getRight() - 11.0f;
    const auto cy = bounds.getCentreY();
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);

    g.setColour (enabled ? theme::textDim : theme::textFaint);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void SideForgeLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (7, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font SideForgeLookAndFeel::getComboBoxFont (juce::ComboBox&)  { return theme::valueFont (12.0f); }
juce::Font SideForgeLookAndFeel::getPopupMenuFont()                 { return theme::valueFont (13.0f); }
juce::Font SideForgeLookAndFeel::getLabelFont (juce::Label& label)  { return label.getFont(); }

juce::Font SideForgeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::font (juce::jlimit (10.0f, 14.0f, (float) buttonHeight * 0.46f), true);
}

void SideForgeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);

    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (bounds, theme::corner);
    g.setColour (theme::outlineBright);
    g.drawRoundedRectangle (bounds, theme::corner, 1.0f);
}

void SideForgeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text,
                                        int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);

    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (bounds, theme::cornerSmall);
    g.setColour (theme::outlineBright);
    g.drawRoundedRectangle (bounds, theme::cornerSmall, 1.0f);

    juce::AttributedString attributed;
    attributed.setJustification (juce::Justification::centredLeft);
    attributed.append (text, theme::valueFont (12.0f), theme::text);
    attributed.setLineSpacing (2.0f);
    attributed.draw (g, bounds.reduced (8.0f, 5.0f));
}

juce::Rectangle<int> SideForgeLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                             juce::Point<int> screenPos,
                                                             juce::Rectangle<int> parentArea)
{
    juce::AttributedString attributed;
    attributed.setJustification (juce::Justification::centredLeft);
    attributed.append (tipText, theme::valueFont (12.0f));

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (attributed, 360.0f);

    const auto w = (int) std::ceil (layout.getWidth()) + 18;
    const auto h = (int) std::ceil (layout.getHeight()) + 12;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 18,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
        .constrainedWithin (parentArea);
}

void SideForgeLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                      juce::Colour fill, juce::Colour border)
{
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, theme::corner);
    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), theme::corner, 1.0f);
}

void SideForgeLookAndFeel::drawSectionHeading (juce::Graphics& g, juce::Rectangle<int> bounds,
                                               const juce::String& text, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (theme::labelFont (11.0f));

    const auto textWidth = juce::GlyphArrangement::getStringWidthInt (theme::labelFont (11.0f),
                                                                     text.toUpperCase());
    g.drawText (text.toUpperCase(), bounds, juce::Justification::centredLeft, false);

    const auto lineX = bounds.getX() + textWidth + 8;

    if (lineX < bounds.getRight() - 4)
    {
        g.setColour (theme::outline);
        g.fillRect (lineX, bounds.getCentreY(), bounds.getRight() - lineX, 1);
    }
}
} // namespace sf
