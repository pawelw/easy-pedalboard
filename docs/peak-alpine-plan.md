# Peak Alpine — implementation plan

A twelfth plugin, `plugins/peak-alpine`: one host panel wrapping three effect
modules — **Modulation** (Tape / Tremolo / Chorus / Phaser), **Delay** (the
existing Peak Delay face and chain, embedded), **Reverb** (Space / Spring).

Design handover: `design_handoff_peak_alpine/README.md`, prototype at
`design_handoff_peak_alpine/Peak Alpine.dc.html` (serve it — the file
needs its sibling `support.js`, so a `file://` open renders raw `{{ }}`
templates; `.claude/launch.json`'s `design-handoff` entry puts it on port 3200).

Status: **stages 1-9 built** (see §6). The face is complete and renders in the
gallery at `#peak-alpine`, bound through `ParamScope` to the namespaced
parameters the processor will carry. `ee::dsp::Tremolo` and `ee::fx::DelayModule`
are both extracted, with Peak Trem & Pan and Peak Delay running on them and
rendering sample-exact, and `SpringReverb` has its two new controls with Peak
Spring untouched, and both side modules exist with their engine crossfade
proven click-free. **`plugins/peak-alpine` builds and makes sound** - 46
parameters, all three modules in the chain, driven by `ee_alpine_host`, and the
real face is bound to them through `PeakAlpineWebEditor`. Stage 10 (the factory
preset bank) not started.

None of the eleven existing pedals is replaced. Peak Delay in particular keeps
shipping exactly as it does today; Peak Alpine holds a second instance of the
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
`peak-alpine` be thin at both ends.

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
and by default keeps every one of them *running* - only the selected engine's
output is used. On a switch the two are crossed with an equal-power fade over
40 ms.

Running the unselected engines is not belt-and-braces; it is the fix for a click
`ee_module_stress` caught on its first run. See §5.12. Parameter values are per-engine and live in the
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

- Guarded by `ee_trempan_stress` (existing) **plus** a new `tests/TremPanRegress.cpp`
  → `ee_trempan_regress`, modelled on `DelayMatch.cpp`: renders a WAV through the
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

### 3.3 `ee::dsp::SpringReverb` — two new controls

Spring's face needs **Tension** and **Low Cut**; the tank exposes only decay and
stereo today. (Reso was on this list and has been dropped - see §4.1.) Adding,
per §3 of the answers:

| Control | What it drives |
|---|---|
| `setTension01` | the dispersion chirp coefficient — already a `SpringConfig.h` constant, promoted to a runtime range around its current value |
| `setLowCut (hz)` | the tank's existing `outputLowCutCoeff`, currently pinned by config |

Both default to today's fixed values, so `SpringReverb` with nothing set is
bit-identical to the current one and **Peak Spring is untouched**. Only Peak
Alpine drives them.

- Guarded by a new `ee_spring_regress` at the defaults, and by
  `ee_reverb_stress` for tail stability across the new ranges. (`ee_spring_match`
  is the voicing renderer and stays as it is - see the `*_regress` / `*_match`
  note in CLAUDE.md.)

### 3.4 `ee::fx::ModulationModule` and `ee::fx::ReverbModule` — new

Thin: hold their engines, own an engine index, a level, a mix and an engaged
flag, and do the crossfades described above.

| Module | Engines | Notes |
|---|---|---|
| `ModulationModule` | `TapeMachine`, `Tremolo`, `Chorus`, `Phaser` | The Tape engine is now the whole of Peak Tape: Tone (a bipolar knob on its own third row) and a Mono/Stereo switch (pinned to the foot of the body) are both on the face, and the recorded tape floor is handed to it so Noise plays the recording rather than the synth-hiss fallback. Tape has **no Mix** — the transport's wow makes its wet path wander, so any partial blend against the dry combs and is heard as tremolo; it runs fully wet, the footer strip stays but the knob is dropped (`MultiEngineModule::engineUsesMix`). Tremolo's Panning stays at its default; Tremolo's rate keeps the free-running Hz mapping (tempo sync stays Peak Trem-Pan's). |
| `ReverbModule` | `FdnReverb`, `SpringReverb` | Both are mono-in/stereo-out; the module sums to mono for the send exactly as `PeakReverbProcessor` and `PeakSpringProcessor` do. |

---

## 4. The plugin — `plugins/peak-alpine`

