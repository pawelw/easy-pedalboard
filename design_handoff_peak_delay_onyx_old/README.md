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
5. **Pill row** — `LINKED` (chain-link icon + label, lit = filled) and `MS`
   (outline when off). These are the `sync` and `timeunit` parameters.
6. **Stage strip** — hairline divider, then `PRE-STAGE` → Tape slider,
   `POST-STAGE` → Mod slider, each with a live percentage.

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

## State management

No new app state. Every control is an existing parameter:
`mix`, `fb`, `ltime`, `rtime`, `sync`, `timeunit`, `mod`, `tape` —
bound through `Juce.getSliderState` / `getToggleState` exactly as
`plugins/peak-wah/jsui/src/juceBindings.jsx` does.

## Assets

- `peak-logo.png` — already in the repo at `packages/pedal-ui/src/peak-logo.png`.
  Use the existing `Logo` component; it needs a token-driven tint so the mark
  can be light on the onyx panel (today it hard-codes `brightness(0)`).
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
