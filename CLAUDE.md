# Synth Peak — working notes

Thirteen JUCE audio plugins ("pedals") sharing one DSP library and one data-driven UI
framework. `README.md` is the user-facing manual (what each pedal does, how to
install it); this file is the map for working on the code.

## Build and test

**Iterating? Use `fast`.** It builds Standalone only, no LTO, and installs
nothing, and `EE_PLUGINS` cuts it down to the pedal you are actually touching:

```bash
cmake --preset fast -DEE_PLUGINS="peak-chorus"
cmake --build build-fast
```

Measured cold: **5m07s**, versus **25m39s** for the full `dev` build. Both numbers
are without ccache. Drop `-DEE_PLUGINS` to get all eleven pedals, still Standalone
only. Standalone is a real app you can launch and hear — you do not need a host.

The full build, when you want the actual VST3/AU installed into `~/Library`:

```bash
cmake --preset dev            # all eleven pedals, all three formats, installed
cmake --build build
cmake --build build --preset tests   # or just the test binaries
```

Use `--preset debug` (separate `build-debug/`) only when you need asserts; the DSP
tests run tens of seconds of audio and are unusably slow in a Debug build.
`--preset release` is the universal binary you hand to someone else.

`build/` is disposable — delete it rather than debugging it. Install `ccache`
(`brew install ccache`) if it is not already there: the top-level `CMakeLists.txt`
picks it up automatically and it matters a lot here (see *Why builds are slow*).

Verification (swap `build` for `build-fast` if that is what you configured):

```bash
scripts/dev-check.sh                # configure + build + all suites, one exit code
scripts/dev-check.sh peak-wah       # ...for a single pedal
```

It exits 0 when the only failures are the two known ones below, so its exit code
means "something you changed". The individual binaries, if you want one directly:

```bash
./build/tests/ee_dsp_tests_artefacts/Release/ee_dsp_tests          # 60 DSP tests, exits non-zero on failure
./build/tests/ee_preset_tests_artefacts/Release/ee_preset_tests    # ee::plugin::PresetStore, both banks
./build/tests/ee_tape_stress_artefacts/Release/ee_tape_stress      # tape knob sweep, non-finite hunt
./build/tests/ee_reverb_stress_artefacts/Release/ee_reverb_stress  # reverb tail stability
./build/tests/ee_trempan_stress_artefacts/Release/ee_trempan_stress
./build/tests/ee_trempan_regress_artefacts/Release/ee_trempan_regress [outDir]
./build/tests/ee_delay_regress_artefacts/Release/ee_delay_regress [outDir]
./build/tests/ee_spring_regress_artefacts/Release/ee_spring_regress [outDir]
./build/tests/ee_module_stress_artefacts/Release/ee_module_stress    # Peak Alpine's switchable modules
./build/tests/ee_alpine_host_artefacts/Release/ee_alpine_host        # drives the real Peak Alpine processor
./build/tests/ee_spring_match_artefacts/Release/ee_spring_match in.wav out.wav 3.58 26  # A/B renderer
./build/tests/ee_wah_stress_artefacts/Release/ee_wah_stress        # onset click hunt
./build/tests/ee_grain_stress_artefacts/Release/ee_grain_stress    # grain cloud into its reverb
./build/tests/ee_grain_host_artefacts/Release/ee_grain_host        # drives the real processor like a host
./build/tests/ee_au_host_artefacts/Release/ee_au_host              # runs an *installed* AU, by identifier
./build/tests/ee_ui_snapshot_artefacts/Release/ee_ui_snapshot /tmp # renders every face to PNG
./build/tests/ee_sympathy_regress_artefacts/Release/ee_sympathy_regress  # Peak Sympathy, checksum per pass
./build/tests/ee_sympathy_stress_artefacts/Release/ee_sympathy_stress    # resonator bank runaway / non-finite hunt
./build/tests/ee_sympathy_match_artefacts/Release/ee_sympathy_match in.wav out.wav  # by-ear voicing renderer
```

