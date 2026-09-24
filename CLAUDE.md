# BitBit Audio — working notes

Fifteen JUCE audio plugins ("pedals") sharing one DSP library and one data-driven UI
framework. `README.md` is the user-facing manual (what each pedal does, how to
install it); this file is the map for working on the code.

## Build and test

**Iterating? Use `fast`.** It builds Standalone only, no LTO, and installs
nothing, and `EE_PLUGINS` cuts it down to the pedal you are actually touching:

```bash
cmake --preset fast -DEE_PLUGINS="bitbit-chorus"
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
scripts/dev-check.sh bitbit-wah       # ...for a single pedal
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
./build/tests/ee_module_stress_artefacts/Release/ee_module_stress    # BitBit Alpine's switchable modules
./build/tests/ee_bit_check_artefacts/Release/ee_bit_check [outDir] [dry.wav]  # Amp's Bit calibration and Drive's dB
./build/tests/ee_alpine_host_artefacts/Release/ee_alpine_host        # drives the real BitBit Alpine processor
./build/tests/ee_modulation_host_artefacts/Release/ee_modulation_host  # the real BitBit Modulation, checksum per engine
./build/tests/ee_reverb_host_artefacts/Release/ee_reverb_host      # the real BitBit Reverb, all three engines
./build/tests/ee_spring_match_artefacts/Release/ee_spring_match in.wav out.wav 3.58 26  # A/B renderer
./build/tests/ee_wah_stress_artefacts/Release/ee_wah_stress        # onset click hunt
./build/tests/ee_grain_stress_artefacts/Release/ee_grain_stress    # grain cloud into its reverb
./build/tests/ee_grain_host_artefacts/Release/ee_grain_host        # drives the real processor like a host
./build/tests/ee_au_host_artefacts/Release/ee_au_host              # runs an *installed* AU, by identifier
./build/tests/ee_plugin_render_artefacts/Release/ee_plugin_render 'AudioUnit:Effects/aufx,Ni$Q,-NI-' --set Decay=2 --in dry.wav --out ref.wav  # renders through any installed AU at knob values set by their text
./build/tests/ee_space_fit_artefacts/Release/ee_space_fit --decay 2 --damp 0.25 --out ir.wav [field=value ...]  # SpaceReverb (the Studio engine) alone, any voicing field overridden
./build/tests/ee_reverb_match_artefacts/Release/ee_reverb_match dry.wav out.wav 2 20 [damp] [predelay] [locut] [hicut]  # the real BitBit Reverb processor, Studio engine
./build/tests/ee_ui_snapshot_artefacts/Release/ee_ui_snapshot /tmp # renders every face to PNG
./build/tests/ee_sympathy_regress_artefacts/Release/ee_sympathy_regress  # BitBit Sympathy, checksum per pass
./build/tests/ee_sympathy_stress_artefacts/Release/ee_sympathy_stress    # resonator bank runaway / non-finite hunt
./build/tests/ee_sympathy_match_artefacts/Release/ee_sympathy_match in.wav out.wav  # by-ear voicing renderer
./build/tests/ee_param_golden_BitBitDelay_artefacts/Release/ee_param_golden_BitBitDelay  # the frozen parameter contract, one per product
./build/tests/ee_preset_fuzz_BitBitDelay_artefacts/Release/ee_preset_fuzz_BitBitDelay [--seed N] [--iterations N]  # hostile state/preset input, one per product
./build/tests/ee_soak_BitBitDelay_artefacts/Release/ee_soak_BitBitDelay [--hours 8] [--instances 32] [--editor-cycles]  # the overnight harness (release-plan.md 1.5), one per product - not in dev-check.sh
./build/tests/ee_latency_audit_BitBitDelay_artefacts/Release/ee_latency_audit_BitBitDelay [--rate HZ] [--verbose]  # does the dry path land on getLatencySamples(), every rate, every discrete state, host bypass (G5.3), one per product
```

