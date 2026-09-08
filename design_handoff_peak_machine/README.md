# Handoff: Peak Machine — three-module host panel (Modulation · Delay · Reverb)

## Overview

Peak Machine is a single plugin host panel that replaces the standalone Peak Delay
face. One common chrome (logo, preset bar, IN/OUT trims, global bypass) wraps three
side-by-side effect modules:

1. **Modulation** (left, narrow) — four engines: Tape, Tremolo, Chorus, Phaser
2. **Delay** (center, wide) — the existing Peak Delay face, unchanged apart from
   module framing
3. **Reverb** (right, narrow) — two engines: Space, Spring

Each module carries one accent hue, applied **only** to its status LED, its header
rule, and the arc indicators of its own knobs. Everything else stays on the shared
onyx greys.

## About the Design Files

The files in this bundle are **design references created in HTML** — a prototype
showing intended look and behaviour, not production code to copy. The task is to
recreate this design inside the existing `easy-pedalboard` environment: React
function components in `plugins/*/jsui/src`, styling through the CSS-variable tokens
and component classes in `packages/pedal-ui/src`. Reuse the real `<Knob>`, `<Card>`,
`<StageGroup>`, `<StageHeader>`, `<StageControl>`, `<Readout>`, `<Pill>` and
`<PresetBar>` components rather than reproducing the inline styles in the prototype —
the prototype inlines everything only because the design tool requires it.

The Delay module in the prototype is a faithful re-draw of the current
`plugins/peak-delay/jsui/src/App.jsx`. **Do not rebuild it** — lift the existing
component into the new module shell.

## Fidelity

**High-fidelity.** Colours, type, spacing and knob geometry are final and are
quoted exactly below. Recreate pixel-for-pixel using the existing pedal-ui
components. Interaction is *not* modelled: knobs in the prototype are static, and
only the engine steppers and the bypass button respond to clicks.

## Layout

Outer host panel:

- Fixed width **1046px**, `background #171d20`, `border 1px solid #2a3336`,
  `border-radius 20px`, `box-shadow 0 20px 44px rgba(0,0,0,.4)`,
  `padding 20px 24px 22px`, page background `#0f1315`.
- Header row, then a `display:flex; gap:14px; align-items:stretch` module row.

Module row track widths: **186 / 598 / 186**. The two side modules are fixed; only
Delay carries the wide content.

### Host header (grid: three flex tracks)

| Track | Contents |
| --- | --- |
| left, `flex:1 1 0` | 22px logo mark (tinted to `#b9d3d9`) + `Peak Machine`, 19px/700, `#b9d3d9`, gap 11px |
| center, `flex:none` | preset stepper + name field + save button |
| right, `flex:1 1 0`, `justify-content:flex-end`, `gap:32px` | IN/OUT fader stack, then the bypass pill |

Preset cluster: two 26×26 chevron buttons and a 168×26 name field, joined into one
segmented control (`margin-left:-1px`, outer radii 5px, inner 0). All three:
`background #222b2e`, `border 1px solid #2a3336`, hover `background #171d20;
color #b9d3d9`. Name text 10px/500, `letter-spacing .04em`, truncating. A double
chevron in `#6c8288` sits 9px from its right edge. A separate 26×26 save button
follows at `margin-left:4px`, radius 5px.

Fader rows (IN then OUT, `gap:5px`): 20px uppercase label 8px/700
`letter-spacing .14em` `#8ba3a9`; a 104×18 track (4px rail, radius 999px,
`#39474b`, fill `#8ba3a9`, 13px round handle `#b9d3d9` with
`0 8px 18px rgba(0,0,0,.55)`); then a 46px right-aligned readout 9px/500 `#6c8288`,
`font-variant-numeric: tabular-nums`. Demo values 62% / `0.0 dB` and 58% /
`-1.5 dB`. The **32px gap** to the bypass pill is deliberate — do not collapse it.

Bypass pill: `padding 6px 11px`, `border-radius 999px`, `border 1px solid #2a3336`,
transparent fill, `#8ba3a9`, 9px/700 uppercase `letter-spacing .1em`, power glyph at
12px. Label toggles `ACTIVE` ⇄ `BYPASSED`.

### Module shell (all three)

- `background`: side modules `#20292d`; Delay `#131819`
- `border 1px solid #161c1e`, `border-radius 8px`, `overflow hidden`
- `box-shadow 0 6px 18px rgba(0,0,0,.38)`
- First child: a **2px solid accent bar** across the full width (no gradient)
- Header strip: `min-height 56px`, `border-bottom 1px solid #222b2e`,
  padding `13px 12px 12px` (side modules) / `13px 18px 12px` (Delay), `gap 10px`.
  Contents: 11px accent LED (`box-shadow 0 0 10px <accent 50%>`), module name
  11px/700 uppercase `letter-spacing .18em` `#b9d3d9`, a `flex:1` spacer, then the
  24px **Level** knob flush to the right edge with no text label.
