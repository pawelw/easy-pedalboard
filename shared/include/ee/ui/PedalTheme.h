#pragma once

#include <juce_graphics/juce_graphics.h>

#include <optional>

namespace ee::ui
{

/** Which family of controls a face is built from.

    `analog` is the original hardware look: photographic knob caps, lit bezel
    buttons, a value arc around every rotary. `digital` is the flat soft-UI
    look: white knobs ringed by a scale of ticks, pill switches and a pale
    recessed display. A theme picks one and every control follows it - the two
    are not mixed on one face. */
enum class ControlStyle
{
    analog,
    digital
};

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

    /** Frame drawn around the outer edge of the pedal. */
    juce::Colour bezel           { 0xff87ceef };

    juce::Colour textPrimary     { 0xfff2f2ea };
    juce::Colour textSecondary   { 0xff8f8f85 };

    juce::Colour knobBody        { 0xff2b2b27 };
    juce::Colour knobFill        { 0xff222222 };
    juce::Colour knobOutline     { 0xff4a4a43 };
    juce::Colour knobPointer     { 0xfff2f2ea };
    juce::Colour knobTrack       { 0xff33332e };

    juce::Colour accent          { 0xffc8e06a };
    juce::Colour accentDim       { 0xff5e6b34 };

    /** Halo colour for the value arcs and the lamp. */
    juce::Colour glow            { 0xffc8e06a };

    juce::Colour title           { 0xfff2f2ea };
    juce::Colour ledRing         { 0xff2a2724 };

    /** Speckle strength on the painted face. 0 = perfectly flat colour. */
    float grain = 0.0f;

    /** Which family of controls this face draws - see `ControlStyle`. */
    ControlStyle controlStyle = ControlStyle::analog;

    //== Soft-UI tokens. Only read when `controlStyle` is `digital`. =========

    /** The shadow a raised element (a knob, a switch knob, the face itself)
        casts down and to the right, and the light it catches up and to the
        left. The whole style is these two against `panel`. */
    juce::Colour softShadow      { 0x33000000 };
    juce::Colour softHighlight   { 0xffffffff };

    /** Fill of a recessed element: the display panel, a switch track that is
        off. What a hole in the face looks like. */
    juce::Colour recess          { 0xffdcdee3 };

    /** Ink on that recess - the display's grid and axis captions. */
    juce::Colour recessInk       { 0xff8b8f98 };

    juce::Colour ledOn           { 0xffc8e06a };
    juce::Colour ledOff          { 0xff35352f };
    juce::Colour switchBody      { 0xff454540 };
    juce::Colour switchHighlight { 0xff6e6e66 };

    float cornerRadius = 12.0f;
    float knobThickness = 3.0f;

    juce::String titleTypeface;  // empty = JUCE default
    juce::String bodyTypeface;

    /** Set to use an embedded face for the title, in preference to a name. */
    juce::Typeface::Ptr titleTypefacePtr;

    /** Optional artwork. Leave empty to use the vector drawing.

        Two ways to supply a knob, both alpha-aware:
          - knobImage: one upright PNG, rotated at runtime. Easiest to produce.
          - knobFilmstrip: frames stacked vertically, already rotated. Sharper
            for detailed caps, and the only option if the art is not radially
            symmetric under rotation.

        Whichever is set, the value arc is still drawn underneath, so artwork
        with a transparent background keeps its ring.
    */
    juce::Image backgroundImage;
    juce::Image knobImage;
    juce::Image knobFilmstrip;
    int knobFilmstripFrames = 0;

    /** Recolours the brand logo, keeping its shape. Leave unset to draw the
        artwork as it is. */
    std::optional<juce::Colour> logoTint;

    /** Set false if knobImage already carries its own pointer at 12 o'clock
        and should stay upright. */
    bool knobImageRotates = true;

    juce::Font titleFont (float height) const;
    juce::Font bodyFont (float height) const;

    static PedalTheme dark();
    static PedalTheme cream();
    static PedalTheme blue();
    static PedalTheme silver();
    static PedalTheme teal();
    static PedalTheme gold();
    static PedalTheme sky();
    static PedalTheme yellow();
    static PedalTheme orange();
    static PedalTheme pink();
    static PedalTheme green();
    static PedalTheme charcoal();
    static PedalTheme white();
    static PedalTheme moss();

    /** First installed name from the list, or empty for the JUCE default. */
    static juce::String pickTypeface (const juce::StringArray& preferred);
};

} // namespace ee::ui
