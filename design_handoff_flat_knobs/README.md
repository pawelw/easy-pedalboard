# Handoff: flat knob treatments (2a–2f)

## Overview

Six indicator treatments for the dark flat plugin style. All six share one knob
body — a raised turned rim with a shallow concave face — and differ only in how
the value is drawn. They are candidates, not a set: pick one as the house knob
(2e is a plausible second for parameters that should recede).

| id | Name | Character |
| --- | --- | --- |
| 2a | Hugging arc | Thick accent arc 2px outside the cap. The reference style. |
| 2b | Inlaid ring | Arc sunk into the cap face; nothing spills outside the circle. |
| 2c | Segmented | Same arc cut into 14 blocks — a countable scale without a tick ring. |
| 2d | Cursor | Neutral full track, short accent block at the value. Calmest in dense rows. |
| 2e | Accent slot | No ring; the pointer carries the colour, two rim dots mark the travel ends. |
| 2f | Full sweep | 340° ring with a 20° break at the bottom; reads as one closed object. |

## About the design files

`Knob Explorations Flat.dc.html` is a **design reference written in HTML** — a
prototype of the intended look, not production code. Recreate it in the app as a
variant of the existing `Knob` component (`packages/pedal-ui/src/Knob.jsx` +
`Knob.css`), driving colours through the CSS-variable tokens rather than copying
the inline styles. The prototype inlines everything only because the design tool
requires it.

Knobs are static in the prototype. Sizes shown are 52px; every value below scales
linearly with the diameter unless noted.

## Fidelity

**High-fidelity.** Geometry, colours and shadow stacks are final — reproduce them
exactly. Values are quoted verbatim below so nothing has to be measured off the
screen.

## The knob body (identical in all six)

Two nested circles.

**Outer rim — the raised body.** 52×52, `border-radius 50%`.

```
background: linear-gradient(180deg, #313d42 0%, #1b2226 38%, #0f1417 100%);
box-shadow: 0 5px 11px rgba(0,0,0,.55),
            0 1px 2px rgba(0,0,0,.6),
            inset 0 1px 0 rgba(222,242,248,.38);
```

**Inner face — the shallow dish.** Inset 3px from the rim (4px on 2f),
`border-radius 50%`.

```
background: radial-gradient(135% 135% at 50% 118%, #232c31 0%, #1a2124 55%, #151b1e 100%);
box-shadow: inset 0 3px 5px rgba(0,0,0,.42),
            inset 0 -1px 0 rgba(214,236,243,.16);
```

The light source is top-centre: the rim catches it on its upper edge, the dish
shades at the top and bounces a thin highlight off its lower lip. Keep the dip
shallow — a deeper `inset` shadow reads as a hole rather than a machined face.

**Pointer.** A 3px rounded bar on the rotating layer, `background #e6f0f4`,
`box-shadow 0 1px 2px rgba(0,0,0,.55)`, positioned from 16% to 42% of the face
height. 2b uses 28%→50% (the arc occupies the rim); 2e uses 14%→48% in
`#e04036`.

**Travel.** −135° to +135°, i.e. 270°. Rotation is
`rotate(-135deg + value × 270deg)`, applied to a full-size layer inside the face
with `transform-origin: 50% 50%` — never to the face itself, or the dish gradient
would spin with it.

## Indicator geometry

All arcs are SVG `<path>` elements on a viewBox centred at the origin, absolutely
positioned and centred over the knob (`top:50%; left:50%;
transform:translate(-50%,-50%)`). Angles use the convention **0° = 12 o'clock**;
a point at radius `r` and angle `d` is `(cos((d−90)°)·r, sin((d−90)°)·r)`. The
viewBox half-size is `r + pad`, so the stroke is never clipped.

| id | Radius | Pad | Stroke | Cap | Track colour | Lit colour |
| --- | --- | --- | --- | --- | --- | --- |
| 2a | 28.5 | 3 | 5 | round | `#1c2427` | `#d0342c` |
| 2b | 22 | 2 | 4 | butt | `#0e1214` | `#d0342c` |
| 2c | 28.5 | 3 | 5 | butt | `#1c2427` | `#d0342c` |
| 2d | 28.5 | 3 | 2 track / 5 cursor | round | `#2a343a` | `#d0342c` |
| 2e | 30 | 3 | — (dots `r 1.4`) | — | `#2a343a` | — |
| 2f | 24.5 | 2 | 3 | butt | `#1c2427` | `#d0342c` |

Per-treatment rules:

- **2a / 2b** — track spans the full −135…135; the lit path spans −135…value.
- **2c** — 14 segments of `270/14 = 19.29°`, each shortened by a `4.5°` gap
  (2.25° at each end). Segments `0…round(value × 13)` are lit, the rest take the
  track colour. Draw all unlit segments first, then the lit ones.
- **2d** — the cursor is a fixed **14°** block centred on the value, clamped to
  `[−135 + 7, 135 − 7]` so it never overruns either end of the travel.
- **2e** — no arc. Two `r 1.4` dots at −135° and +135° on `r 30`, fill `#2a343a`.
- **2f** — the ring runs **−170° to +170°** (340°) with a 20° break at the bottom;
  the lit path is `−170 … −170 + value × 340`. Note the pointer still uses the
  270° mapping, so on this treatment the ring and the pointer are intentionally
  on different scales — if that bothers you in build, move the pointer to 340° too.

Zero-length arcs must render as an empty `d`, not a degenerate path: skip the path
when `|to − from| < 0.4°`.

## Colour tokens

| Token | Value | Use |
| --- | --- | --- |
| page | `#0e1214` | outermost background |
| panel | `#20292e` | card / module surface |
| accent | `#d0342c` | lit arc, badges |
| accent bright | `#e04036` | 2e pointer |
| pointer | `#e6f0f4` | pointer bar |
| track dark | `#1c2427` | unlit arc on panel |
| track deep | `#0e1214` | unlit arc inlaid in the cap (2b) |
| track light | `#2a343a` | neutral full track (2d), rim dots (2e) |
| text primary | `#dce6ea` | titles |
| text secondary | `#8ba3a9` | body copy, value captions |

The accent is per-module in the host — swap `#d0342c` for the module hue and keep
every other value.

## Type

**Space Grotesk**, 500 and 700 only. Card title 11px/700 uppercase
`letter-spacing .16em`; body 11px/500 `line-height 1.5`; value caption 9px/500
with `font-variant-numeric: tabular-nums`; badge 9px/700 `letter-spacing .12em`
on an accent chip, `border-radius 4px`, `padding 2px 6px`.

## Interaction (not in the prototype)

Use the existing pedal-ui gesture: vertical drag to change, `shift` for fine,
double-click to reset to default. On hover, the rim highlight can lift from `.38`
to `.5` alpha; on drag, the lit arc should not change colour — motion is enough
feedback.

## Files

- `Knob Explorations Flat.dc.html` — the prototype (opens directly in a browser)
- `support.js` — runtime the prototype needs; not part of the design

Related: `design_handoff_peak_machine/` holds the full three-module host that
these knobs are destined for. Its knobs currently use the older bevelled cap and
will need updating once a treatment is chosen.