```
plugins/peak-alpine/
  CMakeLists.txt              peak_add_plugin(PeakAlpine CODE Palp ... WEBVIEW)
  presets/                    factory bank, categorised by " - " prefix
  src/PluginProcessor.{h,cpp}
  src/PeakAlpineWebEditor.{h,cpp}
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

**Reverb (11):**
`rev.on`, `rev.engine` (Space/Spring), `rev.level`, `rev.mix`
- Space: `rev.space.decay`, `.shimmer`, `.locut`, `.reso`
- Spring: `rev.spring.decay`, `.tension`, `.locut` — three, not four. A spring
  tank has no resonance to expose: what Space calls Reso is how hard its FDN is
  allowed to ring, and a tank's equivalent is its decay, so a fourth knob would
  have been a second name for the first.

Ranges, skews, defaults and text formatters come from each source pedal
unchanged, so a Machine knob at 50 % sounds like that pedal's knob at 50 %.
Formatters go through `ee/plugin/ParamText.h` only where the source pedals
genuinely already share one — CLAUDE.md's warning about `hzToText`/`decibelsToText`
applies.

### 4.2 Processor

Holds three modules, one APVTS, one `PresetStore { apvts, "Peak Alpine",
EE_FACTORY_PRESETS }`, the two trims, the global engage crossfade, and the
playhead BPM cache (`readPlayHeadBpm` lifted from Peak Delay — audio thread
only, for the reason its comment gives about Live).

`installState` is required, not optional: the Delay module keeps the L/R time
mirror, and `PresetStore::installState` is the bracket that stops a preset load
collapsing a deliberately-uneven pair. Same implementation as
`PeakDelayProcessor::installState`.

`getTailLengthSeconds()` = delay tail + reverb tail, since they are in series.

### 4.3 Web editor

`PeakAlpineWebEditor` is `PeakDelayWebEditor` with more relays. Same
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
| `Card` | one new prop, `subtitlePlacement="below"`, for the stacked title/tagline column. Everything else about the host panel (padding, 32px logo, 19px title, zero body padding) is scoped in the face's own CSS as `.pa-card`, exactly the way `.pd-card` already scopes Peak Delay's. |
| `Slider` | check the 13px thumb against the compact one; add a size token if it differs. Geometry is otherwise already the handover's. |

### 5.2 New package — `packages/delay-face`

Exports `<DelayFace prefix="" />`: the TapScope, the Mix/Feedback/time row with
its link bracket and pills, and the three-cell stage footer — **no Card, no
header**, so each host supplies its own chrome. Carries `TimeControl`, the
delay-specific hooks (`useDelayTimesMs`, `useTimeReadoutText`, `useDelayMeter`,
`useHostBpm`) and the layout CSS currently in `peak-delay/jsui/src/index.css`.

- `plugins/peak-delay/jsui/src/App.jsx` becomes `<Card …><DelayFace/></Card>` —
  a wrapper of about fifteen lines. **Its rendered output must not change.**
- `plugins/peak-alpine/jsui` renders `<DelayFace prefix="dly." />` inside a
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

### 5.3 `plugins/peak-alpine/jsui`

Thin. `App.jsx` composes `HostPanel` chrome + three `ModulePanel`s; a
`ModulationModule.jsx` and a `ReverbModule.jsx` each own an engine table
(name → icon → knob rows) and render `JuceKnob`s from it; the Delay module is
one line. Engine tables are data, mirroring `MOD_PARAMS` / `REV_PARAMS` in the
prototype.

Global bypass dims the module row (`opacity`/`filter`), which the prototype
notes but does not model.

### 5.4 Gallery

- `apps/pedal-gallery/src/pedals.js` gains `{ slug: "peak-alpine", face:
  PeakAlpineFace, theme: "onyx" }`.
- `Components.jsx` gains a section per new component and an updated `PresetBar`
  entry, rendered in all three theme columns as the page already does.
- The gallery is where stage 2 below is verified — no JUCE, no build, hot reload.

---

## 6. Order of work

Each stage is independently verifiable and independently shippable.

| # | Stage | Verified by |
|---|---|---|
| 1 | ✅ `packages/pedal-ui`: new components, icons, `PresetBar` variant, `Card` prop, `Components.jsx` entries | gallery `#components`, three theme columns; every module-shell measurement and colour read back off the live DOM and diffed against the prototype's |
| 2 | ✅ `packages/delay-face`; Peak Delay's App.jsx reduced to a wrapper; the generic JUCE bindings to `@synthpeak/pedal-ui/juce` | fingerprint identical on **both** faces — Delay `324 / 626x481 / 733c7d76 / 42296266`, Wah `374 / 566x469 / cf04e593 / d93913e7` (Wah moved too: one `installAutoResize`) |
| 3 | ✅ `plugins/peak-alpine/jsui`: the whole face | gallery `#peak-alpine` vs the prototype at :3200; card 1046 wide and tracks 186/598/186 exact, every module-shell measurement matches, and the shim's "unknown to the backend" warnings confirm the full namespaced id set |
| 4 | ✅ `ee::dsp::Tremolo` extracted; Peak Trem-Pan delegates | `ee_trempan_regress`: 13 passes, all checksums identical either side of the change. `ee_trempan_stress`: 7776 cases, 0 flagged |
| 5 | ✅ `ee::fx::DelayModule` extracted; Peak Delay delegates | `ee_delay_regress`: 14 passes, all checksums identical either side. `ee_preset_tests`: identical. `ee_trempan_regress` unaffected |
| 6 | ✅ `SpringReverb`'s two new setters (Tension, Low Cut) | `ee_spring_regress`: Peak Spring identical, and the engine section proves the defaults inert, the controls live, and both extremes stable. `ee_reverb_stress` 1512 cases 0 flagged; `ee_dsp_tests` at its known baseline |
| 7 | `ee::fx::ModulationModule` + `ReverbModule` | `ee_dsp_tests` additions; a `ee_machine_host` driving the real processor with ragged blocks, like `ee_grain_host` |
| 8 | ✅ `plugins/peak-alpine`: processor, 46-parameter layout, CMake | `ee_alpine_host`: makes sound, all three power toggles reach the audio, bypass bit-exact, all 8 engine pairs finite and audible |
| 9 | ✅ `PeakAlpineWebEditor` + `ee::plugin::RelaySet`; face bound to real parameters | Standalone launches, editor constructs, WebView loads, `ee_alpine_host` still green. `dev` build installs AU + VST3 universal (`x86_64 arm64`); **`auval -v aufx Palp Peak` → AU VALIDATION SUCCEEDED**. Still not done: Ableton Live — see §7 |
| 10 | Factory preset bank | `ee_preset_tests` extended to Peak Alpine's bank (every parameter in every file) |