**`tests/golden/*.txt` is the frozen parameter contract**, one file per
*shipping* product (`EE_RELEASE_PLUGINS` in the top-level `CMakeLists.txt` - the
six in `docs/release-plan.md`, D2; the other nine are not packaged, so nothing
outside this tree is keyed on their ids). `ee_param_golden_<Target>`
instantiates the real processor and writes down every parameter id, name,
range, default, version hint and **choice list in order**, plus the plugin code,
manufacturer code, bundle id and VST3 categories. A choice list is an engine
enum, so the "append LAST" rule is enforced here rather than only written down.

It fails when any of that moves - which is the point, because all of it is what
a saved session and every preset file is keyed on, and renaming a parameter is
otherwise completely silent. If the change is deliberate:

```bash
for t in BitBitAlpine BitBitGrain BitBitArtifact BitBitModulation BitBitDelay BitBitReverb; do
    "build-fast/tests/ee_param_golden_${t}_artefacts/Release/ee_param_golden_$t" --update
done
```

...and say why in the commit. Adding a parameter is fine; renaming or reordering
one is a decision, and after the first sale it is not available at all.

One binary per product rather than one that knows about them all: every pedal's
`PluginProcessor.cpp` defines `createPluginFilter()`, and that is the same
object file that defines the processor, so two of them in one link is a
duplicate symbol.

**Matching a third-party reference: host it, don't deconvolve it.**
`ee_plugin_render` loads an installed AU the way `ee_au_host` does, sets its
parameters by their *displayed* value (`--set Decay=2.5` bisects until the
plugin prints 2.5; `n:0.4` sets the normalised value) and renders a file or a
unit impulse. Every `--set` is applied twice, because some plugins move one
knob when another is set (Raum resets Mix when Decay changes). BitBit Reverb's
Studio engine (`ee::dsp::SpaceReverb`) was fitted to NI Raum this way:
impulse responses over a Decay x Damp grid, octave-band T20 and per-octave
energy, then `ee_space_fit` renders of the engine measured with the same
analysis. `SpaceConfig.h` records what was measured and how. Match per octave,
not broadband: a white impulse's energy is mostly its top octave, and a
broadband level match once hid a tail 1-2 dB thin everywhere a mix lives.
Match the build-up too, not just decay, tone and level: a network can hit all
three and still bounce - check echo density over the first 150 ms and the
tail's autocorrelation per octave (a peak at one line's length is a repeat you
can hear). Deconvolving a bounced drum loop was tried first and is useless -
the loop does not pin the response down.

`auval -v aufx <CODE> BtBt` runs Apple's AU validation; the four-letter codes are
in each plugin's `CMakeLists.txt` (`PLUGIN_CODE`).

`-DEE_PRESET_AUTHOR=ON` adds a third button to the face's save box, **Save to
Factory**, which writes the preset into the pedal's own `presets/` folder in the
source tree for committing. Off by default and refused by the bridge in a normal
build; never ship one.

Two pedals carry a development side panel that drives the part of their
voicing that is not on the face, and prints the header lines for whatever you
dial in: `-DEE_TAPE_TUNER=ON` (BitBit Delay), `-DEE_GRAIN_TUNER=ON` (BitBit Grain).
Never ship one. (BitBit Reverb's shimmer panel went when it became a WebView
module - `ShimmerTuning.h` still holds that voicing.)

The WebView faces (BitBit Wah, BitBit Delay, BitBit Alpine, BitBit Artifact, BitBit
Modulation, BitBit Reverb) read their page out of the pedal's `jsui/dist` by
default, so an installed plugin renders in a DAW with
nothing else running - **build it once after checkout**, or the editor opens on
a notice telling you to:

```bash
npm run build --prefix plugins/bitbit-delay/jsui
```

