# Handoff: knob 4a — "Spoke"

## Overview

A line-art knob. No cap, no body, no shading: a thin static outline circle, a
continuous accent arc that grows with the value, and a hairline spoke from the
centre out to the arc. The whole control is four SVG strokes, which is why it
scales from a 24px header trim to a 76px hero knob without redrawing anything.

This replaces the bevelled/dished knob used elsewhere in the project. It is the
chosen treatment from the line-art round; `Knob Explorations Line Art.dc.html`
(included) shows it as **4a**, alongside the five alternatives that were rejected,
which are useful as context for why the pointer reaches the rim and why the track
is visible.

## About the design files

The included `.dc.html` is a **design reference written in HTML** — a prototype of
the intended look, not production code. Implement it as a variant of the existing
`Knob` component (`packages/pedal-ui/src/Knob.jsx` + `Knob.css`), with colours
coming from the CSS-variable tokens rather than the literals inlined here. The
prototype inlines styles only because the design tool requires it; it also renders
knobs statically, with no drag behaviour.

## Fidelity

**High-fidelity.** Every radius, stroke width and colour below is final and should
be reproduced exactly. The only intentionally open decision is sizing at the small
end (see *Sizing*).

## Geometry

One SVG per knob. The viewBox is centred on the origin; the knob is drawn around
`(0,0)` so the same markup works at any rendered size.

Angle convention: **0° = 12 o'clock**, positive clockwise. A point at radius `r`,
angle `d`:

```
x = cos((d − 90) × π/180) × r
y = sin((d − 90) × π/180) × r
```

Constants for the reference 56px rendering:

| Name | Value |
| --- | --- |
| travel | −135° → +135° (270°) |
| arc radius `r` | 25 |
| outline radius | `r − 8` = 17 |
| spoke outer end | `r − 5` = 20 (spoke runs from the centre) |
| viewBox padding | 3 → half-size 28, viewBox `-28 -28 56 56` |

Value angle: `a = −135 + value × 270`.

Draw order, back to front:

1. **Outline** — `<circle r="17">`, `stroke-width 1.2`, `#2a343a`.
2. **Track** — arc from −135° to +135° at `r`, `stroke-width 2.4`,
   `stroke-linecap round`, `#1c2427`.
3. **Lit arc** — arc from −135° to `a` at `r`, `stroke-width 2.4`,
   `stroke-linecap round`, accent.
4. **Spoke** — `<line>` from `(0,0)` to the point at radius 20, angle `a`,
   `stroke-width 1.6`, `stroke-linecap round`, `#8ba3a9`.

Arc path:

```
M x1 y1 A r r 0 <largeArc> <sweep> x2 y2
largeArc = |to − from| > 180 ? 1 : 0
sweep    = to > from ? 1 : 0
```

Emit **no path at all** when `|to − from| < 0.4°`, so a zero value renders as an
empty track rather than a dot artefact from the round linecap.

The spoke rotates; nothing else does. Do not implement it as a CSS-rotated
element inside the SVG — compute the endpoint and draw the line, so the stroke
stays crisp at every angle.

## Colours

| Role | Value | Notes |
| --- | --- | --- |
| lit arc | module accent (`#d0342c` in the prototype) | the only saturated element |
| unlit track | `#1c2427` | present but nearly silent |
| outline circle | `#2a343a` | |
| spoke | `#8ba3a9` | deliberately *not* white — the accent should win |
| panel behind | `#20292e` | |
| label | `#dce6ea` | 10–11px/700 uppercase, `letter-spacing .1em` |
| value caption | `#8ba3a9` | 9px/500, `font-variant-numeric: tabular-nums` |

In the host, the accent is per-module (Modulation / Delay / Reverb each have
their own). Everything else stays fixed.

## Sizing

All values above scale linearly with the rendered diameter; `r = diameter × 0.446`
reproduces the reference proportion. Stroke widths should **not** scale below
about 40px — hold the arc at 2.4 and the outline/spoke at 1.2/1.6, or the control
disappears. Above 60px, take the arc to 3 and the spoke to 2.

Suggested steps: 24px (header trims — consider dropping the outline circle at
this size), 40px (parameter rows), 56px (reference), 76px (hero controls).

## Bipolar variant

For parameters centred at zero (Shape, Phase, Pan), draw the lit arc from **0°**
to `a` instead of from −135°, and add a 1.4px detent tick in `#6c8288` at 0°,
spanning `r − 5` to `r − 1.5`. Everything else is unchanged.

## Interaction (not modelled in the prototype)

Reuse the existing pedal-ui gesture: vertical drag to change, `shift` for fine,
double-click to reset to the default. Because the control has no filled body, the
hit area must be an explicit transparent square at least the size of the viewBox,
and never smaller than 44px on touch.

Hover: lift the outline circle to `#39474b`. Active drag: no colour change — the
arc's motion is sufficient feedback.

## Files

- `Knob Explorations Line Art.dc.html` — the prototype; **4a** is the first card
- `support.js` — runtime the prototype needs; not part of the design

Related: `design_handoff_peak_machine/` holds the three-module host these knobs
are destined for, and `design_handoff_flat_knobs/` documents the earlier
solid-cap round this treatment replaces.