### 6.1 After the first play-through

Five things came back from playing the built plugin. Three were the face, two
were real, and one of the two was not where it looked.

**Spring's knobs did nothing.** Not the DSP: driven directly, `ee::fx::ReverbModule`
answers every Spring control, and so does the processor through its APVTS (decay
0.175, tension 0.373, low cut 0.269 peak difference against the base render). The
fault was in `@synthpeak/pedal-ui/juce`, which latched each relay state in a
`useRef` on first render. A module that swaps its engine renders the same knobs
in the same places with *different* parameter ids, and React re-points those
components rather than remounting them — so selecting Spring left Decay and Low
Cut still turning `rev.space.decay` and `rev.space.locut`. Only Tension worked,
because its key happened to change. Chorus → Phaser had the same bug for the same
reason. Fixed by resolving the relay from `id` with `useMemo` and adopting the new
parameter's value when it changes; all three hooks, so it cannot come back through
a toggle or a stepper instead.

**Tape's Flutter sounded like a chorus.** The module mixes its wet side against
its own dry, and TapeMachine reads its whole output off a transport line — 288
samples at 48 kHz — so at anything short of full wet the dry was combing against
a copy of itself 6 ms late, with the wow sweeping the comb. In the Delay module
the same tape stage sounds right because nothing there is holding an undelayed
copy of the same signal alongside it. `MultiEngineModule` now asks each engine
for its latency and pads the dry path and every shorter engine out to the
longest, and the processor adds that to what it reports the host.
`ee_module_stress` grew the check that would have caught it: Tape at rest, at Mix
40 %, must come back as the input times one constant — a comb fails it however
the level is scaled. Before the fix, 0.238 out; after, 1e-7.

**Level rested at the top of its travel.** It was 0..100 % defaulting to 100, so
both header knobs sat wound fully clockwise to say that a module was doing
nothing to the level — and, because the three modules are in series, a Level
anyone could wind to zero was a knob on one module that silenced the whole
plugin. It is now a symmetric ±12 dB trim resting at 0 dB, drawn with a new
`scaleFrom="centre"` on pedal-ui's Knob: the arc lights out from twelve o'clock
in whichever direction it has been turned, and draws nothing at all at rest.
Unity is bit-identical either way, so all eight engine checksums in
`ee_alpine_host` are unmoved.