`-DEE_JSUI_DEV_SERVER=ON` points the faces at their Vite dev servers instead
(Wah 3000, Delay 3001, Alpine 3002, Artifact 3003, Modulation 3004, Reverb 3005)
for hot reload while iterating on `jsui/src`. Never
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
`ee::dsp::SpringReverb` **directly**, at controls BitBit Spring does not expose.
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
(`AudioUnit:Effects/aufx,Bgrn,BtBt`) rather than by name: a full AU scan loads
every third-party component into the process and at least one on this machine
brings it down with a SIGBUS.

### Known failures

None. `ee_dsp_tests` exits clean on a healthy tree, and `scripts/dev-check.sh`
no longer filters anything - a FAIL is yours. Two that used to be waived here
were closed under G1.4 of `docs/release-plan.md`, and both were real:

- **`chorus is silent on a silent input`** (was flaky, ~50 % of runs) was an
  out-of-bounds read in `ModDelayLine::read`. Adding the buffer size to a tiny
  negative read position rounds to exactly `size` in float, which is one sample
  past the end, so the read returned whatever the heap held next door - a
  different value each run. Found by ASan (see *Sanitizers*), which reproduces it
  in the Chorus and in `FdnReverb`; `Rust.h` and `OctaveShifter.h` carried the same
  wrap and are fixed too. `testModDelayLineWrapBoundary` is the regression.
- **`tape at 100 % moves the level too far`** was a voicing change, not a bad
  test: once `maxDrive` was tuned up to 5.53 the makeup gain, which only
  compensates the curve's slope at the origin, left a played level ~4 dB down.
  `TapeCharacter` now calibrates the makeup at `TapeTuning::levelReference` (a
  gaussian at -20 dBFS rms) instead. The test measures a band-limited source,
  because the stage rolls the top off on purpose and broadband noise reads that
  as a level change.

### Input that somebody else wrote

A state arrives by two doors - `setStateInformation` (a host reopening a project)
and `PresetStore::load` (a user preset file the user may have edited or half
copied) - and both go through `ee/plugin/SafeParse.h` and `sanitisedState`
(`StateVersion.h`) before anything is installed. `ee_preset_fuzz_<Target>` (one
per sold product, same duplicate-`createPluginFilter()` reason as the golden
files) feeds both doors mutated valid states, truncations at every byte, NaN /
overflow / text in every value, deep nesting and multi-megabyte files, and holds
them to: no throw, no crash, every parameter finite and in range, finite audio,
and a refused preset leaves the state untouched. Deterministic per `--seed`; if
it dies, the input it died on is in `$TMPDIR/ee_preset_fuzz_<Target>/`. It
writes one `zz-fuzz-preset.xml` into the product's real user-preset folder and
removes it again. Run it under `--preset asan` for anything touching a loader.

It found two things on its first run, both fixed 2026-09-21:

- **JUCE's XML and JSON parsers recurse per level of nesting**, so a 70 kB file of
  opening tags overflows the stack - the host's stack. Refused now by a linear
  scan before the parser (`xmlNestingWithin`, `jsonNestingWithin`, depth 64, and
  a 32 MB size cap). Use `parseXmlText` / `parseXmlFile` / `xmlFromBinary` /
  `parseJson`, never the JUCE calls, on anything that is not our own output.
- **`value="nan"` puts a NaN in the parameter.** `String` parses it, APVTS
  installs it, and the DSP gets a NaN cutoff. `sanitisedState` puts a non-finite
  value back to that parameter's default; out-of-range finite values are clamped
  by APVTS already.

A new pedal's `setStateInformation` should be
`installState (ee::plugin::sanitisedState (ValueTree::fromXml (*xml), apvts))` on
`ee::plugin::xmlFromBinary`, and get its own fuzz binary for free from the loop
in `tests/CMakeLists.txt`.

### Sanitizers

The offline tools build under ASan + UBSan or TSan, in their own trees:

