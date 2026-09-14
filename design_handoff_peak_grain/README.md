# Peak Grain — onyx face handoff

Peak Grain redrawn on the web face the other plugins moved to (`packages/pedal-ui`
+ Peak Alpine's host chrome), replacing the native cream/pastel JUCE face in
`plugins/peak-grain/src/PluginProcessor.cpp`.

Peak Grain has **no draggable modules**, so nothing here reuses Alpine's
`ChainSlot` / dnd-kit wiring. The five parts of the pedal are plain sections in
a fixed grid.

## Files

| File | What |
|------|------|
| `Peak Grain.dc.html` | The design doc: three arrangements, `1a` / `1b` / `1c` |
| `Grain Header.dc.html` | The host header (logo, title, preset bar, Live/Freeze, Level, bypass) |
| `Grain Knob.dc.html` | One knob — both variants (`soft`, and `scale` via `ticks`) |
| `Grain Scope.dc.html` | The recessed grain/delay scope strip |
| `support.js` | Design Components runtime (needed to open the `.dc.html` files) |
| `assets/peak-logo.png` | `packages/pedal-ui/src/peak-logo.png`, unmodified |

Open `Peak Grain.dc.html` in a browser. Nothing is interactive — knob positions
are static values chosen to show the arcs and tick rings at work.

## The three options

- **1a — Alpine-faithful.** Five `ModulePanel`-style panels with header strips,
  three across over two, scope full width underneath. Closest to Peak Alpine's
  own row; least new code.
- **1b — Ruled captions.** No header strips: each section is named on a 2px
  accent keyline over its knobs, on a lighter `#1b2225` card.
- **1c — One plate (the developed one).** All five sections in a single panel
  divided by hairlines, each with its own recessed display and lead knobs, in
  the idiom of the Peak Delay face. **Build this one** unless told otherwise.

## What 1c contains

Header (two rows, because the pedal is ~700px wide and Alpine's single-row
header only fits at ~2100px — mirror `Card`'s `headerCenterPlacement="below"`):

1. Logo + `Peak Grain` + tagline, preset bar (`PresetBar variant="separated"`)
   right.
2. `Live / Freeze` segmented pair left; `Level` fader + bypass `PowerToggle`
   right.

Plate, row 1 — three equal columns, hairline `#0a0b0c` dividers:

| Section | Display | Knobs | Other |
|---------|---------|-------|-------|
| Grain | grain envelope curve | Mix (48px, tick ring, value line), Size / Destiny / Shape (46px soft) | `MS`/`SYNC` pill pair in the header |
| Pitch | three weight bars (Low / Unison / High) | Low, Unison, High, Detune (bipolar) | `WEIGHTS` corner label |
| Random | L/R stereo field, scattered grains | Stereo, Reverse, Scatter | `FIELD` corner label |

Plate, row 2 — `1.95fr / 1fr`:

| Section | Contents |
|---------|----------|
| Delay | `PowerToggle` + `MS`/`SYNC`; Mix and Feedback (52px, tick rings, value lines); link bracket + chain button; Left / Right time knobs (36px, tick rings) each with a recessed readout (`1/8` · `198 ms`); type pills `NORMAL` / `WIDE` / `PING PONG` |
| Reverb | `PowerToggle`; falling-tail bar display; Decay / Low Cut / Mix |

Then the full-width scope strip: grain cloud left, delay repeats fading right
over a faint reverb wash, `GRAINS` / `DELAY` labels in recess ink.

## Parameters

Every control maps to an existing parameter id in
`plugins/peak-grain/src/PluginProcessor.cpp` unless marked NEW.

| Control | Parameter |
|---------|-----------|
| Grain — Mix, Size, Destiny, Shape | `mix`, `size`, `density`, `shape` |
| Grain — MS / Sync | `sizeSync` (mirrors onto `densitySync`, see `onGrainSyncToggled`) |
| Pitch — Low, Unison, High, Detune | `pitchLow`, `pitchUnison`, `pitchHigh`, `detune` |
| Random — Stereo, Reverse, Scatter | `stereo`, `reverse`, `scatter` |
| Delay — Mix, Feedback | `delayMix`, `delayFeedback` |
| Delay — Left / Right time | `delayTime` + **NEW** a second time id; sync flag `delaySync` |
| Delay — type pills | **NEW** (`Normal` / `Wide` / `Ping Pong`, as Peak Delay's type button) |
| Delay / Reverb power | `delayOn`, `reverbOn` |
| Reverb — Decay, Mix | `decay`, `reverbMix` |
| Reverb — Low Cut | **NEW** |
| Live / Freeze | `freeze` |
| Level, bypass | `level`, `on` |

Not on the face: `stretch` (freeze-only) — left off deliberately, as today.
Grain / Pitch / Random keep no power toggle; only Delay and Reverb can be
switched off, so `grainOn` / `pitchOn` / `randomOn` lose their controls.

The old face's five section glyphs are C++ drawing code
(`drawGrainIcon`, `drawPitchIcon`, `drawRandomIcon`, `drawTapeIcon`,
`drawReverbIcon`). They are **not** ported here: Grain / Pitch / Random wear a
plain accent chip as a placeholder. Port those five as SVG marks the way
`TapeIcon.jsx` / `SpaceIcon.jsx` were, and drop them in place of the chip.

## Building it for real

Nothing in 1c needs a new component in `packages/pedal-ui`:

- panel → `Card` (onyx) + one `ModulePanel`-styled plate, or five `ModulePanel`s for 1a
- knobs → `Knob` `variant="flat"` (46px) and `variant="scale"` (36–52px)
- displays → the same recessed treatment as `BarDisplay` / `TapScope`
- readouts → `Readout`, and the Left/Right pair is `delay-face`'s `TimeControl`
- pills → `Pill`; toggles → `PowerToggle`; preset bar → `JucePresetBar variant="separated"`
- header icons → `PowerIcon`, `Chevron`, `SaveIcon`, `DiceIcon`, `LinkIcon`

The Left/Right block, its link bracket and the two readouts should be lifted
from `packages/delay-face` (`TimeControl.jsx`, `DelayFace.css`) rather than
rebuilt — this design is drawn to match it, and the bracket geometry in the
mock is an approximation of that CSS.