`auval -v aufx <CODE> Peak` runs Apple's AU validation; the four-letter codes are
in each plugin's `CMakeLists.txt` (`PLUGIN_CODE`).

`-DEE_PRESET_AUTHOR=ON` adds a third button to the face's save box, **Save to
Factory**, which writes the preset into the pedal's own `presets/` folder in the
source tree for committing. Off by default and refused by the bridge in a normal
build; never ship one.

Three pedals carry a development side panel that drives the part of their
voicing that is not on the face, and prints the header lines for whatever you
dial in: `-DEE_SHIMMER_TUNER=ON` (Peak Reverb), `-DEE_TAPE_TUNER=ON` (Peak
Delay), `-DEE_GRAIN_TUNER=ON` (Peak Grain). Never ship one.

The two WebView faces (Peak Wah, Peak Delay) read their page out of the
pedal's `jsui/dist` by default, so an installed plugin renders in a DAW with
nothing else running - **build it once after checkout**, or the editor opens on
a notice telling you to:

```bash
npm run build --prefix plugins/peak-delay/jsui
```

`-DEE_JSUI_DEV_SERVER=ON` points both faces at their Vite dev server instead
(Wah 3000, Delay 3001) for hot reload while iterating on `jsui/src`. Never
install one: `EE_INSTALL_PLUGINS` is on outside the `fast` preset, so a full
build of a dev-server tree overwrites `~/Library/Audio/Plug-Ins` with a face
that is blank whenever Vite is not running.

**`*_regress` and `*_match` are two different families**, and the distinction
matters when you add one. A `*_match` tool (`ee_delay_match`, `ee_spring_match`,
`ee_tape_render`) renders a real input file so a voicing can be A/B'd by ear
against a reference recording - it answers "is this the sound we want". A
`*_regress` tool answers "is this the *same* sound as before": it renders a
fixed battery of settings through the whole processor over ragged block sizes
and prints an FNV-1a checksum of the finished audio per pass. Run it before a
change that is meant to change nothing, keep the output, run it after, diff.
**Sample-exact is the bar** - "sounds the same" is not, because a move that
alters one sample has altered the code path.

They generate their own input and need no file, so the comparison re-runs from a
clean checkout with nothing to fetch; pass a directory to also get one wav per
pass. Their shared parts - the deterministic test signal, the checksum, the
ragged block sizes and a `FakePlayHead` that plays and relocates - are in
`tests/RegressHarness.h`. The playhead is not optional decoration: without one,
an offline render never reaches a tempo- or phase-locked engine's alignment
code at all, which is the code a refactor is most likely to break.

`ee_trempan_regress` covers every LFO shape anchor, the bias stage in and out,
the panning law, a synced pass with a transport jump, and the bypass crossfade.
`ee_delay_regress` covers the three routings, both tape placements, the filter
pair off its resting points, the in-loop drift and the on-the-repeats phaser,
free-running and synced times, uneven L/R, and both ends of the Mix law.
`ee_spring_regress` covers the Decay knob end to end plus the mono tank, and
then does something the other two do not: a second section drives
`ee::dsp::SpringReverb` **directly**, at controls Peak Spring does not expose.
That is what an *additive* change needs - the pedal battery can only show the
pedal did not move, which for a control it never calls is true by accident. The
engine section asserts the harder thing: that setting the new controls to their
documented defaults is bit-identical to never setting them at all.

The last two binaries above are diagnostic tools rather than pass/fail suites,
for the class of bug that only appears in a host. `ee_grain_host` instantiates
the real processor and drives it the way a host does - `--sr`, `--block`,
`--ragged` for varying block sizes, `--in noise|dc|burst|silence`, `--editor`,
`--reprepare` - and prints the output level per second. `ee_au_host` goes one
further and loads an *installed* component through JUCE's AU host, which is the
only way to exercise the AU wrapper itself. Address it by identifier
(`AudioUnit:Effects/aufx,Pgrn,Peak`) rather than by name: a full AU scan loads
every third-party component into the process and at least one on this machine
brings it down with a SIGBUS.

### Known failures — do not chase these

