# Handoff: Peak Delay — onyx WebView face + two-theme `pedal-ui`

## Overview

Peak Delay gets a React/CSS WebView face (like `plugins/peak-wah/jsui`) built on a
**new layout** (stereo tap scope, small time knobs paired with recessed readouts,
pre/post-stage strip) and a **new dark "onyx" theme** for `@synthpeak/pedal-ui`.

Two hard constraints from the owner:

1. **Peak Wah must keep the exact look it has today.** The current `pedal-ui`
   tokens stay the default theme. Nothing about Wah's rendering may change.
2. **Theme is chosen in code, not in the UI.** Delay ships on `onyx`; the same
   Delay face must render correctly on the light theme by changing one prop.

## About the design files

`peak-delay-onyx-face.html` is a **design reference**, not production code — a
static HTML/CSS prototype of the intended face at exact size and colour.
`full-canvas-all-options.dc.html` holds four explored directions; **implement
only the onyx one (`1c`)**. Recreate them as React components in
`packages/pedal-ui` following that package's existing conventions
(CSS-variable tokens, `pui-` class names, one `.css` file per component,
controlled 0..1 value props, JUCE-agnostic — bindings live in the plugin's
`juceBindings.jsx`). Do not copy inline styles into JSX: every colour in the
prototype maps to a token in `TOKENS.md`.

## Fidelity

**High fidelity.** Colours, sizes, type and shadows in `TOKENS.md` /
`COMPONENTS.md` are final and measured from the prototype. Match them.

## Screens / views

One screen: the Peak Delay plugin face, 568 px wide (content 516 px), height
follows content (~430 px).

Top to bottom:

1. **Header** — Peak mark (22 px, tinted to `--pui-ink`), title "Peak Delay"
   (19px/700), right-aligned meta line "STEREO · 1/8 · 1/8T" (9px/500,
   letter-spacing .18em, uppercase, `--pui-ink-soft`). 20 px below.
2. **Tap scope** — 78 px tall recessed well showing the stereo repeat pattern:
   left-channel taps above the centre line, right below, decaying left→right.
3. **Control row** (gap 24 px): `Mix` knob 84 px · `Feedback` knob 84 px ·
   a 236 px column holding the two time rows and the Sync/ms pills.
4. **Time rows** (gap 22 px) — a 42 px knob + a recessed readout showing
   `LEFT / 1/8 / 250 ms` and `RIGHT / 1/8T / 167 ms`. The knob is what makes it
   visibly changeable; the readout is the value.
5. **Link brace + time-unit switch** — the `sync` parameter is a **brace**, not
   a pill: a vertical spine left of the two time rows with an arm reaching each
   time knob and a lit 26 px square link button at its centre, so the control
   visibly ties LEFT and RIGHT together. Below the rows, `TIME IN` labels a
   two-segment `SYNC | MS` switch (the `timeunit` parameter) — it reads as
   flipping the whole time section between note divisions and milliseconds,
   which a single `MS` pill did not.
6. **Split footer** — one row above a hairline divider, halved, one stage per
   half. The left half is a full-bleed green band (the tape stage — a selling
   point, so it gets its own colour) with the reel-to-reel icon, the name
   `TAPE`, a `‹ PRE ›` router, and two 42 px knobs: `WEAR` and `FLUTTER`. The
   right half keeps the onyx panel with the phased-sine icon, `MOD`, a
   `‹ POST ›` router, and `CHORUS` + `PHASER`. Identical layout, different
   tone. Each section's placement in the signal chain is **user-configurable**
   via its router — the labels are no longer fixed `PRE-STAGE`/`POST-STAGE`
   text. All stage controls are knobs, not sliders.
   The band bleeds to the card's left and bottom edges, so the card shell is
   `overflow:hidden` with its padding on an inner div.

Exact geometry per component: `COMPONENTS.md`. Exact colours: `TOKENS.md`.

## Interactions & behaviour

- Knobs: vertical drag, pointer-lock, arrow keys — all existing `Knob`
  behaviour, unchanged. 270° of travel, −135°..+135°.
- Sliders: horizontal drag anywhere on the track; arrow keys nudge.
- `LINKED` (sync): when on, moving either time knob moves the other — the
  existing C++ mirroring in `PeakDelayProcessor::parameterChanged` already does
  this; the UI only reflects it.
- `MS`: swaps both readouts from note division to milliseconds at host tempo.
  The text comes from C++ (`PeakDelayProcessor::timeReadout`) over the
  `formatKnobValue` native function — do not reimplement division→ms in JS.
- Tap scope: taps fade with a CSS animation (`tapfade`, 2.4 s linear infinite,
  opacity .95 → .08, 0.18 s stagger per tap). Tap count/spacing/decay should be
  derived from `feedback` and the two time values (see COMPONENTS.md); if the
  live host feed isn't wired, the static decay is acceptable for v1.
- Every knob prints its value in place of its caption while dragging (existing
  `Knob` behaviour) — the time knobs instead update their readout.
- Stage routers: each arrow steps that section's placement between `PRE` and
  `POST` and wraps; both arrows act on the same two-state parameter. Changing
  one section does not move the other — both may sit on the same side.

## State management

No new app state. Every control is an existing parameter:
`mix`, `fb`, `ltime`, `rtime`, `sync`, `timeunit`, `mod`, `tape` —
bound through `Juce.getSliderState` / `getToggleState` exactly as
`plugins/peak-wah/jsui/src/juceBindings.jsx` does.

**The two stage sections need new parameters and real DSP work.** Today
`PeakDelayProcessor` has one `tape` and one `mod` parameter, and the chain
order is fixed. The face asks for:

| control | parameter | status |
| --- | --- | --- |
| WEAR | `tape` | exists — remap the label |
| FLUTTER | `flutter` | **new** float 0..1, default .44, `percentToText` |
| CHORUS | `mod` | exists — remap the label |
| PHASER | `phaser` | **new** float 0..1, default .18, `percentToText` |
| TAPE `‹ PRE ›` | `tapepre` | **new** bool/choice, default pre |
| MOD `‹ POST ›` | `modpost` | **new** bool/choice, default post |

The two routing parameters are the substantial part: the tape stage and the
modulation stage must each be movable to either side of the delay line, which
means restructuring `processBlock`, not just adding a flag. Scope that before
committing to it.

If the DSP work lands later, still build the full face and bind the four new
controls to placeholder parameters, flagged in code — do not silently drop a
knob or a router. The footer's 50/50 balance depends on both pairs, and the
routers are what make the stages legible as a chain.

## Assets

- `peak-logo.png` — already in the repo at `packages/pedal-ui/src/peak-logo.png`.
  Use the existing `Logo` component; it needs a token-driven tint so the mark
  can be light on the onyx panel (today it hard-codes `brightness(0)`).
- Two new line icons ship with the face — `TapeIcon` (reel-to-reel deck) and
  `ModIcon` (three phase-offset sine waves). Geometry in `COMPONENTS.md` §8b.
- Chain-link icon: currently only exists as C++ vector drawing
  (`drawLinkIcon` in `plugins/peak-delay/src/PluginProcessor.cpp`). Port it to a
  `LinkIcon` React component — two rounded capsules on a 45° axis (geometry in
  `COMPONENTS.md`).

## Files

- `peak-delay-onyx-face.html` — the face to build (open in a browser).
- `full-canvas-all-options.dc.html` — all four explored directions, for context.
- `TOKENS.md` — the two themes' token tables.
- `COMPONENTS.md` — per-component geometry, API and CSS recipes.
- `BUILD_PLAN.md` — the five phases, in order, with acceptance checks.
