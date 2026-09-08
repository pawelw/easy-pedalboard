# Peak Machine — implementation plan

A twelfth plugin, `plugins/peak-machine`: one host panel wrapping three effect
modules — **Modulation** (Tape / Tremolo / Chorus / Phaser), **Delay** (the
existing Peak Delay face and chain, embedded), **Reverb** (Space / Spring).

Design handover: `design_handoff_peak_machine/README.md`, prototype at
`design_handoff_peak_machine/Peak Multi Host.dc.html` (serve it — the file
needs its sibling `support.js`, so a `file://` open renders raw `{{ }}`
templates; `.claude/launch.json`'s `design-handoff` entry puts it on port 3200).

Status: **stage 1 built** (see §6). `packages/pedal-ui` carries the host shell;
the gallery's `#components` page renders it in all three themes and every
measurement below matches the prototype exactly. Stages 2-10 not started.

None of the eleven existing pedals is replaced. Peak Delay in particular keeps
shipping exactly as it does today; Peak Machine holds a second instance of the
same chain and renders the same face component.

---

## 1. The shape of it

Three problems, and they are mostly independent:

1. **The DSP is scattered across six processors.** Four of the seven engines
   already exist as reusable `ee::dsp::` classes. Tremolo does not — it is
   ~250 lines inline in `PeakTremPanProcessor::processBlock`. Neither does the
   *delay chain*: `ee::dsp::TapeDelay` is only the delay line, and the tape
   pre/post placement, the filter pair, the phaser and the routing that make
   Peak Delay sound like Peak Delay all live in `PeakDelayProcessor`.
2. **The face is new chrome around a face that already exists.** The Delay
   module is `plugins/peak-delay/jsui/src/App.jsx` minus its Card; everything
   around it — host panel, module shell, engine stepper, power toggles, bar
   displays — is genuinely new and belongs in `packages/pedal-ui`.
3. **The two halves meet at a ~47-parameter APVTS** and a web editor with that
   many relays.

So: build a shared module layer in C++, a shared face component in JS, and let
`peak-machine` be thin at both ends.

### Signal chain

```
in ─▶ [in trim] ─▶ Modulation ─▶ Delay ─▶ Reverb ─▶ [out trim] ─▶ out
                        │           │         │
                 each module: engine → level → its own dry/wet mix,
                 with its own power toggle crossfading to its own dry
```

Global bypass wraps the lot with `ee::plugin::crossfadeToDry` against a dry
copy taken before the in trim, so a bypassed Machine is unity whatever the
trims say — same contract every other pedal has.

Module order is fixed. Nothing in the design offers to reorder it, and a
router would be a second, larger feature.

---

## 2. Reuse inventory

What already exists and is used as-is:

| Need | Already there |
|---|---|
| Tape engine | `ee::dsp::TapeMachine` (Wear, Flutter, Saturation, Noise, Tone, Stereo) |
| Chorus engine | `ee::dsp::Chorus` (Rate, Depth, Phase, Mix) |
| Phaser engine | `ee::dsp::Phaser` (Rate, Depth) |
| Space reverb | `ee::dsp::FdnReverb` (Decay, Shimmer, Low Cut, Resonance) |
| Spring reverb | `ee::dsp::SpringReverb` (Decay — see §3.3) |
| Delay line | `ee::dsp::TapeDelay` + `TapeTransport` + `TapeCharacter` |
| Tempo divisions | `ee::dsp::TempoDivision`, `plugins/peak-delay/src/TimeMap.h` |
| LFO shapes | `ee::dsp::lfoValue` |
| Bypass crossfade | `ee::plugin::crossfadeToDry` |
| Presets | `ee::plugin::PresetStore` + `presetBridge` + `JucePresetBar` |
| WebView plumbing | `ee::plugin::webface`, `PeakDelayWebEditor` as the template |
| Plugin boilerplate | `peak_add_plugin(... WEBVIEW)` |
| Onyx palette | `[data-pui-theme="onyx"]` in `tokens.css` — already the design's greys |
| Knobs | `Knob` `variant="soft"` / `"scale"`, `StageControl`, `StageGroup` |
| IN/OUT faders | `Slider compact fine orientation="horizontal"` — already 4px rail, round thumb, 20px label, 46px readout, exactly the handover's spec |
| Tap scope | `TapScope` |
| Tape/Mod/Filter glyphs | `TapeIcon`, `ModIcon`, `FilterIcon` |

The onyx theme's tokens already **are** the handover's palette (`--pui-soft-track:
#2b3639`, `--pui-tick: #39474b`, `--pui-tick-lit: #b4b4b4`, `--pui-soft-lit:
#a3ce7a`). Per-module accents are one override of `--pui-soft-lit` on the module
element — which is exactly what the prototype does. **No new theme is needed.**

---

## 3. New shared C++ — `shared/include/ee/fx/`

A module layer above `ee::dsp::`: an engine, plus the per-block glue a pedal's
`processBlock` currently carries (smoothing, dry/wet, engage crossfade, tempo).
Header-only where it can be; `.cpp` in `shared/src/fx/` added to `ee_dsp` where
it can't.

```cpp
namespace ee::fx
{
/** What every module needs from the host, once per block. */
struct BlockContext
{
    double sampleRate = 44100.0;
    double bpm        = 120.0;
    bool   playing    = false;
    double ppqPosition = 0.0;   // for phase-locking an LFO to the transport
    int    numSamples = 0;
};
}
```

Each module is a plain class — no virtuals, no allocation on the audio thread:

```cpp
void prepare (double sampleRate, int maxBlock);
void reset() noexcept;
void process (juce::AudioBuffer<float>&, const BlockContext&) noexcept;
double tailSeconds() const noexcept;
```

**Engine switching.** A module holds *every* one of its engines, all prepared,
but runs only the selected one. On a switch it runs both for an equal-power
crossfade of `ee::plugin::kRampSeconds * 2`, then `reset()`s the outgoing one so
its tail cannot reappear later. Parameter values are per-engine and live in the
APVTS (§4), so Chorus → Phaser → Chorus restores the Chorus settings for free —
the handover requires this and it falls out of the parameter layout rather than
needing a store in the module.

### 3.1 `ee::dsp::Tremolo` — new, extracted

Lifted verbatim from `PeakTremPanProcessor::processBlock`: the shaped LFO
(`ee::dsp::lfoValue` + the slew), the transport phase-lock (`ppqPosition`
tracking with the jump test and the 0.006-per-block correction limit), the
depth/make-up/bias smoothing, the bias-tube duck-skew and its asymmetric drive
with the DC blocker, the equal-power panning branch, and the non-finite
self-heal. All of it, unchanged.

`PeakTremPanProcessor` then becomes a parameter → engine adapter.

- Guarded by `ee_trempan_stress` (existing) **plus** a new `tests/TremPanMatch.cpp`
  → `ee_trempan_match`, modelled on `DelayMatch.cpp`: renders a WAV through the
  real processor. Render before the refactor, render after, diff. Sample-exact
  is the bar — this is a move, not a rewrite.

### 3.2 `ee::fx::DelayModule` — new, extracted

The whole of Peak Delay's chain: `TapeDelay`, the two `TapeSection`s and their
placement crossfade, `trimmedDelaySeconds`, the `IIR` filter pair with its
resting-position bypasses, the in-loop `Phaser`, `Routing`, the input meter
(`inputLevelUi` / `strikeCountUi`), and the time mapping (`TimeMap.h` moves to
`shared/include/ee/fx/DelayTimeMap.h`).

Setters take real units, not normalised values, so the module never learns what
an APVTS is:

```cpp
void setTimes (float leftSeconds, float rightSeconds) noexcept;
void setFeedback01 (float), setMix01 (float);
void setTape (float wear01, float flutter01, bool post) noexcept;
void setFilter (float loCutHz, float hiCutHz) noexcept;
void setDrift01 (float), setPhaser01 (float);
void setRouting (TapeDelay::Routing) noexcept;
```

`PeakDelayProcessor` keeps: its parameter layout, `timeReadout`/`timeMsReadout`/
`timeMs`, the L/R mirror listener, `installState`, `hostBpm`, and the tape-tuner
hooks. Its `processBlock` becomes read parameters → set on module → call it →
global bypass.

- Guarded by `ee_delay_match` (already exists, already renders a file through
  the whole processor). Same before/after WAV diff, same sample-exact bar.
- `ee_preset_tests` must stay green — it runs against `PeakDelayProcessor` and
  is the check that no parameter went missing.

### 3.3 `ee::dsp::SpringReverb` — three new controls

Spring's face needs **Tension**, **Low Cut** and **Reso**; the tank exposes only
decay and stereo today. Adding, per §3 of the answers:

| Control | What it drives |
|---|---|
| `setTension01` | the dispersion chirp coefficient — already a `SpringConfig.h` constant, promoted to a runtime range around its current value |
| `setLowCut (hz)` | the tank's existing `outputLowCutCoeff`, currently pinned by config |
| `setResonance01` | `LoopDamper` amount — how much top the wire loses per pass |

All three default to today's fixed values, so `SpringReverb` with nothing set is
bit-identical to the current one and **Peak Spring is untouched**. Only Peak
Machine drives them.

- Guarded by `ee_spring_match` (existing A/B renderer) at the defaults, and by
  `ee_reverb_stress` for tail stability across the new ranges.

### 3.4 `ee::fx::ModulationModule` and `ee::fx::ReverbModule` — new

Thin: hold their engines, own an engine index, a level, a mix and an engaged
flag, and do the crossfades described above.

| Module | Engines | Notes |
|---|---|---|
| `ModulationModule` | `TapeMachine`, `Tremolo`, `Chorus`, `Phaser` | Tape's Tone and Stereo, and Tremolo's Panning, stay at their defaults — not on the design's face. Tremolo's rate keeps the free-running Hz mapping; tempo sync is not exposed here (Peak Trem-Pan keeps it). |
| `ReverbModule` | `FdnReverb`, `SpringReverb` | Both are mono-in/stereo-out; the module sums to mono for the send exactly as `PeakReverbProcessor` and `PeakSpringProcessor` do. |

---

## 4. The plugin — `plugins/peak-machine`

```
plugins/peak-machine/
  CMakeLists.txt              peak_add_plugin(PeakMachine CODE Pmch ... WEBVIEW)
  presets/                    factory bank, categorised by " - " prefix
  src/PluginProcessor.{h,cpp}
  src/PeakMachineWebEditor.{h,cpp}
  jsui/                       Vite + React face, dev server port 3002
```

Registered in the top-level `CMakeLists.txt` `EE_ALL_PLUGINS` list.

### 4.1 Parameter layout — ~47 parameters

Dotted, module-scoped ids. APVTS ids are free-form strings (they are a property
*value* in the tree, not a property name) so dots are safe, and they let the JS
side build a whole module's ids from one prefix.

**Global (3):** `ingain`, `outgain`, `on`

**Modulation (17):**
`mod.on`, `mod.engine` (choice: Tape/Tremolo/Chorus/Phaser), `mod.level`, `mod.mix`
- Tape: `mod.tape.sat`, `.flutter`, `.wear`, `.noise`
- Tremolo: `mod.trem.amount`, `.rate`, `.shape`, `.tube`
- Chorus: `mod.chorus.rate`, `.depth`, `.phase`
- Phaser: `mod.phase.rate`, `.depth`

**Delay (15):** `dly.on`, then the leaf names Peak Delay already uses —
`dly.mix`, `dly.fb`, `dly.ltime`, `dly.rtime`, `dly.sync`, `dly.timeunit`,
`dly.dtype`, `dly.tape`, `dly.flutter`, `dly.mod`, `dly.phaser`, `dly.locut`,
`dly.hicut`, `dly.tapepre`

Leaf names kept identical on purpose: `<DelayFace prefix="dly.">` and Peak
Delay's own `prefix=""` are then the same component (§5.2). No `dly.level` and
no module Mix knob — the design's Delay header ends at the spacer and Delay has
its own 76px Mix.

**Reverb (12):**
`rev.on`, `rev.engine` (Space/Spring), `rev.level`, `rev.mix`
- Space: `rev.space.decay`, `.shimmer`, `.locut`, `.reso`
- Spring: `rev.spring.decay`, `.tension`, `.locut`, `.reso`

Ranges, skews, defaults and text formatters come from each source pedal
unchanged, so a Machine knob at 50 % sounds like that pedal's knob at 50 %.
Formatters go through `ee/plugin/ParamText.h` only where the source pedals
genuinely already share one — CLAUDE.md's warning about `hzToText`/`decibelsToText`
applies.

### 4.2 Processor

Holds three modules, one APVTS, one `PresetStore { apvts, "Peak Machine",
EE_FACTORY_PRESETS }`, the two trims, the global engage crossfade, and the
playhead BPM cache (`readPlayHeadBpm` lifted from Peak Delay — audio thread
only, for the reason its comment gives about Live).

`installState` is required, not optional: the Delay module keeps the L/R time
mirror, and `PresetStore::installState` is the bracket that stops a preset load
collapsing a deliberately-uneven pair. Same implementation as
`PeakDelayProcessor::installState`.

`getTailLengthSeconds()` = delay tail + reverb tail, since they are in series.

### 4.3 Web editor

`PeakMachineWebEditor` is `PeakDelayWebEditor` with more relays. Same
`SinglePageBrowser`, same `presetBridge` wrap, same `serveFromDist`, same
`reportContentSize` / `formatKnobValue` / `getDelayTimesMs` native functions,
same 45 Hz `machineMeter` event carrying `{ level, strikes, bpm }`.

~47 relays and attachments is a lot of boilerplate. Worth a small helper —
`ee::plugin::RelaySet`, a vector of `(WebSliderRelay, ParameterAttachment)`
pairs built from a list of ids — rather than 47 hand-written member pairs.
That helper is reusable by every future WebView pedal, so it goes in
`shared/include/ee/plugin/`.

Dev server port **3002** (Wah 3000, Delay 3001).

---

## 5. The face

### 5.1 New in `packages/pedal-ui`

Everything here must also get an entry on `apps/pedal-gallery/src/Components.jsx`,
per the handover.

| Component | What it is |
|---|---|
| `ModulePanel` | the module shell: header strip (power toggle · name · spacer · optional header knob), body, optional footer. `accent` sets `--pui-soft-lit` and `--pui-accent` on the wrapper, `tone="wide"` for the Delay module's darker fill and lighter border. |
| `PowerToggle` | the 22px round accent button. `variant="pill"` is the host header's `ACTIVE` ⇄ `BYPASSED` capsule — same glyph, same component. |
| `EngineStepper` | recessed 9px-radius well: chevron · icon · name · chevron. Wraps in both directions. |
| `BarDisplay` | the 63px recessed well with a bar set. `align="centre"` + a centre line is Tremolo's 26 bars; `align="bottom"` is Reverb's 7. One component, two callers, no caption. |
| `Chevron` | factored out of `StageRouter`, shared with `EngineStepper` and `PresetBar`. |
| Icons | `TremoloIcon`, `PhaserIcon`, `SpaceIcon`, `SpringIcon`, `PowerIcon`; `SaveIcon` factored out of `PresetBar.jsx` (`variant="chrome"` is the flatter 16-unit drawing the host header wants). Drawn to match `TapeIcon`/`ModIcon`/`FilterIcon`. **No `ChorusIcon`:** the mark the prototype draws for the Chorus engine is `ModIcon`, path for path. |
| `installAutoResize` | moved out of the two duplicate `jsui/src/autoSize.js` copies. |
| `@synthpeak/pedal-ui/juce` | a subpath export holding the generic JUCE bindings currently in `peak-delay/jsui/src/juceBindings.jsx`: `useJuceSliderValue`, `useJuceToggleValue`, `useJuceChoiceValue`, `JuceKnob`, `JuceFader`, `JucePill`, `JuceChoicePill`, `JuceStageKnob`, `JuceStageRouter`. A subpath, so `pedal-ui`'s main entry stays JUCE-free for the gallery. Takes an optional parameter-id prefix from context. |

Changed:

| Component | Change |
|---|---|
| `PresetBar` | `variant="separated"`: four discrete 8px-radius controls (28×28 prev, 28×28 next, 190×28 name, gap 6; then 28×28 save at gap 10) instead of today's joined segmented group. The joined look stays the default — Peak Delay and Wah keep it. |
| `Card` | one new prop, `subtitlePlacement="below"`, for the stacked title/tagline column. Everything else about the host panel (padding, 32px logo, 19px title, zero body padding) is scoped in the face's own CSS as `.pm-card`, exactly the way `.pd-card` already scopes Peak Delay's. |
| `Slider` | check the 13px thumb against the compact one; add a size token if it differs. Geometry is otherwise already the handover's. |

### 5.2 New package — `packages/delay-face`

Exports `<DelayFace prefix="" />`: the TapScope, the Mix/Feedback/time row with
its link bracket and pills, and the three-cell stage footer — **no Card, no
header**, so each host supplies its own chrome. Carries `TimeControl`, the
delay-specific hooks (`useDelayTimesMs`, `useTimeReadoutText`, `useDelayMeter`,
`useHostBpm`) and the layout CSS currently in `peak-delay/jsui/src/index.css`.

- `plugins/peak-delay/jsui/src/App.jsx` becomes `<Card …><DelayFace/></Card>` —
  a wrapper of about fifteen lines. **Its rendered output must not change.**
- `plugins/peak-machine/jsui` renders `<DelayFace prefix="dly." />` inside a
  `ModulePanel`.
- Verified by a **geometry fingerprint** rather than by eye: with the gallery on
  `#peak-delay`, walk every rendered descendant of `.pui-card` and hash
  `tag.class|left,top,width,height` relative to the card. The extraction is a
  move, so the hash must come back identical.

  Baseline, at a 700x620 viewport, `theme="grey"` as `pedals.js` sets it:
  **324 elements, card 626x481, full hash `733c7d76`, geometry-only
  `42296266`.**

  Taken *after* the footer restyle (§5.6), not before. The restyle moved one
  class name - `pd-footer__section--tape` is gone - so the pre-restyle hash
  `c69406fa` no longer applies; the element count and card box were unchanged
  either side of it, which is how we know that restyle was colour-only.

  (CLAUDE.md is explicit that the UI snapshot renderer has no baselines and that
  faces have to be diffed by hand. This is that, made checkable - a screenshot
  comparison would not have caught a 1px shift.)

### 5.3 `plugins/peak-machine/jsui`

Thin. `App.jsx` composes `HostPanel` chrome + three `ModulePanel`s; a
`ModulationModule.jsx` and a `ReverbModule.jsx` each own an engine table
(name → icon → knob rows) and render `JuceKnob`s from it; the Delay module is
one line. Engine tables are data, mirroring `MOD_PARAMS` / `REV_PARAMS` in the
prototype.

Global bypass dims the module row (`opacity`/`filter`), which the prototype
notes but does not model.

### 5.4 Gallery

- `apps/pedal-gallery/src/pedals.js` gains `{ slug: "peak-machine", face:
  PeakMachineFace, theme: "onyx" }`.
- `Components.jsx` gains a section per new component and an updated `PresetBar`
  entry, rendered in all three theme columns as the page already does.
- The gallery is where stage 2 below is verified — no JUCE, no build, hot reload.

---

## 6. Order of work

Each stage is independently verifiable and independently shippable.

| # | Stage | Verified by |
|---|---|---|
| 1 | ✅ `packages/pedal-ui`: new components, icons, `PresetBar` variant, `Card` prop, `Components.jsx` entries | gallery `#components`, three theme columns; every module-shell measurement and colour read back off the live DOM and diffed against the prototype's |
| 2 | `packages/delay-face`; Peak Delay's App.jsx reduced to a wrapper | gallery `#peak-delay` before/after screenshot diff — must be identical |
| 3 | `plugins/peak-machine/jsui`: the whole face, off local state, no JUCE | gallery `#peak-machine` vs the prototype at :3200, side by side |
| 4 | `ee::dsp::Tremolo` extracted; Peak Trem-Pan delegates | new `ee_trempan_match` WAV diff (sample-exact) + `ee_trempan_stress` |
| 5 | `ee::fx::DelayModule` extracted; Peak Delay delegates | `ee_delay_match` WAV diff (sample-exact) + `ee_preset_tests` |
| 6 | `SpringReverb`'s three new setters | `ee_spring_match` at defaults (sample-exact) + `ee_reverb_stress` |
| 7 | `ee::fx::ModulationModule` + `ReverbModule` | `ee_dsp_tests` additions; a `ee_machine_host` driving the real processor with ragged blocks, like `ee_grain_host` |
| 8 | `plugins/peak-machine`: processor, parameter layout, CMake | `cmake --preset fast -DEE_PLUGINS="peak-machine"`, Standalone launches and makes sound |
| 9 | `PeakMachineWebEditor` + `RelaySet`; face bound to real parameters | full `dev` build, `auval -v aufx Pmch Peak`, then **Ableton Live** — per the standing rule, DSP work isn't done until the AU/VST3 is rebuilt and Live is relaunched |
| 10 | Factory preset bank | `ee_preset_tests` extended to Peak Machine's bank (every parameter in every file) |

Stages 1–3 need no C++ build at all. Stages 4–6 are refactors of shipping code
and each ends green before the next starts.

---

### 5.5 Notes from stage 1, for stage 3

- **A `--pui-accent` fallback token cannot live in `:root`.** Writing
  `--pui-accent: var(--pui-ink)` there resolves against `:root`'s own ink at
  computed-value time and inherits down as that literal, so a theme's override
  is never consulted - a near-black toggle on the onyx face. The fallback has
  to be at the point of use: `var(--pui-accent, var(--pui-ink))`. Any further
  accent-aware component must do the same.
- **The module header overflows its right padding by 4px**, in the prototype as
  well as here: "MODULATION" measures 88px against an 84px budget, so the Level
  knob sits 8px from the module's inner edge rather than 12. Matched
  deliberately - it is what the design does, and the arc still clears the
  panel edge by 2px. A longer module name would clip.
- **The gap between a module's last knob row and its footer divider is 18px**,
  and in the prototype it is a `margin-top` on the footer rather than padding
  on the body. `ModulePanel`'s body is `flex: 1`, so in the real three-module
  row (which stretches to the Delay module's height) the body absorbs it and
  the margin never applies - the face only needs it if a module is ever laid
  out on its own.

### 5.6 Design revisions after the handoff

Three changes made against the prototype and ported to both sides. The handoff
README and `Peak Multi Host.dc.html` carry them, so the bundle stays the record.

1. **One panel for all three modules.** The Delay module's darker fill inside a
   lighter border is gone; every module is `#20292d` / `#161c1e` / `#0a0b0c`.
   `ModulePanel`'s `tone` survives as the wider padding a 598px module needs,
   and the `--pui-module-wide*` tokens were deleted rather than aliased.
2. **The Tape section's green band is gone**, and with it the whole
   `--pui-tape-*` group and `StageGroup`'s `tone` prop. All six footer knobs are
   now one control on one ground; the `PRE` stepper is an ordinary recessed
   switcher.
3. **Each footer section names itself with a coloured glyph** —
   `--pui-stage-tape` `#d8f088`, `--pui-stage-mod` `#7fb4e0`,
   `--pui-stage-filter` `#e08fc0` — via a new `accent` prop on `StageHeader`.

2 and 3 land on Peak Delay as well as Peak Machine: one Delay face, no variant,
which is what the handoff asks for. That is a visible change to a shipping
pedal, so its README shots are now out of date.

`TapeIcon`'s `bandColour` became `ground`: its reels have to be filled with
whatever panel is behind them, which was the band and is now the card on Peak
Delay and the module on Peak Machine. It reads `var(--pui-stage-ground,
var(--pui-panel))`, with `ModulePanel` setting the former - the point-of-use
fallback again, for the reason §5.5 gives.

**Open:** the three glyph hues were picked against the onyx panel they ship on.
On the light and grey themes - which only the gallery renders - the lime in
particular is low contrast. Same trade the module accents make, and worth a
look if a light Peak Machine is ever a real face.

Also noted for stage 3: `--pui-divider` is `#222b2e`, which was chosen against
the Delay panel's old `#131819`. On the module's `#20292d` it is all but
invisible, so the prototype's footer cell dividers are now `#2a3336`. The face
will want `.pui-module { --pui-divider: var(--pui-outline); }` or similar.

## 7. Risks, and what is done about them

**Refactoring three shipping pedals.** Stages 4, 5 and 6 change Peak Trem-Pan,
Peak Delay and Peak Spring. Each is a *move*, and each has an existing or new
A/B WAV renderer; the bar is sample-exact output, not "sounds the same". If a
diff is not exact the refactor is wrong, not the test.

**`ee_dsp_tests` is not green on a healthy tree.** The tape-at-100 % failure is
baseline and the chorus-silence one fails about half of runs. `scripts/dev-check.sh`
filters both. A chorus change needs several runs, not one — and the Modulation
module touches Chorus.

**`tests/UiSnapshot.cpp` duplicates every pedal's parameter layout.** It covers
the `ee::ui` faces, not the WebView ones, so Peak Machine adds nothing to it —
but stage 4's Trem-Pan work does touch a pedal that *is* in there, and the
snapshot will drift silently if its parameter mirror is not updated.

**CPU.** Peak Machine runs a tape machine or a tremolo, a full delay chain with
two tape sections and a phaser, and a 16-line FDN — in series, in one plugin.
Worth a measurement at stage 8 before the face work is finished, because the
answer might be "prepare the unselected reverb lazily" rather than "hold both".

**Every build tree installs into the same `~/Library/Audio/Plug-Ins`.** A
`build-fast/` iteration on peak-machine does not install, but a `dev` build of
it will overwrite whatever `build-au/` last put there. Check `lipo -archs`
before blaming the plugin.

**Face size.** The host panel is a fixed 1046px wide. `installAutoResize` reports
the card's real rendered size, so the editor follows — but the starting
`setSize` in the editor should be close (1054 × ~640) so the host doesn't
visibly jump.

---

## 8. Open questions

Nothing blocking. Two things worth a decision before stage 3:

- **Module Level vs module Mix.** The side modules carry both (a 24px Level in
  the header, a 38px Mix in the footer). Level is proposed as output trim on the
  wet path, Mix as dry/wet — but Tremolo has no meaningful dry/wet, so its Mix
  reads as depth-of-effect. Alternative: Mix on Tremolo blends the tremolo'd
  signal against the untouched one, which is what it does on the other three.
  The plan assumes the latter — consistent, and the knob is on the face either
  way.
- **Preset bank.** Peak Machine's factory presets are new content, not migrated.
  Six headline patches plus categorised ones, matching Peak Delay's shape.