`ee_dsp_tests` does not exit clean on a healthy tree:

- **`tape at 100 % moves the level too far`** fails on every run. Baseline.
- **`chorus is silent on a silent input`** fails on roughly half of runs, with no
  rebuild in between — genuine nondeterminism in the Chorus engine. A single green
  run therefore does not prove a chorus change is safe; run the suite a few times.

Anything else is yours — and `scripts/dev-check.sh` already filters these two out
of its verdict, so keep its filter list and this section in sync.

**Shimmer is not bit-reproducible, and it is not our bug.** Any render with the
Space reverb's Shimmer above zero differs run to run, even in the same process
with the same binary and the same input — so it cannot be checksummed against
another render, and a `*_regress` battery must leave Shimmer at 0. DaisySP's
`PitchShifter`, which the shimmer is built on, draws its modulation slew
coefficients from `daisysp::myrand()` (`_deps/daisysp-src/Source/Effects/pitchshifter.h`),
and that is one function-local `static uint32_t seed` shared by every instance
in the process and advanced *per sample* from inside `Process()`. Two shimmered
renders therefore start at different points of the sequence; two plugin
instances on different audio threads also race on it. Inaudible — the depth it
scales is zero unless `SetFun` is called, which nothing here does — but it will
waste an afternoon if you are bisecting a checksum. It affects Peak Reverb and
Peak Alpine's Space engine. `ee_alpine_host` prints its one shimmered case
marked "not a baseline".

## Formatting

`.clang-format` encodes the house style (JUCE: Allman braces, 4 spaces, `foo (a)`
with a space but `foo()` without, `float* p`, `! cond`).

- **`plugins/` is clang-format-clean.** Format anything you write there:
  `xcrun clang-format -i <file>`.
- **`shared/` and `tests/` are NOT yet converted.** They are hand-formatted in the
  same style but with deliberate column-aligned tables (the DSP tuning constants in
  `shared/include/ee/dsp/*Config.h` especially). Running clang-format over them
  churns ~27% of their lines and destroys those tables. Match the surrounding style
  by hand instead; do not bulk-reformat.

Never reformat a file you are not otherwise changing.

## Layout

```
shared/include/ee/dsp/    DSP primitives and engines (mostly header-only) -
                          Chorus, Phaser, Tremolo, TapeMachine, FdnReverb,
                          SpringReverb, TapeDelay. A pedal is one of these plus
                          its parameters; nothing owns its own copy of the maths.
shared/include/ee/dsp/*Config.h   tuning constants — the knobs behind the knobs
shared/src/dsp/           FdnReverb, SpringReverb + TapeDelay implementations
shared/include/ee/ui/     the pedal UI framework (PedalSpec, PedalEditor, Knob…)
shared/src/ui/            its implementation
shared/include/ee/fx/     compositions of engines with an opinion about their
                          order - DelayModule is Peak Delay's whole chain, which
                          Peak Alpine's Delay module is a second instance of;
                          ArtifactModule is Peak Artifact's, likewise Alpine's
                          first module
shared/include/ee/plugin/ the bypass crossfade, shared parameter formatters,
                          and the preset store + its WebView bridge
plugins/peak-*/presets/   that pedal's factory presets - see below
cmake/AddPeakPlugin.cmake the juce_add_plugin boilerplate, once
plugins/peak-*/src/       one PluginProcessor.{h,cpp} each: parameters + processBlock
plugins/peak-*/CMakeLists.txt  a peak_add_plugin() call — six lines
tests/                    offline DSP tests, stress sweeps, UI snapshot renderer

packages/pedal-ui/        the WebView face component library (Knob, Card,
                          ModulePanel…) — JUCE-free, so the gallery can render it
packages/pedal-ui/src/juce.jsx   its JUCE half, a separate entry point
                          (`@synthpeak/pedal-ui/juce`): JuceKnob, JucePill, the
                          live-value hooks, ParamScope, installAutoResize
packages/delay-face/      Peak Delay's face minus its enclosure — the component
                          Peak Delay and Peak Alpine's Delay module both render
packages/artifact-face/   the same idea for Peak Artifact: the ArtifactFace
                          component Peak Artifact and Peak Alpine's first module
                          both render, bound through an `art.` prefix in Alpine
plugins/peak-*/jsui/      a WebView pedal's own page: its enclosure and whatever
                          is specific to it, and nothing else
apps/pedal-gallery/       dev-only: every face plus the component showcase
```