- No drag handles anywhere.
- Body padding `14px 12px 0` (side) / `16px 18px 0` (Delay).
- Footer: `border-top 1px solid #222b2e`, `padding 14px 12px 16px`, holding the
  module's **Mix** knob, centred.

Accents: Modulation `#e0b23c`, Delay `#a3ce7a`, Reverb `#7fd2d8`.

### Modulation module

Engine stepper: `padding 5px 6px`, `border-radius 9px`, `background #101416`,
`box-shadow inset 0 2px 6px rgba(0,0,0,.6)`. 22px chevron buttons in the accent
colour, hover `rgba(255,255,255,.08)`; centre shows the engine icon (26×21, accent)
and the engine name 10px/700 uppercase `letter-spacing .16em` `#b9d3d9`.

Engine parameters (knob rows of two, **never more than two rows**):

| Engine | Knobs | Extra |
| --- | --- | --- |
| Tape | Saturation, Flutter, Wear, Noise | — |
| Tremolo | Amount, Rate, Shape, Tube | trem display above the knobs |
| Chorus | Rate, Depth, Phase | — |
| Phaser | Rate, Depth | — |

Trem display (Tremolo only): 76px tall, `border-radius 12px`, `background #101416`,
`box-shadow inset 0 2px 6px rgba(0,0,0,.6)`, a 1px `#233034` centre line, an 8px/700
`TREM` caption top-left, and 26 accent bars (3px wide, radius 2px, `opacity .85`)
whose heights trace `8 + 46·|sin(2π·i/25)|` px.

### Delay module

Unchanged from `peak-delay`. For reference the prototype reproduces: the 78px tap
scope (L/R rows, amber input marker, taps alternating above/below the centre line
with decaying opacity), 76px **Mix** and **Feedback** knobs on the 20-tick scale
ring with `35 %` readouts, the linked Left/Right time knobs (42px, scale ring)
joined by the brace and 29px link button feeding two readout fields
(`LEFT 1/8 250ms`, `RIGHT 1/8T 167ms`), the `SYNC` / `NORMAL` pills, and the
three-cell stage footer — **Tape** (lit, `background #375916`, arc track `#5a7f2a`,
arcs and pointers `#d8f088`, `PRE` position stepper), **Mod** and **Filter**.

### Reverb module

Same stepper pattern as Modulation. Decay display: 76px, same recessed treatment,
caption `DECAY 3.4 S` (Space) / `DECAY 1.8 S` (Spring), and seven accent bars
(4px, `opacity .8`) at heights 34/29/24/19/15/11/8 px, bottom-aligned.

| Engine | Row 1 | Row 2 | Footer |
| --- | --- | --- | --- |
| Space | Decay, Shimmer | Low Cut, Reso | Mix |
| Spring | Decay, Tension | Low Cut, Reso | Mix |

## Knobs

Two variants, both centred on a rotating cap and an SVG indicator ring drawn from
**−135° to +135°** (270° of travel).

**Cap.** `border-radius 50%`, `background linear-gradient(180deg,#333f43,#151a1d)`,
`box-shadow inset 0 -1px .5px rgba(0,0,0,.49), inset 0 .5px 1px rgba(185,211,217,.3),
0 6px 14px rgba(0,0,0,.5)`. Pointer: a 2.4px (3px on the 76px knobs) rounded bar,
`background #b9d3d9`, from 10% to 36% of the cap height, rotated to
`−135° + value·270°`.

**`soft` — continuous arc.** Default everywhere. One stroked arc at
`r = size/2 + 3.5`, `stroke-width 3` (4 at ≥60px), `stroke-linecap round`; unlit
track `#2b3639` (side modules) / `#39474b` (Delay footer), lit portion in the
indicator colour. `from="max"` reverses the fill so it lights from the top of the
travel — used by Delay's **High Cut**.

**`scale` — 20-tick ring.** Reserved for standout controls: Delay's Mix, Feedback
and the two time knobs. 20 radial ticks between `r` and `r + 4` (6 on ≥60px),
stroke 1.4 (2 on ≥60px); unlit `#39474b`, lit `#b4b4b4` up to `round(value·19)`.
Large knobs also sit on a `#0b0e10` disc with `0 8px 18px rgba(0,0,0,.55)` and an
inset cap at 8%.

