#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sf::theme
{
// -- surfaces ---------------------------------------------------------------
inline const juce::Colour background   { 0xff0b0e14 };
inline const juce::Colour panel        { 0xff141a24 };
inline const juce::Colour panelRaised  { 0xff1b2330 };
inline const juce::Colour panelSunken  { 0xff0d1119 };
inline const juce::Colour outline      { 0xff2a3547 };
inline const juce::Colour outlineBright{ 0xff3d4c66 };

// -- text -------------------------------------------------------------------
inline const juce::Colour text         { 0xffd7e3f4 };
inline const juce::Colour textDim      { 0xff8798b0 };
inline const juce::Colour textFaint    { 0xff5c6b82 };

// -- the signal language the visualiser and the panels share ----------------
inline const juce::Colour destination  { 0xff3ad8ff };   // cyan: the track being processed
inline const juce::Colour destinationDeep { 0xff1173a8 };
inline const juce::Colour sidechain    { 0xffff8a3d };   // orange: the trigger source
inline const juce::Colour sidechainDeep { 0xffc23bb0 };  // magenta end of the same ramp
inline const juce::Colour collision    { 0xffffd447 };   // yellow -> red: where they fight
inline const juce::Colour collisionHot { 0xffff4d4d };
inline const juce::Colour reduction    { 0xffa974ff };   // violet: gain reduction
inline const juce::Colour output       { 0xff4ade80 };   // green: what comes out
inline const juce::Colour trigger      { 0xffffffff };

// -- states -----------------------------------------------------------------
inline const juce::Colour accent       = destination;
inline const juce::Colour warning      { 0xffffb020 };
inline const juce::Colour danger       { 0xffff5470 };
inline const juce::Colour ok           = output;
inline const juce::Colour learnActive  { 0xffff4da6 };

/** Slightly transparent version of a colour, for fills under a stroke. */
inline juce::Colour wash (juce::Colour c, float alpha) { return c.withAlpha (alpha); }

// -- type -------------------------------------------------------------------
inline juce::Font font (float height, bool bold = false)
{
    auto options = juce::FontOptions().withHeight (height);

    if (bold)
        options = options.withStyle ("Bold");

    return juce::Font (options);
}

/** Small-caps-ish section label. */
inline juce::Font labelFont (float height = 11.0f) { return font (height, true); }
inline juce::Font valueFont (float height = 13.0f) { return font (height); }

// -- metrics ----------------------------------------------------------------
inline constexpr float corner       = 6.0f;
inline constexpr float cornerSmall  = 4.0f;
inline constexpr int   gap          = 8;
inline constexpr int   gapSmall     = 4;
inline constexpr int   rowHeight    = 58;
inline constexpr int   headerHeight = 54;
inline constexpr int   footerHeight = 26;

/** The dB range the meters and the visualiser both draw. */
inline constexpr float meterFloorDb = -60.0f;
inline constexpr float meterTopDb   = 6.0f;

inline float dbToNorm (float db)
{
    return juce::jlimit (0.0f, 1.0f, (db - meterFloorDb) / (meterTopDb - meterFloorDb));
}
} // namespace sf::theme