A face's parameter ids are resolved through the enclosing `ParamScope`, so the
same component binds to `mix` in Peak Delay and `dly.mix` inside Peak Alpine.
A pedal that wraps nothing in one is bound exactly as it was before that
existed.

The UI is data-driven: a pedal describes its face with an `ee::ui::PedalSpec` in
`createEditor()` and writes no editor code. See "Adding another effect" in
`README.md` for the `PedalSpec` fields.

### Control styles

`PedalTheme::controlStyle` picks which family of controls a face is built from,
and every control follows it - the families are never mixed on one face.

- **`analog`** (most pedals): the photographic knob cap from `knob.png`,
  a value arc around it, lit bezel buttons, dark recessed displays.
- **`analogSilver`** (Peak Reverb): `analog` with the knob's black outer collar
  swapped for a static brushed-silver bezel ring. `silver-knob.png` is
  `knob.png` minus that collar; `silver-knob-base-v1.png` is the ring that takes
  its place, so the whole control stays a normal knob size. Same `plate.png`
  centre, lights and value arc. Everything else - buttons, the filter scope,
  compact "reso" caps - is drawn exactly as under `analog` (every
  `== ControlStyle::digital` check treats it as analog). `drawSilverKnob` in
  `PedalLookAndFeel.cpp` composes the layers; `kSilverCapReachFrac` /
  `kSilverBezelOuterFrac` line the cap and ring up with `knob.png`'s geometry.
- **`digital`** (`PedalTheme::white()` on Peak Wah, `PedalTheme::moss()` on Peak
  Delay, `PedalTheme::onyx()` on Peak Grain): the flat soft-UI look. `DigitalKnob` (pale cap, dark ring, a tick
  scale instead of an arc - two sizes, picked from the cap diameter),
  `DigitalSwitch` (pill track, label either side), `DigitalToggle`
  (rounded-square bezel carrying a glyph or a caption) and `DigitalScreen` (pale
  recessed panel with a captioned grid; chrome only - the caller draws its own
  trace into the plot rect it hands back).

The style is a whole palette, not a colour: `white()`, `moss()` and `onyx()` are
the same drawing with every token shifted, so a face keeps its own hue. `onyx()`
is the dark one, and it inverts the soft-UI pair: `softShadow` goes to near-black
and `softHighlight` is a grey lift rather than a white one. That highlight is the
top of the knob-cap gradient, so a bright one there would stop a black cap
reading as black - the same trap `moss()` documents for green. A digital theme
must set `softShadow`, `softHighlight`, `recess` and `recessInk` - the analog
faces never read them, so they are easy to forget.

Nothing else has to change to move a pedal across: the same `PedalSpec` drives
both. `PedalLookAndFeel::drawRotarySlider`, `FilterScope::paint` and
`PedalEditor::Face::paint` each branch on the style; `SlideToggle`,
`DigitalSwitch`, `DigitalToggle` and `MiniToggle` all satisfy `SwitchControl`,
so the layout code places whichever one the theme asked for without knowing
which it is.

One cap can be held back in the other style with `KnobSpec::capStyle` - Peak
Delay's Tape knob keeps its photographic cap on a face of digital ones, because
the tape machine is not part of the delay. It travels to the look and feel as
the slider's `digitalCap` property.

DSP voicing constants live in `*Config.h`, not inline in the processors. When
changing a sound, change the config header — the tests and the tuning panels read
the same values.

## Presets

`ee::plugin::PresetStore` (header-only, `shared/include/ee/plugin/`) is the one
preset store, and it is meant to be adopted by every pedal. Two banks:

