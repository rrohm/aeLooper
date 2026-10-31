#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Central color scheme. The ONLY two values to change are the base and the
    accent color below; every other color in the UI is derived from them, so
    re-theming the app means editing those two constants.

    - base:   the general base color; backgrounds, borders, buttons, text,
              meters, waveform and LEDs are darker/lighter derivatives of it.
    - accent: the signal color, used ONLY where attention is required - here,
              only for the Mute button in its active (muted) state. In the
              inactive state the mute button uses a dark, faded derivative of
              the accent, close to the other field colors.
*/
struct ColorScheme
{
    // ---- the two poles: change only these ----
    static constexpr juce::uint32 baseColorValue   = 0xff4ed6c5;   ///< general base color (teal)
    static constexpr juce::uint32 accentColorValue = 0xffe35b;     ///< accent / signal color (yellow)

    static juce::Colour base()   { return juce::Colour(baseColorValue); }
    static juce::Colour accent() { return juce::Colour(accentColorValue); }

    // ---- derived colors ----
    static juce::Colour background()      { return base().darker(0.93f); }  ///< app background (dark)
    static juce::Colour fieldBackground() { return base().darker(0.88f); }  ///< label / status / number fields
    static juce::Colour border()          { return base().darker(0.60f); }  ///< outlines
    static juce::Colour text()            { return base().brighter(0.45f); }///< light text on dark fields
    static juce::Colour buttonFace()      { return base().brighter(0.10f); }///< light buttons
    static juce::Colour buttonText()      { return base().darker(0.90f); }  ///< dark text on buttons

    static juce::Colour waveform()        { return base().withAlpha(0.45f); }
    static juce::Colour waveformRedo()    { return base().withAlpha(0.22f); }///< undone/redoable layer

    static juce::Colour progress()        { return base().darker(0.45f); }  ///< status-bar progress fill
    static juce::Colour boxBorder()       { return base().withMultipliedBrightness(0.7f); }
    static juce::Colour downbeatBorder()  { return base().brighter(0.35f); }
    static juce::Colour currentBoxFill()  { return base().brighter(0.45f); }

    static juce::Colour led()             { return base(); }
    static juce::Colour ledDownbeat()     { return base().brighter(0.45f); }

    static juce::Colour meterLow()        { return base().withMultipliedBrightness(0.8f); }
    static juce::Colour meterMid()        { return base().brighter(0.25f); }
    static juce::Colour meterHigh()       { return base().brighter(0.55f); }

    static juce::Colour tick()            { return base().brighter(0.2f); }

    static juce::Colour muteActive()      { return accent(); }                ///< muted: full accent
    static juce::Colour muteInactive()    { return accent().darker(0.85f); }  ///< not muted: dark faded accent
};