**The three face changes.** The global bypass lost its `ACTIVE`/`BYPASSED`
capsule and became the same 22px ring each module wears (`PowerToggle` no longer
has variants at all); the Modulation module's title is `MOD`; and a module that
is switched off now dims its name, body and footer to 0.42, the fade the whole
row already used under global bypass — one language for "not running", at either
scope. Its own toggle stays lit, because that is the way back.

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
README and `Peak Alpine.dc.html` carry them, so the bundle stays the record.

1. **One panel for all three modules.** The Delay module's darker fill inside a
   lighter border is gone; every module is `#20292d` / `#161c1e` / `#0a0b0c`.
   `ModulePanel`'s `tone` survives as the wider padding a 560px module needs,
   and the `--pui-module-wide*` tokens were deleted rather than aliased.
2. **The Tape section's green band is gone**, and with it the whole
   `--pui-tape-*` group and `StageGroup`'s `tone` prop. All six footer knobs are
   now one control on one ground; the `PRE` stepper is an ordinary recessed
   switcher.
3. **Each footer section names itself with a coloured glyph** —
   `--pui-stage-tape` `#309a10`, `--pui-stage-mod` `var(--pui-accent-mod)`,
   `--pui-stage-filter` `#e08fc0` — via a new `accent` prop on `StageHeader`.

2 and 3 land on Peak Delay as well as Peak Alpine: one Delay face, no variant,
which is what the handoff asks for. That is a visible change to a shipping
pedal, so its README shots are now out of date.

`TapeIcon`'s `bandColour` became `ground`: its reels have to be filled with
whatever panel is behind them, which was the band and is now the card on Peak
Delay and the module on Peak Alpine. It reads `var(--pui-stage-ground,
var(--pui-panel))`, with `ModulePanel` setting the former - the point-of-use
fallback again, for the reason §5.5 gives.

**Open:** the three glyph hues were picked against the onyx panel they ship on.
On the light and grey themes - which only the gallery renders - the lime in
particular is low contrast. Same trade the module accents make, and worth a
look if a light Peak Alpine is ever a real face.

4. **Two footer dividers, not one.** The footer only ever drew one vertical
   line: Tape was separated from Mod by the edge of its green band, so
   `.pd-footer__section`'s rule skipped that pair. With the band gone the row
   needs both, and the rule is now a plain `+` on every adjacent pair.

   They also have to agree with the horizontals around them, which is two
   problems in one. `--pui-divider` is `#222b2e`, picked against the *card's*
   panel: inside a module's lighter `#20292d` it is both near-invisible and a
   different line from the module's own `#0a0b0c` header and footer strips.
   Fixed at the host rather than in the face — `.pui-module` re-points
   `--pui-divider` to `--pui-module-divider`, so a whole pedal face dropped
   into a module draws its internal lines in the module's line colour without
   ever learning where it is. Verified: `--pui-divider` resolves to `#0a0b0c`
   inside a `ModulePanel` and stays `#222b2e` outside one.

### 5.6a Style tuning on the built face

Four values were re-dialled against the running plugin rather than the drawing.
The README carries them; `Peak Alpine.dc.html` still shows the pre-tuning look.

1. **The soft cap is a flat `#111416`**, not the
   `--pui-soft-face-top`/`-bottom` gradient. A vertical falloff on an
   undecorated disc reads as a sphere; flat reads as a cap seen straight on.
   It is a literal in `Knob.css`, so the pair is dead in all three theme
   blocks and the cream and grey themes get the onyx cap too - only the
   gallery renders those, but it is a real difference.
2. **`SOFT_SWEEP_WIDTH_SMALL` 3 → 1.5.** Below 60px the arc is a hairline; at
   36-42px anything wider reads as a band. `--pui-knob-reach-small` is derived
   from it and drops 6.5px → 5px, which tightens Peak Delay's stage rows by
   1.5px per gap - `StageControl.css` and `StageGroup.css` both spend it.
3. **`.pui-caption` 11px → 10px.** Shared by `Knob`, `Slider`, `Toggle` and
   `Dropdown`, so this is every pedal's captions, not Peak Alpine's.
