#pragma once

#include <juce_graphics/juce_graphics.h>

#include "ee/ui/PedalTheme.h"

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

    /** Draw this one cap in the other style, against the theme's own. For a
        control that is not really part of the same machine as the rest of the
        row - Peak Delay's Tape knob is a tape machine in front of the delay,
        and keeps its photographic cap on a face of digital ones.

        Unset follows `PedalTheme::controlStyle`, which is what every other
        control does. */
    std::optional<ControlStyle> capStyle;

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

    /** When set, this paints the value row instead of any text - a little glyph
        that stands in for a discrete setting (a filter-curve icon for a
        low/band/high switch, an LFO-waveform icon for a shape morph). Given the
        value-row rect and the colour the text would have used; re-called on
        every value change. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> valueIcon;

    /** Leave the value row empty until the knob is being turned: the caption is
        all that shows at rest, and the reading (or `valueIcon`) appears above it
        only while it is dragged. For a control whose number only matters while
        it is being set. */
    bool captionUntilTouched = false;

    /** Marks the top of the travel: the last tick on the scale is drawn fat and
        in this colour, with `endMarkerLabel` printed just outside it. For a
        control whose maximum is a different thing rather than more of the same
        - a Decay that latches the sweep on for ever rather than merely holding
        it a long time.

        Digital caps only; the analog cap has no tick scale to mark. */
    std::optional<juce::Colour> endMarker;
    juce::String endMarkerLabel;

    /** When set, this paints a glyph on the cap itself rather than in a text
        row - given the clear circle inside the pointer's orbit and the colour
        to draw in. For a control whose setting is a shape (a filter curve, an
        LFO waveform): the picture belongs on the knob, where it is always in
        view, not in a row of text under it.

        Digital caps only. The analog cap is photographic artwork with no clear
        face to draw on, so this is ignored there - use `valueIcon` for a face
        in that style. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> capIcon;
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

    /** Colour of the two labels. Unset follows `accent`, so the labels match
        the switch; set it for a face that wants them read as plain text. */
    std::optional<juce::Colour> labelColour;

    /** Pull the switch left so the resting label's text starts at the component
        edge, rather than leaving the label box's own slack in front of it. For
        a switch that has to line up with a panel below it. */
    bool labelFlushLeft = false;

    /** Put `labelOn` on the left and rest the knob there when the parameter is
        true, rather than the other way round. For a switch whose reading order
        puts the set state first - a Sync / MS switch, where "Sync" belongs on
        the left but is the true state. */
    bool invertPosition = false;
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

/** A live filter-response scope, drawn in a band between the knob rows and the
    pedal name. A static curve at the base frequency plus two moving curves whose
    peak slides with a modulator; all resonant bumps, height set by resonance. */
struct FilterScopeSpec
{
    std::function<float()> baseFreqHz;    // static curve centre, in Hz
    std::function<float()> resonance01;   // 0..1 -> peak height and width
    std::function<float()> modL;          // live signed exponent for the left curve
    std::function<float()> modR;          // ... and the right

    /** Curve colours. Unset keeps the built-in blue static curve and its
        orange / amber moving pair. */
    std::optional<juce::Colour> baseColour;    // the static curve at Freq
    std::optional<juce::Colour> sweepColour;   // both moving curves

    /** The largest |mod| the modulator can reach - the Range knob on Peak Wah.
        When set, the whole band the peak can sweep over is shaded behind the
        curves, so a face shows its range at rest rather than only while
        something is playing through it. Unset leaves the band undrawn.

        Digital screens only; the analog scope draws its curves alone. */
    std::function<float()> sweepDepth01;

    /** peakHz = baseFreqHz * sweepRatioMax^mod. */
    float sweepRatioMax = 5.0f;

    int height = 64;
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

    /** Place the button centred below `afterKnobIndex`'s label block - under a
        knob rather than beside it, the way a Sync switch hangs off a Time
        knob. */
    bool centeredBelow = false;

    /** Pixels between the anchor's printed text and the top of the button, for
        `centeredBelow`. The default tucks it right up under the label, which is
        what a switch hanging off a knob in a crowded row wants; a face with a
        row underneath needs more, or the button crowds the caps below it. */
    int belowGap = 4;

    /** Called on a user click of the button (not on automation or host-driven
        state changes), after the toggle state and its parameter have updated.
        Lets a pedal react to a deliberate flip without a parameter listener that
        writes other parameters. */
    std::function<void()> onClick;

