#pragma once
#include "Theme.h"

namespace sf
{
/**
    The plugin's visual language in one place.

    Flat, high-contrast and legible rather than skeuomorphic: an arc for the
    value, a thin track behind it, and no attempt to look like a piece of
    hardware. Every colour comes from `sf::theme` so the visualiser and the
    controls agree about what cyan, orange and violet mean.
*/
class SideForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SideForgeLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    /** Draws the standard sunken container every panel sits in. */
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds,
                           juce::Colour fill = theme::panel,
                           juce::Colour border = theme::outline);

    /** Section heading with a hairline rule running off to the right. */
    static void drawSectionHeading (juce::Graphics&, juce::Rectangle<int> bounds,
                                    const juce::String& text,
                                    juce::Colour colour = theme::textDim);
};
} // namespace sf