4. **The host frame is an even 14px** (was `20px 24px 22px`) and the side
   modules' parameter knobs are **36px** (was 40). The gallery's module demo
   follows the 36.

### 5.7 Notes from stage 3

- **The face is built on the real JUCE bindings, not local state.** The plan
  said local state first; that would have been a rewrite. `juce-framework-
  frontend` is vendored and degrades cleanly with no backend - which is how
  Peak Delay has always rendered in the gallery - so the face is the real one
  from the start.
- **`useJuceToggleValue` gained a `defaultValue`.** The shim answers `false`
  for an id it has never heard of, which is right for a missing parameter and
  wrong for one that merely has no backend: every power switch opened
  bypassed and the whole row rendered dimmed. It is consulted *only* for an id
  the backend has not mentioned, so in a host it is never reached. Knobs
  deliberately did **not** get the same treatment - 0 is not misleading the way
  "off" is, and duplicating every parameter default in JS is two places to
  drift.
- **`JuceFader` gained `length`.** 64px of travel fits a 528px pedal card; the
  host header has room for the handoff's 104.
- **The global bypass is lifted into `App`** and handed down. Two components
  each calling `useJuceToggleValue("on")` hold two independent copies, and with
  no relay echo outside a host the pill would have said BYPASSED over an
  undimmed row.
- **The prototype's Delay module is ~9px taller than the real face.** Its row
  measures 150 and its footer 125; Peak Delay's own face measures 138 and 128,
  and Peak Alpine renders exactly those. The handoff says to embed the real
  face rather than rebuild it, so the real face wins and the prototype is the
  approximation. The side modules differ by 2px per knob row for the same
  reason - the real `Knob`'s caption metrics against a hand-drawn one.
- **Every knob reads 0 in the gallery**, because the shim has no defaults to
  give. That is not worth faking; it resolves when the processor exists.

### 5.8 Notes from stage 4

The processor went 522 lines to 331 and its header 91 to 78; what left is now
`shared/include/ee/dsp/Tremolo.h` (+ `TremoloConfig.h` for the voicing, per the
house rule). Peak Trem & Pan keeps its parameters, its Rate map, the playhead
read and the bypass crossfade, and is otherwise an adapter.

- **The A/B harness is new and generates its own input.** `ee_delay_match`
  takes a wav; this one does not, so the comparison can be re-run from a clean
  checkout with nothing to fetch. It prints an FNV-1a checksum over the raw
  sample bits per pass, which is a stricter bar than a peak/RMS pair and a
  diffable one. It installs a **fake playhead** that plays at 128 bpm and
  relocates a third of the way through: without one, an offline render never
  reaches the phase-lock at all, which is the code an extraction is most likely
  to break.
- **One real behavioural difference, caught before it shipped.** The original
  sets `wasPlaying = isPlaying` - the transport alone - not the combined
  "synced and playing" test that guards the alignment block. Collapsing the two
  would have made switching Sync on mid-take hard-snap the LFO phase instead of
  easing onto the grid. `Tremolo::Transport` therefore carries `synced` and
  `playing` as separate fields, and says why.
- **`kSmoothingSeconds` is restated in `TremoloConfig.h`** rather than the
  engine including `ee/plugin/Bypass.h` for `kRampSeconds`. Nothing else under
  `ee/dsp` reaches up into `ee/plugin`, and an engine that did would stop being
  usable without the plugin layer. The processor sees both and `static_assert`s
  they are equal, so they cannot drift in silence.

### 5.10 Notes from stage 5

`PeakDelayProcessor` went 883 lines to 554 and its header 373 to 224. What left
is `shared/include/ee/fx/DelayModule.h` (+ its config header): the delay line,
both tape placements and the router between them, the filter pair, the phaser,
the dry/wet law, the trims and the engage crossfade. What stayed is the
parameters, the Time map, `installState` and the L/R mirror, the playhead cache,
and the scope's meter feed.

- **`ee/fx/` is a new layer**, above `ee/dsp/` and below the plugins: a
  composition of engines with an opinion about their order, which is a
  different kind of thing from an engine. `ee/dsp` stays a box of parts.
- **The module takes real units only** - seconds, hertz, linear gain. Parameter
  ranges, skews, tempo maps and anything that reads a playhead stay with
  whoever owns the parameters, which is what lets Peak Alpine bind the same
  chain to differently-named parameters without the chain knowing.
