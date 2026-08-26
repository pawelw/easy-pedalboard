#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ee::ui
{

/** All colours, metrics and optional artwork for a pedal face.

    Everything the look and feel draws comes from here. To swap in real graphics
    later, populate `backgroundImage` / `knobFilmstrip` and the vector fallbacks
    stop being used - no layout code changes.
*/
struct PedalTheme
{
    juce::Colour background      { 0xff1c1c1a };
    juce::Colour panel           { 0xff232320 };
    juce::Colour outline         { 0xff3a3a35 };
    juce::Colour textPrimary     { 0xfff2f2ea };
    juce::Colour textSecondary   { 0xff8f8f85 };

    juce::Colour knobBody        { 0xff2b2b27 };
    juce::Colour knobOutline     { 0xff4a4a43 };
    juce::Colour knobPointer     { 0xfff2f2ea };
    juce::Colour knobTrack       { 0xff33332e };

    juce::Colour accent          { 0xffc8e06a };
    juce::Colour accentDim       { 0xff5e6b34 };

    juce::Colour ledOn           { 0xffc8e06a };
    juce::Colour ledOff          { 0xff35352f };
    juce::Colour switchBody      { 0xff454540 };
    juce::Colour switchHighlight { 0xff6e6e66 };

    float cornerRadius = 12.0f;
    float knobThickness = 3.0f;

    juce::String titleTypeface;  // empty = JUCE default
    juce::String bodyTypeface;

    /** Optional artwork. Leave empty to use the vector drawing. */
    juce::Image backgroundImage;
    juce::Image knobFilmstrip;
    int knobFilmstripFrames = 0;

    juce::Font titleFont (float height) const;
    juce::Font bodyFont (float height) const;

    static PedalTheme dark();
};

} // namespace ee::ui
