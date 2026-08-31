# Synth Peak — working notes

Nine JUCE audio plugins ("pedals") sharing one DSP library and one data-driven UI
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
are without ccache. Drop `-DEE_PLUGINS` to get all nine pedals, still Standalone
only. Standalone is a real app you can launch and hear — you do not need a host.

The full build, when you want the actual VST3/AU installed into `~/Library`:

```bash
cmake --preset dev            # all nine pedals, all three formats, installed
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
./build/tests/ee_dsp_tests_artefacts/Release/ee_dsp_tests          # 45 DSP tests, exits non-zero on failure
./build/tests/ee_tape_stress_artefacts/Release/ee_tape_stress      # tape knob sweep, non-finite hunt
./build/tests/ee_reverb_stress_artefacts/Release/ee_reverb_stress  # reverb tail stability
./build/tests/ee_trempan_stress_artefacts/Release/ee_trempan_stress
./build/tests/ee_ui_snapshot_artefacts/Release/ee_ui_snapshot /tmp # renders all 9 faces to PNG
```

`auval -v aufx <CODE> Peak` runs Apple's AU validation; the four-letter codes are
in each plugin's `CMakeLists.txt` (`PLUGIN_CODE`).

### Known failures — do not chase these

`ee_dsp_tests` does not exit clean on a healthy tree:

- **`tape at 100 % moves the level too far`** fails on every run. Baseline.
- **`chorus is silent on a silent input`** fails on roughly half of runs, with no
  rebuild in between — genuine nondeterminism in the Chorus engine. A single green
  run therefore does not prove a chorus change is safe; run the suite a few times.

Anything else is yours — and `scripts/dev-check.sh` already filters these two out
of its verdict, so keep its filter list and this section in sync.

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
shared/include/ee/dsp/    DSP primitives and engines (mostly header-only)
shared/include/ee/dsp/*Config.h   tuning constants — the knobs behind the knobs
shared/src/dsp/           FdnReverb + TapeDelay implementations
shared/include/ee/ui/     the pedal UI framework (PedalSpec, PedalEditor, Knob…)
shared/src/ui/            its implementation
shared/include/ee/plugin/ the bypass crossfade and shared parameter formatters
cmake/AddPeakPlugin.cmake the juce_add_plugin boilerplate, once
plugins/peak-*/src/       one PluginProcessor.{h,cpp} each: parameters + processBlock
plugins/peak-*/CMakeLists.txt  a peak_add_plugin() call — six lines
tests/                    offline DSP tests, stress sweeps, UI snapshot renderer
```

The UI is data-driven: a pedal describes its face with an `ee::ui::PedalSpec` in
`createEditor()` and writes no editor code. See "Adding another effect" in
`README.md` for the `PedalSpec` fields.

DSP voicing constants live in `*Config.h`, not inline in the processors. When
changing a sound, change the config header — the tests and the tuning panels read
the same values.

## Traps

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
