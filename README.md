# Easy Effects

Guitar pedal-style audio plugins for Ableton Live, built with JUCE. Designed to
sit after an amp sim like NAM.

Builds as **VST3**, **AU** and a **Standalone** app.

## Plugins

### Easy Reverb

A modulated feedback delay network reverb. Mono in, stereo out. Three controls:

| Knob       | Range      | What it does                                                                 |
| ---------- | ---------- | ---------------------------------------------------------------------------- |
| **Decay**  | 0.3 – 8 s  | Sets the tail length, and derives room size and predelay from it behind the scenes |
| **Mix**    | 0 – 100 %  | Equal-power blend of dry signal and wet tail                                  |
| **Mod**    | 0 – 100 %  | Pitch movement in the reflections, for width                                  |

The footswitch has **trails**: bypassing stops feeding the network but leaves the
wet path open, so the existing tail rings out instead of being cut off.

High and low frequency decay rates are fixed internally (lows ring slightly
longer, highs die faster) so the tail sits behind a guitar without getting fizzy.

## Requirements

- macOS with Xcode Command Line Tools
- CMake and Ninja: `brew install cmake ninja`

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`COPY_PLUGIN_AFTER_BUILD` installs to `~/Library/Audio/Plug-Ins/VST3` and
`~/Library/Audio/Plug-Ins/Components` automatically. Rescan in Live to pick up
changes.

For a build to hand to someone else, produce a universal binary:

```bash
cmake -B build-universal -G Ninja -DCMAKE_BUILD_TYPE=Release -DEE_UNIVERSAL_BINARY=ON
cmake --build build-universal
```

An Apple Silicon-only build will not appear in a Live instance running under
Rosetta.

## Verifying

```bash
./build/tests/ee_dsp_tests_artefacts/Release/ee_dsp_tests   # DSP: decay accuracy, stability, levels
./build/tests/ee_ui_snapshot_artefacts/Release/ee_ui_snapshot /tmp   # renders the UI to PNG
auval -v aufx Ervb Eefx                                     # Apple's AU validation
```

`pluginval` (`brew install --cask pluginval`) covers the VST3:

```bash
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --strictness-level 10 --validate-in-process \
  --validate ~/Library/Audio/Plug-Ins/VST3/"Easy Reverb.vst3"
```

## Layout

```
shared/
  include/ee/dsp/    reusable DSP primitives + the reverb engine
  include/ee/ui/     the pedal UI framework
plugins/
  easy-reverb/       processor + parameter definitions
tests/               offline DSP tests and the UI snapshot renderer
```

`ee_dsp` and `ee_ui` are separate targets so DSP can be tested without pulling in
the GUI. They are INTERFACE libraries on purpose: JUCE propagates module sources
to consumers and its config macros are per-target, so shared code has to compile
into each plugin target.

## Adding another effect

The UI is data-driven, so a new pedal needs no editor code. Describe the face:

```cpp
ee::ui::PedalSpec spec;
spec.name = "Easy Drive";
spec.tagline = "...";
spec.bypassParameterID = "on";
spec.knobs = { { "gain", "Gain" }, { "tone", "Tone" }, { "level", "Level" } };

return new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::dark());
```

Then copy `plugins/easy-reverb/CMakeLists.txt`, change `PLUGIN_CODE` and
`PRODUCT_NAME`, and add it to the top-level `CMakeLists.txt`.

## Replacing the graphics

Everything drawn comes from `PedalTheme`. Change the colours there, or drop in
artwork and the vector fallbacks stop being used:

```cpp
theme.backgroundImage = ...;      // whole pedal face
theme.knobFilmstrip = ...;        // vertical filmstrip
theme.knobFilmstripFrames = 128;
```

Layout code does not change either way.

## Licence

JUCE is dual licensed. Personal use falls under **GPLv3**, which means source has
to be available if you distribute binaries. Selling closed-source builds needs a
commercial JUCE licence. The VST3 SDK bundled with JUCE carries Steinberg's own
terms — check both before shipping anything.