- **Factory** - every `*.xml` in the pedal's own `presets/` folder, compiled in
  by `peak_add_factory_presets` in `cmake/AddPeakPlugin.cmake`. There is nothing
  per-pedal to register: drop a file in the folder and the next build ships it
  (the glob is `CONFIGURE_DEPENDS`). Read-only at runtime, by design.
- **User** - one XML file per preset under
  `~/Library/Application Support/Peak/<Product>/Presets`.

**A factory preset's category is the prefix in its filename.** `Modulated -
Deep Wow.xml` is filed under a "Modulated" column in the picker and shown there
as "Deep Wow"; a file with no ` - ` in its name is a top-level entry, which is
what Peak Delay's six headline presets are. The split happens in
`PresetPicker.jsx` and nowhere else - the store, the bridge and `presetLoad` all
keep speaking in whole names. It lives in the name because that is all a factory
preset has: the glob is flat and `juce_add_binary_data` keeps only each file's
basename, so a subfolder would not survive into the binary. Categories sort
alphabetically; the uncategorised ones follow them.

A preset is `apvts.copyState()` and nothing else, so state a processor keeps
outside the tree is not in one. **Every parameter must appear in every preset
file**: `APVTS::replaceState` re-appends a parameter the tree is missing and
fills it from whatever that parameter currently holds, so a partial preset
silently inherits from whichever preset was loaded before it. `ee_preset_tests`
guards this for every preset in the bank, by loading each one twice from
opposite ends of every range and checking it lands in the same place both times.

**A whole tree arriving at once is not a knob being turned.** Peak Delay links
its two Time knobs while Sync L/R is on, off an APVTS parameter listener - and
that listener fired for each of the three parameters a preset load writes while
the button still held the *previous* state, collapsing any preset whose two
sides are deliberately apart onto one of them. `PresetStore::installState` is
the hook that fixes it: a processor that has to hold something off for the
length of an install hands its own bracket in, rather than the store calling
`replaceState` itself. `PeakDelayProcessor::installState` is the one
implementation so far, and every route a tree arrives by - `setStateInformation`
included - goes through it.

Wiring a WebView pedal up is three things and nothing else:

```cpp
// PluginProcessor.h - after apvts, and #include EE_FACTORY_PRESETS_HEADER
ee::plugin::PresetStore presets { apvts, "Peak Whatever", EE_FACTORY_PRESETS };

// the editor - wrap the options you already build
webView (ee::plugin::presetBridge (juce::WebBrowserComponent::Options {} ... ,
                                   p.presets, EE_PRESET_SOURCE_DIR))
