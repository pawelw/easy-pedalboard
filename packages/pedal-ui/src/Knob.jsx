import { useEffect, useMemo, useRef, useState } from "react";
import "./Knob.css";
import StableDigits from "./StableDigits.jsx";

const MIN_ANGLE = -135;
const MAX_ANGLE = 135;
const PIXELS_PER_FULL_SWEEP = 200;

// Fine tune: holding shift scales every increment of a drag (and of an arrow
// key) down by this much, so the same hand movement covers a fifth of the
// range - the gesture a DAW's own controls use for the last few percent of a
// value. It is a factor on the *increment*, not a second mapping of the
// pointer's absolute position, so shift can go down and come back up
// mid-drag without the knob jumping.
const FINE_FACTOR = 0.2;

// The value sweep, in pixels off the knob's rim so it keeps the same visual
// gap at every knob size.
const SWEEP_GAP = 6;
const SWEEP_WIDTH = 3.2;

// The collar, as fractions of the knob radius. The teeth are circles buried
// deep in the ring so only a shallow cap of each one shows: that gives wide
// scallops parted by narrow notches, the reference's edge rather than a knurl.
const TOOTH_COUNT = 24;
const RING_OUTER = 0.915;
const TOOTH_CENTRE = 0.775;
const TOOTH_RADIUS = 0.18;
const COLLAR_INNER = 0.79; // where the cap starts

function angleFor(value01) {
  return MIN_ANGLE + value01 * (MAX_ANGLE - MIN_ANGLE);
}

/** A short radial tick riding on top of an arc/tick-ring the knob already
    draws, at `value01`'s own angle - BitBit Grain's live "this is where the
    LFO has the value pushed to right now" marker (see ModdableKnob.jsx),
    separate from the knob's own value pointer so the two can move
    independently: the knob's own arc/needle stays at the automatable
    parameter's value, this rides the modulated position around it.
    `rInner`/`rOuter` are in the caller's own SVG units, so it always lines
    up exactly with whichever ring (Sweep's arc or TickScale's dashes) it is
    drawn over. Colour comes from a CSS class, not a `stroke` attribute -
    see TickScale's own note on why WKWebView needs that. */
function ModTick({ value01, rInner, rOuter }) {
  const angle = angleFor(value01);
  const toRad = (d) => ((d - 90) * Math.PI) / 180;
  const rad = toRad(angle);
  return (
    <line
      className="pui-knob__mod-tick"
      x1={Math.cos(rad) * rInner}
      y1={Math.sin(rad) * rInner}
      x2={Math.cos(rad) * rOuter}
      y2={Math.sin(rad) * rOuter}
    />
  );
}