    /** When set, the button carries this glyph instead of its caption - given
        the square inside the bezel and the colour the legend would have used.
        For a control whose meaning is a picture (a chain link for "follow the
        host tempo"). Digital faces only; the lit bezel button always prints its
        caption. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;

    /** When set, the toggle is drawn as a small two-way sliding switch carrying
        these labels rather than as a lit bezel button. Placement is unchanged -
        `afterKnobIndex`, `centeredBelow` and the rest still decide where it
        goes; only the shape differs. `parameterID` and `caption` above still
        drive it, so leave the spec's own `parameterID` empty. */
    std::optional<SlideToggleSpec> asSwitch;
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

    /** Paints a glyph in place of the value text - see `KnobSpec::valueIcon`. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> valueIcon;

    /** Show the value (or the glyph) only while the knob is being turned - see
        `KnobSpec::captionUntilTouched`. */
    bool captionUntilTouched = false;

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

    /** Centre the sliding switch in its strip rather than pinning it left - for
        a face where it is the only thing in that strip. */
    bool slideToggleCentred = false;

    /** Lift the sliding switch this many pixels out of its strip, towards the
        top edge. For a face that wants the strip tighter than the shared one. */
    int slideToggleRise = 0;

    /** Vertical gap between knob rows. 0 keeps the shared column gap; a larger
        value spreads the rows apart and a smaller one pulls them together,
        which (because the block stays centred in its area) moves the top and
        bottom rows by half of the difference each. Spread a face that wants
        something sitting between its rows; tighten one whose rows are two
        clusters that should read as blocks. */
    int knobRowGap = 0;

    /** Lift the whole knob block this many pixels above where centring would put
        it, for a face that wants its rows closer to the strip above them and
        further off the pedal name below. */
    int knobBlockRise = 0;

    /** Artwork centred in the gap between the knob block and the pedal name -
        a small emblem for the effect itself. Scaled to `titleImageHeight`,
        keeping its aspect. Leave the image unset for no emblem. */
    juce::Image titleImage;
    int titleImageHeight = 36;

    /** Recolours the emblem, keeping its shape - the same treatment the brand
        logo gets. Leave unset to draw the artwork as it is. Dark lineart on a
        dark face needs this to read at emblem size. */
    std::optional<juce::Colour> titleImageTint;

    /** Push each knob this many pixels away from the middle of its row, within
        its own column. The middle column of an odd row stays put. For a face
        whose columns are wider than its caps and wants them out at the edges,
        clear of whatever sits between them. */
    int knobColumnSpread = 0;

    /** Lift the display band (and everything above it) this many pixels off the
        bottom of the control area, for a face that wants more air between the
        band and the pedal name below it. */
    int displayBandRise = 0;

    /** LFO preview band between the knob row and the pedal name. */
    std::optional<WaveDisplaySpec> waveDisplay;

    /** Live filter-response scope band, in the same slot as `waveDisplay`. */
    std::optional<FilterScopeSpec> filterScope;

    /** Main-knob cap diameter override. 0 keeps the shared `kKnobDiameter`; a
        face that has to fit more rows can ask for smaller caps. */
    int knobDiameter = 0;

    int knobsPerRow = 3;

    /** Draw a vertical rule down the knob grid, centred in the gap after this
        many columns. 0 = no rule. For a face whose knobs read as two clusters -
        the filter on one side of it, its modulation on the other. */
    int knobDividerAfterColumn = 0;

    /** Put the pedal name on the logo row, to the right of the emblem, instead
        of on a row of its own above it. Hands the whole name row back to the
        controls. */
    bool titleBesideLogo = false;

    /** Push that emblem-and-name pair to the right end of its row, leaving the
        left of the row for whatever else lives down there - a Mono/Stereo
        switch. Only meaningful with `titleBesideLogo`. */
    bool titleRowAlignRight = false;

    /** Pull that right-aligned pair this many pixels back in from the right
        margin. For a face whose name would otherwise sit hard against the edge
        the controls above it keep clear of. */
    int titleRowRightInset = 0;

    /** Height is shared across pedals so they line up side by side on a rack;
        only the width follows the number of knobs in a row - use
        `knobRowWidth (knobsPerRow)` unless the face has no knob row. */
    int width = knobRowWidth (3);
    int height = 478;
};

} // namespace ee::ui