```

```jsx
// the face
headerCenter={<JucePresetBar />}
```

`JucePresetBar` (`@synthpeak/pedal-ui`) takes no props - it talks to the five
native functions `presetBridge` registers. `PresetBar` is the same bar
prop-driven, for the gallery and for a face with no store behind it. Loading a
preset says nothing to the knobs: it replaces the APVTS tree and every relay
attachment already listens to its own parameter.

Two pedals are **not** on it yet. Peak Wah has its own older copy of the bar
(`plugins/peak-wah/jsui/src/PresetBar.jsx`, literal colours against that face's
cream panel); Peak Grain has its own `ee::grain::PresetStore`, flat, user-only,
and its own preset folder - migrating it would move presets a user may already
have saved. The nine `ee::ui` faces have a native preset bar
(`ee::ui::PresetBar` + `PresetBarSpec`) with a menu rather than this dialog;
`PresetStore` suits them as a store, but the save box here is web-only.

## Traps

**An x86_64 cmake on an Apple Silicon Mac builds plugins nothing can load.** It
is easy to end up with: an Intel Homebrew in `/usr/local` takes precedence over
`/opt/homebrew` on a default PATH, so `which cmake` finds the x86_64 one, it runs
under Rosetta, and every target defaults to x86_64. The build succeeds, the
bundles install, and a natively-running Live or Logic then refuses them with
nothing more than "this Audio Unit could not be opened".

The top-level `CMakeLists.txt` now asks `sysctl -n hw.optional.arm64` - which
reports the hardware whether or not the process is translated - and when the
host is Apple Silicon and cmake is not, forces a **universal** build. Configure
prints a line saying so. `EE_UNIVERSAL_BINARY=ON` still overrides it, and an
explicit `-DCMAKE_OSX_ARCHITECTURES=...` wins over both.

Universal rather than plain arm64, which was the first attempt and was wrong: a
DAW on Apple Silicon may be running native or may have been launched under
Rosetta - Live 12 ships a universal binary and either is a click away - and it
can only load a plugin matching the architecture it is *currently* running as.
A thin bundle is invisible to half the possibilities, and both directions fail
identically, with the host saying only "could not be opened". Building both is
the only answer that does not depend on knowing something this build cannot see.
It costs a second compile pass; installing an arm64 cmake removes the mismatch
and the cost with it:

```bash
arch -arm64 /opt/homebrew/bin/brew install cmake
```

To check what a host is actually running as, `auval` is the fastest oracle - it
loads a component exactly as a host does, and can be pointed at either
architecture:

```bash
arch -arm64  auval -v aufx Pgrn Peak
arch -x86_64 auval -v aufx Pgrn Peak
```

Two things follow. An existing build directory configured before this keeps its
old architecture until it is reconfigured, so `cmake -S . -B build` once after
pulling this. And **every build tree installs into the same
`~/Library/Audio/Plug-Ins`**, because `COPY_PLUGIN_AFTER_BUILD` is on outside the
`fast` preset - so a build from `build/` overwrites whatever `build-au/` or any
other tree last installed. If a plugin stops loading, check what is actually
installed before anything else:

```bash
lipo -archs ~/Library/Audio/Plug-Ins/Components/"Peak Grain.component"/Contents/MacOS/"Peak Grain"
```


**`tests/UiSnapshot.cpp` duplicates every pedal's parameter layout and PedalSpec.**
It builds throwaway processors so faces can be rendered without a host. If you
change a pedal's parameters, ranges, defaults or spec, you must mirror the change
there or the snapshot silently drifts from the real plugin. There is no check that
catches this.

**The snapshot renderer has no baselines.** It writes PNGs; nothing compares them.
UI changes cannot be self-verified yet — render before and after and diff the PNGs
by hand.

**Formatters that share a name do not always share a behaviour.** `hzToText`
rounds in `peak-wah` but keeps a decimal in `peak-chorus`/`peak-phase`;
`decibelsToText` prints "3.0 dB" in `peak-overdrive` and a bare signed integer in
`peak-eq`. Only the genuinely identical ones live in `ee/plugin/ParamText.h` —
the header explains which were left out and why. Do not "finish the job".

## Why builds are slow

`ee_dsp` and `ee_ui` are CMake **INTERFACE** libraries, so their sources compile
into every consumer rather than once. Measured: `PedalEditor.cpp` (1500 lines) is
compiled 14 times, `FdnReverb.cpp` 18 times, and the full JUCE module set 14 times
— 863 object files, 1.3 GB. Touching one shared UI file rebuilds and relinks 14
targets and reinstalls 18 plugin bundles into `~/Library/Audio/Plug-Ins`.

The comment in `shared/CMakeLists.txt` justifies this with "JUCE config macros are
per-target". That is true of the JUCE modules but **not** of our own code: nothing
under `shared/` references a `JucePlugin_*` macro. Converting `ee_dsp`/`ee_ui` to
STATIC is the fix and is planned.

A cold full build measured 25m39s wall / 8731s CPU (2026-08-31, no ccache).

Two other multipliers on top of that:

- LTO on all 27 plugin links (`EE_LTO`, now off outside release builds). That was
  most of the tail of a full build and buys nothing during iteration.
- `COPY_PLUGIN_AFTER_BUILD` re-signing and reinstalling 18 bundles into
  `~/Library/Audio/Plug-Ins` every build (`EE_INSTALL_PLUGINS`).

Both are now off in the `fast` preset, which is most of why it is 5x quicker.
The INTERFACE-library duplication is the part still outstanding.