```bash
cmake --preset asan -DEE_PLUGINS="bitbit-alpine;bitbit-grain;bitbit-artifact;bitbit-modulation;bitbit-delay;bitbit-reverb"
cmake --build build-asan --target ee_dsp_tests ee_alpine_host ee_grain_host   # ...any test tool
./build-asan/tests/ee_dsp_tests_artefacts/Release/ee_dsp_tests
cmake --preset tsan -DEE_PLUGINS=bitbit-chorus && cmake --build build-tsan --target ee_dsp_tests
```

Build only the test targets there - the plugin targets are not meant to run
instrumented, and a full ASan build of all six is ~12 minutes. `EE_SANITIZE`
applies to JUCE and DaisySP too, on purpose. Add
`-DFETCHCONTENT_SOURCE_DIR_JUCE=$PWD/build/_deps/juce-src` (likewise `_DAISYSP`,
`_CHOWDSP_WDF`) to reuse an existing checkout instead of re-cloning. ASan runs
the suite in ~30 s, TSan in ~5 min. A sanitizer cannot see what it does not
instrument: the *uninitialised* members in DaisySP's `PitchShifter` sat past the
range ASan's allocation fill covers (see below), so a clean run is evidence, not proof.

**Shimmer is bit-reproducible** (`testShimmerReproducible`), and it used not to
be, for two reasons in DaisySP's `PitchShifter` - which is why BitBit's shimmer
now runs `ee::dsp::ShimmerPitchShifter`, a copy with those two fixed. Its
modulation drew from `daisysp::myrand()`, one function-local `static uint32_t`
shared by every instance and advanced from `Process()`, so a render depended on
what had run before it and two instances on different audio threads raced on it.
And its constructor is user-provided and empty, so several members (`mod_a_amt_`,
`slewed_mod_`, `mod_coeff_`, `prev_phs_a_`...) started as heap garbage. Each
instance now has its own generator, seeded in `Init()`, and every member is set
there. The flutter (`ShimmerTuning::flutter`, 0.25) is **not** zero, so this was
audible modulation, not inaudible as an earlier version of this file said. The
voicing is unchanged; only the random sequence differs from the old shared one.
It affects BitBit Reverb and BitBit Alpine's Shimmer engine. A `*_regress` battery
can now leave Shimmer above zero (the module's own Shimmer engine has run it at
its maximum, unconditionally, since 2026-09-23 - see `ee::fx::ReverbModule::
setShimmer` - so this now matters on every render of it, not just some).

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
                          Chorus, Phaser, Tremolo, TapeMachine, FdnReverb
                          (the Shimmer reverb, and BitBit Grain's), SpaceReverb
                          (the Studio reverb), SpringReverb, TapeDelay. A pedal is one of these plus
                          its parameters; nothing owns its own copy of the maths.
shared/include/ee/dsp/*Config.h   tuning constants — the knobs behind the knobs
shared/src/dsp/           FdnReverb, SpaceReverb, SpringReverb + TapeDelay implementations
shared/include/ee/ui/     the pedal UI framework (PedalSpec, PedalEditor, Knob…)
shared/src/ui/            its implementation
shared/include/ee/fx/     compositions of engines with an opinion about their
                          order - DelayModule is BitBit Delay's whole chain, which
                          BitBit Alpine's Delay module is a second instance of;
                          ArtifactModule is BitBit Artifact's, likewise Alpine's
                          first module; ModulationModule and ReverbModule are
                          BitBit Modulation's and BitBit Reverb's, and
                          ModulationControls.h holds the Mod knob maps and host
                          sync both plugins read
shared/include/ee/plugin/ the bypass crossfade, shared parameter formatters,
                          and the preset store + its WebView bridge
plugins/bitbit-*/presets/   that pedal's factory presets - see below
cmake/AddBitBitPlugin.cmake the juce_add_plugin boilerplate, once
plugins/bitbit-*/src/       one PluginProcessor.{h,cpp} each: parameters + processBlock
plugins/bitbit-*/CMakeLists.txt  a bitbit_add_plugin() call — six lines
tests/                    offline DSP tests, stress sweeps, UI snapshot renderer

packages/pedal-ui/        the WebView face component library (Knob, Card,
                          ModulePanel…) — JUCE-free, so the gallery can render it
packages/pedal-ui/src/juce.jsx   its JUCE half, a separate entry point
                          (`@synthpeak/pedal-ui/juce`): JuceKnob, JucePill, the
                          live-value hooks, ParamScope, installAutoResize
packages/delay-face/      BitBit Delay's face minus its enclosure — the component
                          BitBit Delay and BitBit Alpine's Delay module both render
packages/artifact-face/   the same idea for BitBit Artifact: the ArtifactFace
                          component BitBit Artifact and BitBit Alpine's first module
                          both render, bound through an `art.` prefix in Alpine
packages/module-face/     and again for Alpine's two narrow modules:
                          ModulationFace and ReverbFace (one SideModule), which
                          BitBit Modulation and BitBit Reverb render standalone and
                          Alpine binds through `mod.` / `rev.`
plugins/bitbit-*/jsui/      a WebView pedal's own page: its enclosure and whatever
                          is specific to it, and nothing else
apps/pedal-gallery/       dev-only: every face plus the component showcase
```

A face's parameter ids are resolved through the enclosing `ParamScope`, so the
same component binds to `mix` in BitBit Delay and `dly.mix` inside BitBit Alpine.
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
- **`analogSilver`** (BitBit Tape): `analog` with the knob's black outer collar
  swapped for a static brushed-silver bezel ring. `silver-knob.png` is
  `knob.png` minus that collar; `silver-knob-base-v1.png` is the ring that takes
  its place, so the whole control stays a normal knob size. Same `plate.png`
  centre, lights and value arc. Everything else - buttons, the filter scope,
  compact "reso" caps - is drawn exactly as under `analog` (every
  `== ControlStyle::digital` check treats it as analog). `drawSilverKnob` in
  `PedalLookAndFeel.cpp` composes the layers; `kSilverCapReachFrac` /
  `kSilverBezelOuterFrac` line the cap and ring up with `knob.png`'s geometry.
- **`digital`** (`PedalTheme::white()` on BitBit Wah, `PedalTheme::moss()` on BitBit
  Delay, `PedalTheme::onyx()` on BitBit Grain): the flat soft-UI look. `DigitalKnob` (pale cap, dark ring, a tick
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

One cap can be held back in the other style with `KnobSpec::capStyle` - BitBit
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
  by `bitbit_add_factory_presets` in `cmake/AddBitBitPlugin.cmake`. There is nothing
  per-pedal to register: drop a file in the folder and the next build ships it
  (the glob is `CONFIGURE_DEPENDS`). Read-only at runtime, by design.
- **User** - one XML file per preset under
  `~/Library/BitBit/<Product>/Presets` (JUCE's `userApplicationDataDirectory` is `~/Library` on macOS, not Application Support).

**A factory preset's category is the prefix in its filename.** `Modulated -
Deep Wow.xml` is filed under a "Modulated" column in the picker and shown there
as "Deep Wow"; a file with no ` - ` in its name is a top-level entry, which is
what BitBit Delay's six headline presets are. The split happens in
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

**Every write of a pedal's state is stamped with a format version.**
`ee::plugin::copyVersionedState (apvts)` (`StateVersion.h`) is `copyState()` plus a
`stateVersion` property on the root - use it in `getStateInformation` and anywhere
else a tree is serialised, never a bare `apvts.copyState()`. Factory preset XML
carries `stateVersion="1"` on its `<PARAMETERS>` root, and `ee_preset_tests`
checks all of them. Nothing reads the stamp yet: it exists so a later release can
tell what 1.0 wrote from what it writes itself. Bump `kStateVersion` only for a
change that needs a migration on load, and do the migration in `installState`.

**A whole tree arriving at once is not a knob being turned.** BitBit Delay links
its two Time knobs while Sync L/R is on, off an APVTS parameter listener - and
that listener fired for each of the three parameters a preset load writes while
the button still held the *previous* state, collapsing any preset whose two
sides are deliberately apart onto one of them. `PresetStore::installState` is
the hook that fixes it: a processor that has to hold something off for the
length of an install hands its own bracket in, rather than the store calling
`replaceState` itself. `BitBitDelayProcessor::installState` is the one
implementation so far, and every route a tree arrives by - `setStateInformation`
included - goes through it.

Wiring a WebView pedal up is three things and nothing else:

```cpp
// PluginProcessor.h - after apvts, and #include EE_FACTORY_PRESETS_HEADER
ee::plugin::PresetStore presets { apvts, "BitBit Whatever", EE_FACTORY_PRESETS };

// the editor - wrap the options you already build
webView (ee::plugin::presetBridge (juce::WebBrowserComponent::Options {} ... ,
                                   p.presets, EE_PRESET_SOURCE_DIR))
```

```jsx
// the face
headerCenter={<JucePresetBar />}
```

`JucePresetBar` (`@synthpeak/pedal-ui`) takes no props - it talks to the six
native functions `presetBridge` registers (the sixth, `presetRandomize`, is
the dice beside Save - `PresetStore::randomize`). `PresetBar` is the same bar
prop-driven, for the gallery and for a face with no store behind it. Loading a
preset says nothing to the knobs: it replaces the APVTS tree and every relay
attachment already listens to its own parameter.

Two pedals are **not** on it yet. BitBit Wah has its own older copy of the bar
(`plugins/bitbit-wah/jsui/src/PresetBar.jsx`, literal colours against that face's
cream panel); BitBit Grain has its own `ee::grain::PresetStore`, flat, user-only,
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
arch -arm64  auval -v aufx Bgrn BtBt
arch -x86_64 auval -v aufx Bgrn BtBt
```

Two things follow. An existing build directory configured before this keeps its
old architecture until it is reconfigured, so `cmake -S . -B build` once after
pulling this. And **every build tree installs into the same
`~/Library/Audio/Plug-Ins`**, because `COPY_PLUGIN_AFTER_BUILD` is on outside the
`fast` preset - so a build from `build/` overwrites whatever `build-au/` or any
other tree last installed. If a plugin stops loading, check what is actually
installed before anything else:

```bash
lipo -archs ~/Library/Audio/Plug-Ins/Components/"BitBit Grain.component"/Contents/MacOS/"BitBit Grain"
```


**A pedal that reports latency must override `processBlockBypassed`.** JUCE's
default is a pass-through with no delay, so a host's own bypass button pulls the
signal ahead of the figure it just compensated for (Debug builds assert on it).
Delay, Modulation and Alpine do it by running the ordinary block with power forced
off (`hostBypassed`), which keeps the aligned crossfade and the tails. The same
rule holds inside a plugin: whatever a bypass falls back to must be held back by
the reported latency (`ee::fx::AlignDelay`). `ee_latency_audit_<Target>` finds
both, and finds them for a new product without being told about it.

**`tests/UiSnapshot.cpp` duplicates every pedal's parameter layout and PedalSpec.**
It builds throwaway processors so faces can be rendered without a host. If you
change a pedal's parameters, ranges, defaults or spec, you must mirror the change
there or the snapshot silently drifts from the real plugin. There is no check that
catches this.

**The snapshot renderer has no baselines.** It writes PNGs; nothing compares them.
UI changes cannot be self-verified yet — render before and after and diff the PNGs
by hand.

**Formatters that share a name do not always share a behaviour.** `hzToText`
rounds in `bitbit-wah` but keeps a decimal in `bitbit-chorus`/`bitbit-phase`;
`decibelsToText` prints "3.0 dB" in `bitbit-overdrive` and a bare signed integer in
`bitbit-eq`. Only the genuinely identical ones live in `ee/plugin/ParamText.h` —
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