function arcPath(r, fromDeg, toDeg) {
  const toRad = (d) => ((d - 90) * Math.PI) / 180;
  const x1 = Math.cos(toRad(fromDeg)) * r;
  const y1 = Math.sin(toRad(fromDeg)) * r;
  const x2 = Math.cos(toRad(toDeg)) * r;
  const y2 = Math.sin(toRad(toDeg)) * r;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${toDeg - fromDeg > 180 ? 1 : 0} 1 ${x2} ${y2}`;
}

/** The collar: one flat dark scalloped ring. No facet shading - it reads as a
    moulded plastic collar, one colour, and the light comes from the cap
    sitting proud of it. It's a physical part of the knob, not a fixed bezel
    around it, so it turns with `angle` - the cap's own gradient stays put,
    since that's a fixed light source's reflection, not something that
    should spin with the plastic underneath it. */
function Collar({ radius, angle }) {
  const teeth = useMemo(() => {
    const marks = [];
    for (let i = 0; i < TOOTH_COUNT; i++) {
      const a = (i / TOOTH_COUNT) * Math.PI * 2;
      marks.push(
        <circle key={i} cx={Math.cos(a) * radius * TOOTH_CENTRE} cy={Math.sin(a) * radius * TOOTH_CENTRE} r={radius * TOOTH_RADIUS} />,
      );
    }
    return marks;
  }, [radius]);

  return (
    <svg
      className="pui-knob__collar"
      viewBox={`${-radius} ${-radius} ${radius * 2} ${radius * 2}`}
      width={radius * 2}
      height={radius * 2}
      style={{ transform: `rotate(${angle}deg)` }}
    >
      <g fill="var(--pui-knob-body)">
        <circle cx="0" cy="0" r={radius * RING_OUTER} />
        {teeth}
      </g>
      <circle
        cx="0"
        cy="0"
        r={radius * (COLLAR_INNER + 0.015)}
        fill="none"
        stroke="var(--pui-knob-body-hi)"
        strokeWidth={radius * 0.03}
      />
    </svg>
  );
}

/** The value readout: one continuous arc outside the knob, a pale track with
    the part up to the value lit. Outside rather than on the collar - a light
    line on the dark ring reads as part of the knob, not as its value.
    `gap`/`width`/the two colours default to the collar variant's own look
    (unchanged); `variant="scale"` passes its own smaller gap and the
    grayscale tick tokens instead - same arc, different geometry/palette.
    `from` is TickScale's, on an arc: "max" lights the span between the value
    and the top of travel, for a cut that rests wide open. "centre" lights the
    span between twelve o'clock and the value, in whichever direction it has
    been turned - for a trim whose resting position is the middle and whose
    travel is a departure from it in either direction, so which *way* it has
    been moved is as much of the reading as how far. */
function Sweep({
  diameter,
  value,
  gap = SWEEP_GAP,
  width = SWEEP_WIDTH,
  trackColor = "var(--pui-knob-sweep)",
  litColor = "var(--pui-knob-sweep-lit)",
  from = "min",
  // BitBit Grain's live modulation marker (see ModTick above) - 0..1 or
  // undefined/null to draw nothing. Spans exactly the arc's own stroke
  // width so it reads as a mark riding the existing ring rather than a
  // second, separate arc.
  modValue,
}) {
  const r = diameter / 2 + gap;
  const box = r + width;
  const fromMax = from === "max";
  const fromCentre = from === "centre";

  // The same 0.004 of travel every reading here treats as "still at rest", so a
  // knob parked at its resting position shows a bare track rather than a stub
  // of lit arc - measured from the middle rather than from an end.
  const lit = fromCentre ? Math.abs(value - 0.5) > 0.004 : fromMax ? value < 0.996 : value > 0.004;

  const span = fromCentre
    ? arcPath(r, Math.min(angleFor(0.5), angleFor(value)), Math.max(angleFor(0.5), angleFor(value)))
    : fromMax
      ? arcPath(r, angleFor(value), MAX_ANGLE)
      : arcPath(r, MIN_ANGLE, angleFor(value));

  return (
    <svg
      className="pui-knob__sweep"
      viewBox={`${-box} ${-box} ${box * 2} ${box * 2}`}
      width={box * 2}
      height={box * 2}
    >
      <g fill="none" strokeWidth={width} strokeLinecap="round">
        <path d={arcPath(r, MIN_ANGLE, MAX_ANGLE)} stroke={trackColor} />
        {lit && <path d={span} stroke={litColor} />}
      </g>
      {modValue != null && <ModTick value01={modValue} rInner={r - width / 2} rOuter={r + width / 2} />}
    </svg>
  );
}

// variant="soft"'s arc, pulled in tight to the rim (see the Sweep call in Knob
// below). At 60px and up it is thicker than the collar's 3.2; the small pair is
// for the 42px-class knobs and goes the other way, to a hairline, because at
// that diameter anything wider reads as a band rather than a line.
const SOFT_SWEEP_GAP = 0;
const SOFT_SWEEP_WIDTH = 4;
const SOFT_SWEEP_WIDTH_SMALL = 3;

// variant="concave" - "2a" of design_handoff_flat_knobs: one continuous arc
// hugging the rim, drawn by the same Sweep every other variant's arc uses.
// CONCAVE_REF_* are the handoff's own numbers at its 52px reference size (radius
// 28.5, stroke 5) - gap/width below scale those linearly for any other
// `size`, the way the handoff scales everything.
const CONCAVE_REF_DIAMETER = 52;
const CONCAVE_REF_RADIUS = 28.5;
const CONCAVE_REF_STROKE = 5;
const CONCAVE_GAP = CONCAVE_REF_RADIUS - CONCAVE_REF_DIAMETER / 2;

// variant="spoke" - "4a" of design_handoff_knob_4a: no cap, no body at all -
// a thin outline circle, a track/lit arc, and a hairline spoke from the
// centre out to the value, the whole thing four SVG strokes. Every number is
// the handoff's own, at its 56px reference size; SPOKE_REF_* is what the rest
// scale against for any other `size`.
const SPOKE_REF_DIAMETER = 56;
const SPOKE_REF_RADIUS = 25;
const SPOKE_REF_OUTLINE_RADIUS = 17;
const SPOKE_REF_SPOKE_RADIUS = 20;
const SPOKE_OUTLINE_WIDTH = 1.2;
// The handoff's own floor: below ~60px the arc/spoke hold these widths rather
// than scaling down with the rest, "or the control disappears".
const SPOKE_ARC_WIDTH = 2.4;
const SPOKE_ARC_WIDTH_LARGE = 3;
const SPOKE_WIDTH = 1.6;
const SPOKE_WIDTH_LARGE = 2;
// The bipolar detent tick at 0deg (design_handoff_knob_4a's "Bipolar variant").
const SPOKE_DETENT_WIDTH = 1.4;
const SPOKE_DETENT_INNER = 5; // r - 5
const SPOKE_DETENT_OUTER = 1.5; // r - 1.5

/** The whole "spoke" knob face: an SVG with no filled body under it, so the
    knob reads as line art over whatever ground it sits on rather than as an
    object of its own. Draw order is the handoff's own, back to front: static
    outline, track, lit arc, then the spoke on top.

    `from` is the same three readings every other variant's arc takes -
    "min" (default) lights from the bottom of travel, "max" from the top down,
    "centre" out from 12 o'clock in whichever direction the knob has turned
    (the handoff's own "Bipolar variant"), with a fixed tick at 12 marking the
    rest position no lit arc would otherwise show. */
function SpokeKnob({ diameter, value, from = "min" }) {
  const scale = diameter / SPOKE_REF_DIAMETER;
  const r = SPOKE_REF_RADIUS * scale;
  const outlineR = SPOKE_REF_OUTLINE_RADIUS * scale;
  const spokeR = SPOKE_REF_SPOKE_RADIUS * scale;
  const large = diameter >= 60;
  const arcWidth = large ? SPOKE_ARC_WIDTH_LARGE : SPOKE_ARC_WIDTH;
  const spokeWidth = large ? SPOKE_WIDTH_LARGE : SPOKE_WIDTH;

  const angle = angleFor(value);
  const toRad = (d) => ((d - 90) * Math.PI) / 180;

  const fromMax = from === "max";
  const fromCentre = from === "centre";
  const litFrom = fromMax ? angle : fromCentre ? Math.min(0, angle) : MIN_ANGLE;
  const litTo = fromMax ? MAX_ANGLE : fromCentre ? Math.max(0, angle) : angle;
  const showLit = Math.abs(litTo - litFrom) >= 0.4;

  const box = r + arcWidth;

  return (
    <svg
      className="pui-knob__sweep"
      viewBox={`${-box} ${-box} ${box * 2} ${box * 2}`}
      width={box * 2}
      height={box * 2}
    >
      <circle r={outlineR} fill="none" stroke="var(--pui-outline)" strokeWidth={SPOKE_OUTLINE_WIDTH} />
      <g fill="none" strokeLinecap="round">
        <path d={arcPath(r, MIN_ANGLE, MAX_ANGLE)} stroke="var(--pui-concave-track)" strokeWidth={arcWidth} />
        {showLit && <path d={arcPath(r, litFrom, litTo)} stroke="var(--pui-soft-lit)" strokeWidth={arcWidth} />}
        {fromCentre && (
          <line
            x1={Math.cos(toRad(0)) * (r - SPOKE_DETENT_INNER)}
            y1={Math.sin(toRad(0)) * (r - SPOKE_DETENT_INNER)}
            x2={Math.cos(toRad(0)) * (r - SPOKE_DETENT_OUTER)}
            y2={Math.sin(toRad(0)) * (r - SPOKE_DETENT_OUTER)}
            stroke="var(--pui-ink-dim)"
            strokeWidth={SPOKE_DETENT_WIDTH}
          />
        )}
        <line
          x1={0}
          y1={0}
          x2={Math.cos(toRad(angle)) * spokeR}
          y2={Math.sin(toRad(angle)) * spokeR}
          stroke="var(--pui-ink-soft)"
          strokeWidth={spokeWidth}
        />
      </g>
    </svg>
  );
}

const TICK_COUNT = 20;
const TICK_LENGTH = 6;
const TICK_THICKNESS = 2;
const TICK_LENGTH_SMALL = 4;
const TICK_THICKNESS_SMALL = 1.4;

/** `variant="scale"`'s tick ring: 20 individual radial dashes, evenly spaced
    from -135deg to +135deg *inclusive of both endpoints* - so the first and
    last ticks sit exactly on the travel limits and are mirror images of
    each other around the top (0deg), by construction rather than by hoping
    a periodic pattern happens to land there.

    Each tick's lit/unlit colour is a plain `index <= litCount` comparison,
    not a continuous angular mask over a repeating pattern (the previous
    approach) - that mask's own sweep angle and the ticks' 13.5deg period
    were two independently-computed numbers that only lined up at exact
    multiples of a tick's width, so at most values the sweep boundary fell
    *inside* a tick rather than between two of them, leaving that tick part-
    covered - including, at the very top of travel, one or two ticks whose
    sliver of "still transparent" mask left them reading dim instead of lit.
    Per-tick integer comparison can't have a boundary that lands wrong.

    Colour is set via `style`, not the `stroke` attribute directly - WebKit
    (the real plugin's WKWebView; this ran fine in Chromium-based preview
    tools) doesn't reliably resolve `var(...)` custom properties written
    into an SVG presentation attribute, only ones reached through the
    ordinary CSS/style pipeline, so `stroke="var(--x)"` could render every
    tick some default colour regardless of index.

    `gap`: distance from the dial's own edge to the ticks' inner radius -
    like Sweep's own `gap`, independent of `diameter`, so two differently-
    sized scale knobs can be told to sit the same distance from a neighbour
    above them (see Knob's own comment on this below). Below `diameter` 60,
    the dashes themselves also shrink - full-size ones looked oversized next
    to a 42px knob. */
function TickScale({ diameter, value, gap = SWEEP_GAP, from = "min", modValue }) {
  const small = diameter < 60;
  const tickLength = small ? TICK_LENGTH_SMALL : TICK_LENGTH;
  const tickThickness = small ? TICK_THICKNESS_SMALL : TICK_THICKNESS;
  const rInner = diameter / 2 + gap;
  const rOuter = rInner + tickLength;
  const box = rOuter + tickThickness;

  // Which ticks are lit, and from which end. `from: "min"` is the ordinary
  // reading - the scale fills from the bottom of the travel as the knob comes
  // up. `from: "max"` reverses it, for a control whose *resting* position is
  // the top of its range and whose travel is a subtraction: a high cut sits at
  // the top doing nothing, and what the lit ticks then show is how much of the
  // band is being taken away. `from: "centre"` lights out from the middle in
  // whichever direction the knob has been turned, for a trim that rests at
  // twelve o'clock - the same three readings Sweep draws.
  const fromMax = from === "max";
  const fromCentre = from === "centre";

  // -1 / TICK_COUNT (nothing lit) below the same threshold Sweep uses for its
  // own arc, so a knob at its resting end shows no lit tick at all rather than
  // always lighting the end one (Math.round(0 * anything) is still 0, which
  // would otherwise read as "the first tick has been passed").
  const rest = fromCentre ? Math.abs(value - 0.5) > 0.004 : fromMax ? value < 0.996 : value > 0.004;
  const edge = Math.round(value * (TICK_COUNT - 1));
  const middle = Math.round(0.5 * (TICK_COUNT - 1));
  const litFrom = fromMax && rest ? edge : TICK_COUNT;
  const litCount = ! fromMax && rest ? edge : -1;
  const isLit = (i) =>
    fromCentre
      ? rest && (edge >= middle ? i > middle && i <= edge : i < middle && i >= edge)
      : fromMax
        ? i >= litFrom
        : i <= litCount;

  const ticks = useMemo(() => {
    const toRad = (d) => ((d - 90) * Math.PI) / 180;
    const marks = [];
    for (let i = 0; i < TICK_COUNT; i++) {
      const angle = MIN_ANGLE + (i * (MAX_ANGLE - MIN_ANGLE)) / (TICK_COUNT - 1);
      const rad = toRad(angle);
      marks.push({
        x1: Math.cos(rad) * rInner,
        y1: Math.sin(rad) * rInner,
        x2: Math.cos(rad) * rOuter,
        y2: Math.sin(rad) * rOuter,
      });
    }
    return marks;
  }, [rInner, rOuter]);

  return (
    <svg className="pui-knob__ticks" viewBox={`${-box} ${-box} ${box * 2} ${box * 2}`} width={box * 2} height={box * 2}>
      <g strokeWidth={tickThickness} strokeLinecap="round">
        {ticks.map((t, i) => (
          <line
            key={i}
            x1={t.x1}
            y1={t.y1}
            x2={t.x2}
            y2={t.y2}
            style={{ stroke: isLit(i) ? "var(--pui-tick-lit)" : "var(--pui-tick)" }}
          />
        ))}
      </g>
      {modValue != null && <ModTick value01={modValue} rInner={rInner} rOuter={rOuter} />}
    </svg>
  );
}

/** `variant="scale"`'s needle: a rounded bar pinned at the knob's centre and
    rotated to the value angle, the way `.pui-knob__dot` pins a dot in the
    collar variant - same wrapper-rotates-not-the-bar trick, so the bar's own
    box can be positioned in plain top/left percentages instead of trig. */
function Pointer({ angle, diameter, soft = false, concave = false }) {
  // `soft`: variant="soft"'s needle instead - one hairline running from the
  // cap's centre out towards the rim, rather than a stub parked near the
  // edge. Same wrapper and the same rotation; only the bar's own geometry
  // (set in Knob.css) differs, so there is one place that knows how a
  // pointer is pinned and rotated. `concave` is variant="concave"'s own bar,
  // the handoff's own geometry (design_handoff_flat_knobs).
  const cls = concave
    ? "pui-knob__pointer pui-knob__pointer--concave"
    : soft
      ? `pui-knob__pointer pui-knob__pointer--needle${diameter < 60 ? " pui-knob__pointer--needle-thin" : ""}`
      : `pui-knob__pointer${diameter < 60 ? " pui-knob__pointer--thin" : ""}`;

  return (
    <div className="pui-knob__pointer-wrap" style={{ transform: `rotate(${angle}deg)` }}>
      <div className={cls} />
    </div>
  );
}

/** A fixed label printed just outside the arc's top end, the way hardware
    prints a mark ("MAX", an infinity sign) next to a knob's end of travel -
    always there, not tied to the current value the way the lit sweep is. */
function EndMarker({ label, radius, lit }) {
  if (!label) return null;
  const r = radius + SWEEP_GAP + SWEEP_WIDTH + 7;
  const toRad = (d) => ((d - 90) * Math.PI) / 180;
  const x = Math.cos(toRad(MAX_ANGLE)) * r;
  const y = Math.sin(toRad(MAX_ANGLE)) * r;
  return (
    <span
      className={`pui-knob__end-marker${lit ? " pui-knob__end-marker--lit" : ""}`}
      style={{ left: `calc(50% + ${x}px)`, top: `calc(50% + ${y}px)` }}
    >
      {label}
    </span>
  );
}

/**
 * A rotary knob: controlled (0..1), drag-vertically to turn, arrow keys to
 * nudge, shift for fine tune (FINE_FACTOR). JUCE-agnostic - a plugin's jsui
 * wires this to a WebSliderRelay by passing value/onChange/onDragStart/
 * onDragEnd itself.
 *
 * `variant`: "collar" (default, unchanged) is the scalloped dark collar and
 * outer value arc every pedal has used so far. "scale" is the plain-ring +
 * tick-scale + needle face the onyx Delay layout uses (COMPONENTS.md).
 * "soft" is the third: no collar and no ring at all, just a soft-UI cap that
 * fills the whole dial, a hairline needle from its centre, and one continuous
 * accent arc hugging the rim - the tick ring's information without the twenty
 * dashes, for a face that wants the control to read as one quiet disc.
 * "flat" renders exactly as "soft" does - a separate name only because
 * callers already ask for it by that one, not a second rendering to keep in
 * sync.
 * "concave" is "2a" of design_handoff_flat_knobs: a turned-rim cap with a
 * shallow dish (a light counterpart in :root, the handoff's own dark numbers
 * moved into onyx's block) and a continuous accent arc hugging the rim, the
 * simpler of the two treatments tried here ("2c"'s segmented ring was the
 * first pass) - what every small knob in BitBit Alpine/BitBit Artifact was
 * carrying under the name "flat" before it moved to this one.
 * "spoke" is "4a" of design_handoff_knob_4a: no cap and no body at all - a
 * thin static outline circle, the same continuous arc "concave" draws, and a
 * hairline spoke from the centre out to the value, all four strokes in one
 * SVG. A second treatment tried alongside "concave" so switching between them
 * is a one-word `variant` change, not a rewrite.
 * Same controlled API, same drag/keyboard handling below, only the dial's
 * own markup and CSS differ.
 *
 * `bare`: just the dial - no caption line under it, and no width padding for
 * one. The default wrapper is `size + 28` wide so a caption longer than the
 * knob still centres on it; in a row that puts its labels *beside* the knob
 * instead (StageControl), that padding reads as ~14px of dead space either
 * side, silently widening whatever gap the row asked for.
 *
 * `scaleFrom`: where the value reading starts from - "min" (default), "max"
 * for a knob that rests at the top of its range and counts downwards, or
 * "centre" for a trim that rests at twelve o'clock and departs from it either
 * way. BitBit EQ draws the "max" distinction as an inverted arc on its High Cut;
 * this is that idea generalised, and it drives both the soft variant's arc and
 * the scale variant's tick ring, so the prop means one thing whichever a
 * caller picks. A "centre" knob wants `centreValue` left at its 0.5 default:
 * double-click then puts it back exactly where it rests.
 *
 * `centreValue`: where a double-click puts the knob, the middle of its travel
 * by default. Not the parameter's own default, which for several of these is
 * the bottom of the range - "back to twelve o'clock" is a position on the
 * hardware, and it is the same gesture on every knob whatever that knob
 * happens to open at.
 *
 * `pointer`: false hides the rotating dot/needle/bar every variant otherwise
 * draws over the cap - for a caller whose `icon` already reads as the value
 * (BitBit Grain's Shape, whose glyph *is* the envelope the value produces),
 * so the two aren't fighting for the same small area. The arc/tick ring
 * outside the cap is untouched; only the thing pinned to the cap's own
 * centre goes away. Default true, so every other caller is unaffected.
 */
export default function Knob({
  value,
  onChange,
  onDragStart,
  onDragEnd,
  caption,
  valueLabel,
  subLabel,
  size = 72,
  cornerLabels,
  icon,
  endMarkerLabel,
  step = 0.01,
  variant = "collar",
  sweepGap,
  scaleFrom = "min",
  bare = false,
  centreValue = 0.5,
  pointer = true,
  // A drop target for a drag-and-drop library this component knows nothing
  // about (a caller wires up its own useDroppable/useDrop and hands the
  // pieces in) - `dropRef` is that library's node ref, attached to the dial
  // box rather than the whole control, so a drop-active outline never
  // touches the caption below it or the pointer-drag body inside it.
  // `dropActive` just toggles the outline; this component does no drag
  // detection of its own.
  dropRef,
  dropActive = false,
  // A small marker in the dial box's own top-right corner (BitBit Grain's
  // "modulated by the LFO" dot) - rendered inside .pui-knob__dial via
  // .pui-knob__badge below, not positioned by the caller against the outer
  // .pui-knob box, so it lands in the same spot relative to the dial
  // whatever size that outer box ends up rendered at (a flex-shrunk row,
  // say) rather than drifting with it.
  badge,
  // A style override for the badge's own wrapper (.pui-knob__badge below,
  // which carries the shared top/right anchor every badge starts from) -
  // BitBit Grain's Filter knob is close enough to the plate's own edge that
  // the shared anchor reads ambiguous there, so it passes badgeStyle to move
  // just its own badge (ModdableKnob.jsx). Undefined for every other caller,
  // so nothing here changes for a knob that doesn't ask.
  badgeStyle,
  // BitBit Grain's live "the LFO has this knob pushed to here right now"
  // marker - 0..1 or undefined/null to draw nothing. Deliberately separate
  // from `value`: the knob's own arc/needle keeps showing the automatable
  // parameter's own value and never moves for this, only the extra tick
  // drawn on top of it does (see ModTick above and ModdableKnob.jsx, which
  // computes this every animation frame from the live LFO output).
  modIndicator,
}) {
  const [dragging, setDragging] = useState(false);
  const dragStartRef = useRef(null);
  const bodyRef = useRef(null);

  // onChange/onDragEnd are fresh closures every render (each carries the
  // caller's own current `value`), so the drag-tracking effect below reads
  // them through a ref instead of depending on them directly. It used to
  // depend on them: every onChange call re-rendered the parent with a new
  // onChange identity, which re-ran this effect's cleanup mid-drag - nulling
  // dragStartRef and tearing down the window listeners after the first
  // pixel of movement. A single big jump (as in a synthetic test) never
  // triggers a second pointermove and so never exposed it; an actual mouse
  // drag, which fires many, stopped dead after the first one.
  const latest = useRef({ onChange, onDragEnd, value });
  latest.current = { onChange, onDragEnd, value };

  useEffect(() => {
    if (!dragging) return undefined;

    const handlePointerMove = (event) => {
      const start = dragStartRef.current;
      if (!start) return;
      // Once the OS cursor is locked to the knob (see onPointerDown), it no
      // longer moves at all - clientY stays frozen at wherever it was on
      // pointer-down, so the delta has to come from movementY (the raw
      // relative motion the OS still reports) instead, folded onto the last
      // committed value rather than measured against a fixed start point.
      const locked = document.pointerLockElement === bodyRef.current;
      const fine = event.shiftKey;

      // Unlocked, the delta is measured from a fixed anchor, so a shift press
      // or release part-way through a drag has to re-anchor - otherwise the
      // whole distance travelled so far gets re-scaled at once and the knob
      // jumps. The anchor moves to the *previous* sample rather than this
      // one, paired with the value that sample produced, so the movement
      // carried by the very event that noticed the change still counts. The
      // locked branch is already incremental (each event carries only its own
      // movement) and needs none of this.
      if (!locked && start.fine !== fine) {
        start.y = start.lastY;
        start.value = latest.current.value;
        start.fine = fine;
      }

      const travel = locked ? -event.movementY : start.y - event.clientY;
      const raw = travel * (fine ? FINE_FACTOR : 1);
      const base = locked ? latest.current.value : start.value;
      start.lastY = event.clientY;
      latest.current.onChange(Math.min(1, Math.max(0, base + raw / PIXELS_PER_FULL_SWEEP)));
    };

    const handlePointerUp = () => {
      dragStartRef.current = null;
      setDragging(false);
      latest.current.onDragEnd?.();
      if (document.pointerLockElement === bodyRef.current)
        document.exitPointerLock?.();
    };

    window.addEventListener("pointermove", handlePointerMove);
    window.addEventListener("pointerup", handlePointerUp);
    return () => {
      window.removeEventListener("pointermove", handlePointerMove);
      window.removeEventListener("pointerup", handlePointerUp);
    };
  }, [dragging]);

  const onPointerDown = (event) => {
    event.preventDefault();
    onDragStart?.();
    dragStartRef.current = { y: event.clientY, lastY: event.clientY, value, fine: event.shiftKey };
    setDragging(true);
    // Locks the OS cursor in place for the rest of the drag, the way a
    // hardware knob's own travel isn't tied to how far your hand moves -
    // falls back to plain (cursor-visible, unfrozen) dragging above wherever
    // the browser refuses the lock, so this is never required for dragging
    // to work.
    bodyRef.current?.requestPointerLock?.()?.catch(() => {});
  };

  const onKeyDown = (event) => {
    // Same shift-is-finer rule as the drag above, on the arrow keys.
    const nudge = step * (event.shiftKey ? FINE_FACTOR : 1);
    if (event.key === "ArrowUp" || event.key === "ArrowRight") {
      event.preventDefault();
      onChange(Math.min(1, value + nudge));
    } else if (event.key === "ArrowDown" || event.key === "ArrowLeft") {
      event.preventDefault();
      onChange(Math.max(0, value - nudge));
    }
  };

  // Bracketed by the drag callbacks even though nothing is being dragged: to
  // the host this has to look like one gesture, or it lands in the undo
  // history as a value that changed with no move around it.
  const onDoubleClick = (event) => {
    event.preventDefault();
    onDragStart?.();
    onChange(centreValue);
    onDragEnd?.();
  };

  const angle = angleFor(value);

  const isScale = variant === "scale";
  // "flat" is "soft" under a name some callers already use - see the
  // `variant` doc above.
  const isSoft = variant === "soft" || variant === "flat";
  const isConcave = variant === "concave";
  const isSpoke = variant === "spoke";

  // Soft/flat's cap reads slightly small next to the other variants at the
  // same `size`, so it draws 2px larger while every call site keeps asking
  // for the same size as before.
  const dialSize = isSoft ? size + 6 : size;
  const radius = dialSize / 2;

  return (
    <div
      className={`pui-reset pui-knob${isScale ? " pui-knob--scale" : ""}${isSoft ? " pui-knob--soft" : ""}${isConcave ? " pui-knob--concave" : ""}${isSpoke ? " pui-knob--spoke" : ""}`}
      style={{ width: bare ? size : size + 28 }}
    >
      <div
        ref={dropRef}
        className="pui-knob__dial"
        data-drop-active={dropActive || undefined}
        style={{ width: dialSize, height: dialSize }}
      >
        {isSpoke ? (
          <SpokeKnob diameter={size} value={value} from={scaleFrom} />
        ) : isConcave ? (
          <Sweep
            diameter={size}
            value={value}
            gap={CONCAVE_GAP * (size / CONCAVE_REF_DIAMETER)}
            width={CONCAVE_REF_STROKE * (size / CONCAVE_REF_DIAMETER)}
            trackColor="var(--pui-concave-track)"
            litColor="var(--pui-soft-lit)"
            from={scaleFrom}
            modValue={modIndicator}
          />
        ) : isSoft ? (
          // The same arc the collar variant draws, on the soft palette and
          // sitting closer in: with no ring or collar around the cap there is
          // nothing between the arc and the knob, so the old 6px gap read as
          // a detached circle rather than the knob's own value.
          <Sweep
            diameter={dialSize}
            value={value}
            gap={sweepGap ?? SOFT_SWEEP_GAP}
            width={size >= 60 ? SOFT_SWEEP_WIDTH : SOFT_SWEEP_WIDTH_SMALL}
            trackColor="var(--pui-soft-track)"
            litColor="var(--pui-soft-lit)"
            from={scaleFrom}
            modValue={modIndicator}
          />
        ) : isScale ? (
          <TickScale
            diameter={size}
            value={value}
            // The ring's own distance past the dial's box (what actually
            // sets its visual distance from whatever sits above the knob,
            // like Delay's TapScope) is `gap` alone, independent of `size` -
            // the ring's outer edge sits at radius size/2+gap regardless of
            // diameter, so two differently-sized scale knobs read the same
            // distance from that neighbour as long as gap matches, whatever
            // their own sizes are. Callers that need to line up with
            // another scale knob of a different size (Delay's Mix/Feedback
            // with its Time knobs) pass sweepGap explicitly instead of
            // relying on the size default.
            gap={sweepGap ?? (size >= 60 ? 9 : 6)}
            from={scaleFrom}
            modValue={modIndicator}
          />
        ) : (
          <Sweep diameter={size} value={value} />
        )}
        <EndMarker label={endMarkerLabel} radius={radius} lit={value >= 0.999} />

        {badge && (
          <div className="pui-knob__badge" style={badgeStyle}>
            {badge}
          </div>
        )}

        {cornerLabels?.topLeft && <span className="pui-knob__corner pui-knob__corner--tl">{cornerLabels.topLeft}</span>}
        {cornerLabels?.topRight && <span className="pui-knob__corner pui-knob__corner--tr">{cornerLabels.topRight}</span>}
        {cornerLabels?.bottomLeft && (
          <span className="pui-knob__corner pui-knob__corner--bl">{cornerLabels.bottomLeft}</span>
        )}
        {cornerLabels?.bottomRight && (
          <span className="pui-knob__corner pui-knob__corner--br">{cornerLabels.bottomRight}</span>
        )}

        <div
          ref={bodyRef}
          className={`pui-knob__body${isScale ? " pui-knob__body--scale" : ""}${isSoft ? " pui-knob__body--soft" : ""}${isConcave ? " pui-knob__body--concave" : ""}${isSpoke ? " pui-knob__body--spoke" : ""}${dragging ? " pui-knob__body--dragging" : ""}`}
          style={{ width: dialSize, height: dialSize }}
          onPointerDown={onPointerDown}
          onKeyDown={onKeyDown}
          onDoubleClick={onDoubleClick}
          role="slider"
          tabIndex={0}
          aria-label={caption}
          aria-valuemin={0}
          aria-valuemax={1}
          aria-valuenow={value}
        >
          {isSpoke ? (
            // No cap under the spoke's own SVG - the hit area is the bare
            // body div, styled fully transparent (Knob.css).
            icon && <div className="pui-knob__icon">{icon(value)}</div>
          ) : isConcave ? (
            <div className="pui-knob__concave-face">
              {pointer && <Pointer angle={angle} diameter={size} concave />}
              {icon && <div className="pui-knob__icon">{icon(value)}</div>}
            </div>
          ) : isSoft ? (
            <>
              {/* One element, not the ring+cap pair the scale variant needs:
                  there is no ring here, so the cap itself carries both the
                  face gradient and the raised shadow. */}
              <div className="pui-knob__soft-cap">
                {pointer && <Pointer angle={angle} diameter={dialSize} soft />}
                {icon && <div className="pui-knob__icon">{icon(value)}</div>}
              </div>
            </>
          ) : isScale ? (
            <>
              <div className="pui-knob__scale-ring" />
              <div className="pui-knob__scale-cap">
                {pointer && <Pointer angle={angle} diameter={size} />}
                {icon && <div className="pui-knob__icon">{icon(value)}</div>}
              </div>
            </>
          ) : (
            <>
              <Collar radius={radius} angle={angle} />
              <div className="pui-knob__cap">
                {pointer && <div className="pui-knob__dot" style={{ transform: `rotate(${angle}deg)` }} />}
                {icon && <div className="pui-knob__icon">{icon(value)}</div>}
              </div>
            </>
          )}
        </div>
      </div>

      {/* The value replaces the caption in place while dragging, rather than
          appearing as a second line below it - a second line meant the row's
          height (and everything below it in the grid) changed the instant
          you touched a knob. */}
      {!bare && (
        <div className="pui-caption pui-knob__caption">
          {dragging && valueLabel ? <StableDigits>{valueLabel}</StableDigits> : caption}
        </div>
      )}

      {/* Unlike valueLabel above, this is a second, permanent line - Mix/
          Feedback show their value here at all times, not only mid-drag
          (COMPONENTS.md: "value line 4px under the caption"). Only rendered
          when a caller actually passes one, so every other knob's layout is
          untouched. */}
      {!bare && subLabel && (
        <div className="pui-knob__sublabel">
          <StableDigits>{subLabel}</StableDigits>
        </div>
      )}
    </div>
  );
}