- **One `pushSettings` rather than two copies.** `prepareToPlay` and
  `processBlock` both need the whole set, and the original had them written out
  separately - a control added to one and not the other would have been wrong
  until the next block.
- **The filters' resting points are handed in**, not assumed. Each cut is
  skipped entirely at the end of its travel, and where that end *is* comes from
  the owner's parameter range - so `setFilterRestingPoints` is called once at
  prepare rather than the module guessing 20 Hz / 20 kHz.
- **The harness split happened first.** `ee_trempan_match` became
  `ee_trempan_regress`, `ee_delay_regress` was added, and their shared parts
  moved to `tests/RegressHarness.h`. The rename was verified by running the
  refactored harness against the checksums recorded before it - so the tool
  proving the refactor was itself proven not to have moved.

### 5.9 Where stage 5 started (kept - it is the shape of stages 7 and 8 too)

Nothing of stage 5 was written when this was noted. The reading is done, so this is the shape of
it:

`PeakDelayProcessor::processBlock` is one chunk loop doing, in order: the
engage ramp, the Input trim, `tapeIn`, the engage crossfade back to the
untouched input, the delay feed, `delay.process`, `tapeOut`, `runFilter`,
`runPhaser`, the dry/wet mix, the Output trim, and a final engage crossfade to
the unprocessed buffer. `prepareToPlay` seeds all of it. Everything in that
list belongs to `ee::fx::DelayModule`; what stays is the parameters, the Rate/
Time maps, `installState` and the L/R mirror, the playhead cache, and the
meter feed.

**A naming decision, made and not yet applied.** `*_match` in this repo means
"render a file so it can be A/B'd against a reference recording" - a voicing
tool (`ee_delay_match`, `ee_spring_match`, `ee_tape_render`). What stage 4 added
is a different job: a checksum battery proving a change altered nothing. Naming
it `ee_trempan_regress` blurred the two families. Before stage 5 adds a second one:

- rename `ee_trempan_regress` -> `ee_trempan_regress` (new this session, so free)
- add `ee_delay_regress` alongside the existing, untouched `ee_delay_match`
- factor their shared parts - the deterministic test signal, the FNV-1a
  checksum, the ragged block sizes and the `FakePlayHead` - into
  `tests/RegressHarness.h`, since there will be two users and a third when the
  reverb work lands

**Stage 5's gates**, once it is written: `ee_delay_regress` identical either
side, and `ee_preset_tests` still green. The preset bank is worth keeping for
exactly this - it walks every parameter in every preset and exercises
`installState` and the L/R mirror, which is the part of this refactor most
likely to break quietly.

### 5.11 Notes from stage 6

Both controls expose something the tank already had, so the work was mapping
and plumbing rather than new DSP.

- **Tension moves `kChirpCoefficient`**, the all-pass coefficient in each
  spring's dispersion chain - which is what a spring's tension physically
  changes. Mapped symmetrically about the voicing's own value so **0.5 is
  exactly `-0.62f`**, bit for bit: an untouched tank is the tank that was there
  before the control existed, by construction rather than by hoping the
  arithmetic lands. `ChirpChain::setCoefficient` sets it without rebuilding the
  stage buffers, so it is safe from the audio thread and does not disturb what
  is already ringing.
- **Low Cut moves `kOutputLowCutHz`**, the pickup's high-pass on the wet output.
  Outside every feedback path, so it thins the tail without changing how fast it
  dies. Given `FdnReverb`'s own 20-800 Hz range deliberately: Peak Alpine puts a
  Low Cut knob on both reverb engines, and a knob meaning one thing on Space and
  another on Spring is two knobs wearing one label.
- **The harness had to grow a second kind of section.** For a *move*, rendering
  the pedal is enough. For an *addition*, it proves nothing: the pedal never
  calls the new controls, so "unchanged" is true by accident. `ee_spring_regress`
  therefore drives `ee::dsp::SpringReverb` directly as well, and asserts that
  setting both controls to their documented defaults checksums identically to
  never setting them - which is the claim "Peak Spring is untouched" actually
  rests on.

### 5.12 Notes from stage 7 — the cold-engine click

The two side modules share `ee::fx::MultiEngineModule`, which owns everything
that is not an engine: the selector and its crossfade, the equal-power Mix (the
same law the delay uses, so a Mix knob means one thing across the plugin), the
output Level, and the power toggle. Writing it once is also what guarantees both
modules crossfade *identically* rather than nearly.

