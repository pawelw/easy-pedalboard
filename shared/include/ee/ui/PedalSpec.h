#pragma once

#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <optional>
#include <vector>

namespace ee::ui
{

//==============================================================================
// Shared face metrics, so every pedal spaces its knob columns identically
// whatever the row count. These match Peak Reverb's original layout.

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
    /** Leave empty for a spacer: the entry holds its column in the knob grid
        and draws nothing, for a face that wants a hole in its block. A toggle
        anchored to a spacer is centred in the empty cell. */
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

    /** In compact mode, print the caption on the one text line instead of the
        value. For a knob whose value needs no unit on the face - a bare "reso"
        tucked between the main knobs. */
    bool compactCaption = false;

    /** If set, this knob shades one side of the fader grid to show the band it
        is removing. Only meaningful for `cornerKnobs`. */
    CutSide cutSide = CutSide::none;

    /** Draw the value ring full and white, with the arc growing backward from
        the maximum as the knob turns down - for a control whose resting place
        is the top of its range (a high cut that idles wide open). */
    bool invertedArc = false;

    /** Grow the value arc out of 12 o'clock instead of out of the minimum, and
        mark that centre with a tick - for a bipolar control whose resting place
        is the middle of its range (a tone control that tilts either way). */
    bool bipolarArc = false;

    /** Snap onto the middle of the range while dragging, so a bipolar control
        can be put back to dead centre with a flick rather than a nudge. Only
        affects the mouse: automation and typed values pass through untouched. */
    bool centreDetent = false;

    /** Cap diameter for this knob alone, in pixels. 0 keeps the shared size.
        Smaller marks a control out as secondary without moving it off the grid
        or dropping its caption, the way `compact` would. */
    int diameter = 0;

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

/** A small knob that nudges a group of faders together. It carries no parameter
    of its own: turning it applies the change in its own position as a relative
    offset (in the faders' own units) to every listed fader, clamped to each
    fader's range. Sits in the strip above the fader grid, to the left, mirroring
    the corner cut knobs on the right. Only meaningful on a fader pedal. */
struct GroupTrimSpec
{
    juce::String caption;

    /** Indices into `sliders` of the faders this knob moves. */
    std::vector<int> sliderIndices;
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

    /** Set both to turn the preview into a live scope: the trace scrolls at the
        running LFO's phase and its amplitude follows the effective depth. Unset
        keeps the static, knob-tracking preview. */
    std::function<float()> livePhase;   // [0, 1) LFO phase
    std::function<float()> liveDepth;   // 0..1 Amount * gate
};

/** A small latching button tucked into the gap between two knobs of a row. */
struct ToggleSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Index into `knobs`. By default the button sits in the gap to the right of
        that knob (ignored if the next knob starts a new row); with
        `centeredAbove` it sits centred over that knob's cap instead. Point it at
        a spacer entry and it takes the middle of that empty cell, whichever of
        the two is set. */
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

/** A compact knob for a secondary row below the main knob block, with an
    optional latching button pinned directly beneath it - the LFO random switch
    under a Shape knob, a tempo Sync switch under a Time knob. */
struct SubKnobSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Replaces the parameter's own text in the value readout, re-queried on
        every change and whenever a sibling button is clicked (so a Time knob can
        flip between milliseconds and note values when Sync moves). */
    std::function<juce::String()> liveValueText;

    /** Empty for a plain knob. Otherwise a latching button sits under the knob,
        bound to this bool parameter. */
    juce::String buttonParameterID;
    juce::String buttonCaption;
    std::optional<juce::Colour> buttonLitColour;

    /** Called on a user click of the button, after its parameter has updated. */
    std::function<void()> buttonOnClick;
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

    /** A small knob centred on the block of main knobs, for a secondary control
        that belongs "between" them. Laid out on top of the row grid, in the
        gap the caps leave in the middle. */
    std::optional<KnobSpec> centreKnob;

    /** Group-trim knobs pinned to the top-left of the fader strip, opposite the
        corner cut knobs. A divider is drawn between the two clusters. */
    std::vector<GroupTrimSpec> groupTrims;

    /** Cap diameter for the compact knobs - the corner cut knobs and the group
        trims. 0 keeps the shared default; a wider face can carry larger caps. */
    int compactKnobDiameter = 0;

    /** A row of small knobs below the main knob block, each with an optional
        button beneath it. For a pedal whose secondary controls (LFO shape,
        rate, filter type) sit under its headline knobs. */
    std::vector<SubKnobSpec> subKnobs;

    std::vector<SliderSpec> sliders;
    std::vector<ToggleSpec> toggles;

    /** Big sliding switch in a strip carved off the top of the knob area, or -
        with `slideToggleBottom` - left-aligned in a strip below everything else. */
    std::optional<SlideToggleSpec> slideToggle;
    bool slideToggleBottom = false;

    /** LFO preview band between the knob row and the pedal name. */
    std::optional<WaveDisplaySpec> waveDisplay;

    /** Main-knob cap diameter override. 0 keeps the shared `kKnobDiameter`; a
        face that has to fit more rows can ask for smaller caps. */
    int knobDiameter = 0;

    int knobsPerRow = 3;

    /** Height is shared across pedals so they line up side by side on a rack;
        only the width follows the number of knobs in a row - use
        `knobRowWidth (knobsPerRow)` unless the face has no knob row. */
    int width = knobRowWidth (3);
    int height = 478;
};

} // namespace ee::ui