**Indicator colours.** Modulation `#e0b23c`; Reverb `#7fd2d8`; Delay's Tape stage
`#d8f088`; **every other Delay knob — Mod, Filter, Mix, Feedback, time — `#b4b4b4`.**

Knob sizes: 24px in module headers, 38px in footers and Delay stage cells, 40px in
side-module parameter rows, 42px for Delay time, 76px for Mix/Feedback. Labels sit
3.5px under the knob at 10px/700 uppercase `letter-spacing .1em` `#b9d3d9`; the
Delay hero knobs use 11px labels plus a 10px/500 `#8ba3a9` value line.

## Interactions & behaviour

- **Engine steppers** cycle their engine list with wraparound in both directions.
  Changing the engine swaps the parameter set, the icon, the name, and (Modulation)
  shows or hides the trem display.
- **Bypass** toggles the label between `ACTIVE` and `BYPASSED`. In production it
  should also dim the module row.
- **Knobs** in the prototype are static. In the app they use the existing pedal-ui
  vertical-drag gesture: drag up increases, `shift` for fine, double-click resets.
- Hover states are defined only for the chrome buttons (chevrons, save, stepper
  arrows) as listed above.

## State

| State | Type | Notes |
| --- | --- | --- |
| `modEngine` | `"Tape" \| "Tremolo" \| "Chorus" \| "Phaser"` | drives Modulation params, icon, trem display |
| `reverbEngine` | `"Space" \| "Spring"` | drives Reverb params and decay caption |
| `bypassed` | boolean | global |
| per-knob values | 0–1 normalised | one store per module; the host maps to real units |

Parameter values must be **per-engine**, not shared: switching Chorus → Phaser and
back should restore the Chorus settings.

## Design tokens

Greys: `#0f1315` page · `#171d20` host · `#131819` Delay panel · `#20292d` side
panel · `#101416` recessed wells · `#0b0e10` knob disc · `#161c1e` panel border ·
`#222b2e` internal divider · `#2a3336` chrome border · `#233034` display centre
line · `#39474b` / `#2b3639` unlit arc · `#4d5f65` · `#6c8288` muted text ·
`#8ba3a9` secondary text · `#b9d3d9` primary text · `#b4b4b4` neutral indicator.

Accents: `#e0b23c` Modulation · `#a3ce7a` Delay · `#7fd2d8` Reverb ·
`#375916` / `#2b4410` / `#5a7f2a` / `#d8f088` / `#f2f7e6` Tape stage.

Radii: 20 host · 12 displays · 9 stepper · 10 readout · 8 panel · 5 chrome button ·
999 pill.

Shadows: `0 20px 44px rgba(0,0,0,.4)` host · `0 6px 18px rgba(0,0,0,.38)` panel ·
`inset 0 2px 6px rgba(0,0,0,.6)` recessed · `0 8px 18px rgba(0,0,0,.55)` large knob ·
`0 6px 14px rgba(0,0,0,.5)` small knob.

Type: **Space Grotesk**, 500 and 700 only. 19px title · 11px module name · 10–11px
knob label · 9–10px chrome · 8px micro caption. Uppercase tracking runs .1em (knob
labels, pills), .14em (micro captions), .16em (engine names), .18em (module names).

Spacing: 14px module gap · 18px header padding · 12/16px body padding · 18–22px
between knob rows.

## Assets

- `assets/peak-logo.png` — from `packages/pedal-ui/src/peak-logo.png`, tinted to
  `#b9d3d9` with a filter in the prototype. Prefer an SVG or a pre-tinted asset in
  production.
- All icons (tape reels, tremolo, chorus, phaser, space, spring, filter, link,
  power, save, chevrons) are inline SVG in the prototype. Tape/Mod/Filter come from
  `TapeIcon.jsx` / `ModIcon.jsx` / `FilterIcon.jsx` in pedal-ui and should be used
  from there. The tremolo, chorus, phaser, space and spring glyphs are **new** and
  drawn to match that family — replace them if the team has final artwork.

## Files

- `Peak Multi Host.dc.html` — the design prototype (open directly in a browser).
- `support.js` — runtime required by the prototype. Not part of the design.
- `assets/peak-logo.png`

Upstream source for the Delay module: `plugins/peak-delay/jsui/src/App.jsx`,
`index.css`, `TimeControl.jsx`; shared components in `packages/pedal-ui/src`
(`Knob.jsx`, `tokens.css`, `Card.css`, `StageGroup.css`, `StageHeader.css`,
`StageControl.css`, `Readout.css`, `Pill.css`, `PresetBar.css`, `Slider.css`).