**`ee_module_stress` failed on its first run, and the failure was real.**
Switching *into* Chorus stepped by 0.115 - about seven times anything in the
signal - while switching out of it was clean, and every other engine passed both
ways.

The cause: Chorus is the one engine with a long dry delay line. Left unfed, its
line goes stale; select it again and its output is silence until the write
catches the read, then the signal arrives all at once. That step happens *inside*
the incoming signal, so the crossfade cannot smooth it - a crossfade can only
control how loudly a discontinuity arrives, not whether it is there. Lengthening
the fade would have hidden it, not fixed it.

The fix is that unselected engines **keep running**, their output discarded. A
switch is then a crossfade between two signals that are both already real, and an
entire class of stale-state bug goes with it. `enginesRunWarm()` is the opt-out,
and `ReverbModule` takes it: an FDN and a spring tank running at once is the
heaviest thing in the plugin, and neither clicks cold, because a reverb's output
*is* a gradual build from silence - there is no dry path in it to arrive
abruptly. That second claim is measured by the two Reverb switch cases rather
than assumed.

**CPU** is the cost, and it is now a known quantity rather than a guess: four
modulation engines always, one reverb at a time. Still worth the measurement
stage 8 was always going to want.

Two other things the suite pins down, both bit-exact rather than approximate:
Mix at 0 returns the input unchanged (the mix law is cos/sin, and cos(0) is
exactly 1), and a bypassed module returns the input whatever Mix and Level say -
the engage crossfade goes back to the *input*, not to the dry side of the mix,
so a module turned off cannot be left loud.

### 5.13 Notes from stage 8

`plugins/peak-alpine` exists: 46 parameters, the three modules in a fixed
chain, the two trims and the global bypass. The processor owns no DSP - it
reads knobs, converts them to the real units the modules take, and wraps the
lot in one crossfade. `createEditor` returns JUCE's generic editor for now; the
real face is stage 9.

Three things moved to shared headers on the way, each because a second user
appeared and each verified by the existing suites coming back identical:

- **`ee::plugin::InputMeter`** - the level follower and note-onset detector the
  TapScope feeds on. Its constants are fiddly and block-size-independent by
  design, so two copies would have drifted. Peak Delay now uses it too.
- **`ee/fx/DelayTimeMap.h`** (was `peak-delay/src/TimeMap.h`) - both plugins run
  the same delay chain, so the same knob position has to mean the same time.
- **The tremolo Rate sweep** - three numbers, now in `TremoloConfig.h`, so Peak
  Trem & Pan and the Modulation module cannot disagree about what the knob does.

**Parameter ids are dotted and module-scoped** (`mod.tape.wear`, `dly.mix`,
`rev.spring.tension`). Two reasons: three modules carry a Mix, a Level and a Low
Cut between them, and the face resolves ids through a prefix - so the Delay
leaf names are Peak Delay's own *exactly*, which is the mechanism that lets one
`DelayFace` component drive both plugins. Dots are safe in an APVTS id: it is a
value in the state tree, not a property name.

**Every range, skew and default is its source pedal's**, down to the skew
centres, so a knob at 50 % sounds like that pedal's knob at 50 %.

`ee_alpine_host` is the driver, in the spirit of `ee_grain_host`: it asserts the
things that would make the plugin broken rather than merely different - it makes
sound, each of the three power toggles reaches the audio (a module wired to the
wrong parameter would pass every other check), a bypassed plugin is the input
bit for bit, and all eight engine pairs are finite and audible. It prints a
checksum per case, so it is also the baseline for the next refactor of it.

**Still outstanding from §8:** the CPU measurement. The plugin builds and runs
offline comfortably, but nothing has been measured in a host yet.

### 5.14 Notes from stage 9

`PeakAlpineWebEditor` contains no list of parameters at all. Forty-six relays
plus forty-six attachments would have been ninety-two declarations that must be
kept in step with the layout by eye - the same silent drift CLAUDE.md warns
about for `tests/UiSnapshot.cpp`, and at that size a certainty rather than a
risk.

**`ee::plugin::RelaySet`** walks the processor's own parameters and makes the
right relay for each, deciding from the parameter's type: a bool gets a toggle
relay, a choice a combo relay, everything else a slider. Add a parameter to the
layout and the face can bind it with no editor change. The ordering it depends
on - relays before the browser, attachments after it - is documented on the
class, because the compiler will not check it.

