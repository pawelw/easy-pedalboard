#pragma once

#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <vector>

namespace ee::ui
{

/** For a corner knob that trims one end of the spectrum: which side of the
    fader grid its "removed" shading grows from. `low` grows from the left as
    the knob turns up; `high` grows from the right as the knob turns down. */
enum class CutSide { none, low, high };

struct KnobSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Override the theme's cap colours, for a control that is not really part
        of the same effect as the rest of the row. */
    std::optional<juce::Colour> capFill;
    std::optional<juce::Colour> capBorder;

    /** Override the value arc and its halo to match. */
    std::optional<juce::Colour> arc;

    /** Smaller cap and value readout, no caption. For utility knobs tucked into
        a corner. */
    bool compact = false;

    /** If set, this knob shades one side of the fader grid to show the band it
        is removing. Only meaningful for `cornerKnobs`. */
    CutSide cutSide = CutSide::none;

    /** Draw the value ring full and white, with the arc growing backward from
        the maximum as the knob turns down - for a control whose resting place
        is the top of its range (a high cut that idles wide open). */
    bool invertedArc = false;
};

/** A vertical fader plus the value readout and caption underneath it.

    Faders lay out in a single row across the control area, below any knob rows.
    A pedal built from faders alone (a graphic EQ) gives the whole area over to
    them.
*/
struct SliderSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Override the theme accent for this fader's filled track. */
    std::optional<juce::Colour> fill;

    /** Whether the response curve passes through this fader's node. A make-up
        level fader sits in the row but is not part of the frequency response,
        so it is left out of the curve. */
    bool joinCurve = true;

    /** Centre frequency this fader represents, in Hz. Lets the editor place the
        curve on a frequency axis and bend it where the cut knobs roll off.
        0 = not on the frequency axis. */
    float axisHz = 0.0f;
};

/** A small latching button tucked into the gap between two knobs of a row. */
struct ToggleSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Index into `knobs`; the button sits to the right of that knob. Ignored
        if the next knob starts a new row. */
    int afterKnobIndex = 0;

    /** Colour of the bezel and legend while the button is on. Unset falls back
        to the theme's glow. */
    std::optional<juce::Colour> litColour;
};

/** Declarative description of a pedal face. Add an effect by writing one of these. */
struct PedalSpec
{
    juce::String name;
    juce::String tagline;
    juce::String version;
    std::vector<KnobSpec> knobs;

    /** Small knobs pinned to the top-right, above the main control area. Not
        part of the knob-row layout. */
    std::vector<KnobSpec> cornerKnobs;

    std::vector<SliderSpec> sliders;
    std::vector<ToggleSpec> toggles;

    int knobsPerRow = 3;

    /** Height is shared across pedals so they line up side by side on a rack;
        only the width follows the number of knobs in a row. */
    int width = 340;
    int height = 478;
};

} // namespace ee::ui
