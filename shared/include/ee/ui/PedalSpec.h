#pragma once

#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <optional>
#include <vector>

namespace ee::ui
{

//==============================================================================
// Shared face metrics, so every pedal spaces its knob columns identically
// whatever the row count. These match Easy Reverb's original layout.

/** Frame inset + content padding: the margin every control keeps from the edge. */
inline constexpr int kFaceContentMargin = 21;

/** One knob column, cap plus the slack that sets the gap to its neighbour. */
inline constexpr int kKnobCellWidth = 143;

/** Gap between knob columns. */
inline constexpr int kKnobColumnGap = 12;

/** Face width that gives `knobsPerRow` columns that shared spacing. Every pedal
    with a knob row should use this rather than a hand-picked width. */
inline constexpr int knobRowWidth (int knobsPerRow)
{
    return knobsPerRow <= 0
             ? 2 * kFaceContentMargin
             : 2 * kFaceContentMargin
                   + knobsPerRow * kKnobCellWidth
                   + (knobsPerRow - 1) * kKnobColumnGap;
}

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

    /** When set, this replaces the parameter's own text in the value readout.
        Re-queried on every value change, and whenever a parent calls
        `Knob::refreshValueText()` - so a control whose unit depends on another
        switch (a rate that reads note values or milliseconds) can be kept in
        step by re-poking it when that switch moves. */
    std::function<juce::String()> liveValueText;
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

/** A big sliding two-way switch in a strip above the knob rows: a label on each
    side of a dark track with a light knob that sits left (off) or right (on).
    Latching, bound to a bool parameter. Pinned to the top-left. */
struct SlideToggleSpec
{
    juce::String parameterID;

    /** Left label (parameter false) and right label (parameter true). */
    juce::String labelOff;
    juce::String labelOn;

    /** Colour of the knob and the active label. Unset falls back to the theme
        title colour. */
    std::optional<juce::Colour> accent;
};

/** An LFO waveform preview, drawn in a band between the knob row and the pedal
    name. Reads the live parameter values so the trace tracks the knobs. */
struct WaveDisplaySpec
{
    juce::String amountID;   // 0..100  -> trace amplitude
    juce::String rateID;     // 0..1    -> number of cycles drawn
    juce::String shapeID;    // 0..100  -> sine .. square morph
    juce::String modeID;     // bool: false = one trace, true = mirrored pair

    int height = 68;
};

/** A small latching button tucked into the gap between two knobs of a row. */
struct ToggleSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Index into `knobs`. By default the button sits in the gap to the right of
        that knob (ignored if the next knob starts a new row); with
        `centeredAbove` it sits centred over that knob's cap instead. */
    int afterKnobIndex = 0;

    /** Colour of the bezel and legend while the button is on. Unset falls back
        to the theme's glow. */
    std::optional<juce::Colour> litColour;

    /** Place the button centred above `afterKnobIndex`'s cap rather than in the
        gap after it. */
    bool centeredAbove = false;

    /** Called on a user click of the button (not on automation or host-driven
        state changes), after the toggle state and its parameter have updated.
        Lets a pedal react to a deliberate flip without a parameter listener that
        writes other parameters. */
    std::function<void()> onClick;
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

    /** Big sliding switch in a strip carved off the top of the knob area. */
    std::optional<SlideToggleSpec> slideToggle;

    /** LFO preview band between the knob row and the pedal name. */
    std::optional<WaveDisplaySpec> waveDisplay;

    int knobsPerRow = 3;

    /** Height is shared across pedals so they line up side by side on a rack;
        only the width follows the number of knobs in a row - use
        `knobRowWidth (knobsPerRow)` unless the face has no knob row. */
    int width = knobRowWidth (3);
    int height = 478;
};

} // namespace ee::ui