Any future WebView pedal gets its relays from this too, and Peak Delay is now
the odd one out with its seventeen hand-written pairs. Worth folding in next
time that pedal is opened; not worth opening it for.

**The meter event keeps Peak Delay's name** (`"delayMeter"`). The face inside
the Delay module *is* Peak Delay's face and listens for exactly that - one feed
per editor rather than one per module, so a host embedding the component emits
it under the same name instead of the component learning a second one.

**The synthetic "Ms" ids arrive already scoped.** The face builds them by
appending `Ms` to the id it resolved through its `ParamScope`, so what reaches
`formatKnobValue` is `dly.ltimeMs` rather than `ltimeMs`. The editor matches on
that rather than on a bare name.

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
the `ee::ui` faces, not the WebView ones, so Peak Alpine adds nothing to it —
but stage 4's Trem-Pan work does touch a pedal that *is* in there, and the
snapshot will drift silently if its parameter mirror is not updated.

**CPU.** Peak Alpine runs a tape machine or a tremolo, a full delay chain with
two tape sections and a phaser, and a 16-line FDN — in series, in one plugin.
Worth a measurement at stage 8 before the face work is finished, because the
answer might be "prepare the unselected reverb lazily" rather than "hold both".
Still not measured in a host.

**Latency: 576 samples, 12.0 ms, and all of it is tape.** Measured against the
reported figure by `ee_alpine_host`'s ledger, which puts an impulse through the
real processor with every Mix at 0. Two identical stages, 288 each — 4.5 ms of
transport (the room the wow wobbles in) plus 1.5 ms of `TapeCharacter` — one in
the Modulation module's Tape engine and one in the Delay module's pre section.
Tremolo, Chorus, Phaser, Space and Spring contribute exactly nothing, and no
stage anywhere in the plugin has an *unreported* delay: measured equals reported
to the sample. For scale, every Arturia effect on this machine (TAPE-201,
BRIGADE, ETERNITY, JUN-6, DIMENSION-D) reports 48 samples, because their wow
modulates a delay line that *is* the effect; ours is a tape machine, whose
transport line is pure latency.

Two things follow, and one of them is a real gap:

- The Modulation module reports its 288 **whichever engine is selected**, Tape
  or not. That is the price of a constant latency contract, and the ledger
  asserts it: the alternative is calling `setLatencySamples` on every engine
  switch, which hosts handle badly.
- **Shimmer is not bit-reproducible** — `daisysp::PitchShifter` advances one
  process-wide `static` RNG per sample, so two shimmered renders never agree and
  two instances on different audio threads race on it. Found while bisecting a
  moving checksum that turned out to be nothing to do with the change under
  test. Inaudible, but it means Shimmer must stay at 0 in any regression
  battery; `ee_alpine_host` marks its one shimmered case "not a baseline". See
  CLAUDE.md's known-failures section.
- **Bypassing the Delay module drops the real latency to 288 while the plugin
  goes on reporting 576**, so a compensating host pulls everything 6 ms early.
  `ee::fx::DelayModule` crossfades back to the caller's untouched buffer rather
  than to a copy delayed to match — the same thing the global bypass does, and
  the reason a bypassed plugin lands early too. The Modulation module does *not*
  have this problem: its engage crossfade reads the aligned dry. The fix is to
  give the delay's bypass reference and the plugin's own an `AlignDelay` each;
  it would change "bypassed is bit-exact the input" to "bit-exact the input,
  delayed", which is what makes it line up. Printed by the ledger, deliberately
  not asserted, until that is decided.
The fix, if it is worth one, is to delay the bypass path's dry copy too.

**Every build tree installs into the same `~/Library/Audio/Plug-Ins`.** A
`build-fast/` iteration on peak-alpine does not install, but a `dev` build of
it will overwrite whatever `build-au/` last put there. Check `lipo -archs`
before blaming the plugin.

**Face size.** The host panel is a fixed 966px wide (a 14px frame and a 1px
border either side of a row of 180 + 8 + 560 + 8 + 180). `installAutoResize`
reports the card's real rendered size, so the editor follows — but the starting
`setSize` in the editor should be close (974 × ~640) so the host doesn't visibly
jump.

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
- **Preset bank.** Peak Alpine's factory presets are new content, not migrated.
  Six headline patches plus categorised ones, matching Peak Delay's shape.
