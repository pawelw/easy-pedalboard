# Easy Effects

Guitar pedal-style audio plugins for Ableton Live, built with JUCE. Designed to
sit after an amp sim like NAM.

Builds as **VST3**, **AU** and a **Standalone** app.

## Plugins

### Easy Reverb

A modulated feedback delay network reverb. Mono in, stereo out. Four controls:

| Knob         | Range        | What it does                                                                 |
| ------------ | ------------ | ---------------------------------------------------------------------------- |
| **Decay**    | 0.5 - 8 s    | Sets the tail length, and derives room size and predelay from it behind the scenes |
| **Mix**      | 0 - 100 %    | Blend of dry signal and wet tail                                              |
| **Resonance** | 0 - 100 %   | Fully open is a still, lush tail. Backing it off sets the delay lines moving, which smears the modes but is heard as movement |
| **Low Cut**  | off - 800 Hz | Highpass across the wet tail, for keeping the bottom end out of the reverb    |

Modulation is not a knob. It rides the Mix control: dry-heavy settings leave the
tail still, and as the wet takes over the extra movement smears the modes that
would otherwise ring through.

The pedal carries no on/off switch of its own — use the host's device on/off.
The `on` parameter has **trails**: bypassing stops feeding the network but leaves
the wet path open, so the existing tail rings out instead of being cut off.

High and low frequency decay rates are fixed internally (lows ring slightly
longer, highs die faster) so the tail sits behind a guitar without getting fizzy.

### Simple Delay

A tempo-synced stereo delay with independent left and right times. Six controls:

| Knob           | Range         | What it does                                                                   |
| -------------- | ------------- | ------------------------------------------------------------------------------ |
| **Left Time**  | 1/32 - 1/1.   | Note value for the left channel, always locked to the host tempo               |
| **Right Time** | 1/32 - 1/1.   | Same for the right channel                                                     |
| **Feedback**   | 0 - 100 %     | 0 % is a single slap; 100 % is a long run of repeats that still lands          |
| **Mix**        | 0 - 100 %     | Blend of dry signal and repeats                                                |
| **Mod**        | 0 - 100 %     | Tape wow and flutter plus a gentle loop rolloff — turns the repeats analog     |
| **Crush**      | 0 - 100 %     | Decimation, bit reduction and drive on top — the worn-tape / destroyed end     |

The **Sync** button between the two time knobs links them: with it on, moving
either knob moves the other, so the two channels stay on the same note value.

Every division comes in straight, dotted (`.`) and triplet (`T`) flavours.

With Mod and Crush at zero the repeat path is bypassed stage by stage, so the
plugin is a clean digital delay rather than an almost-clean one. Mod alone gives
the analog voicing; adding Crush takes it to destroyed analog. Both stages sit
inside the feedback loop, so each repeat is more degraded than the last.

Like the reverb it has no on/off switch of its own, and the `on` parameter has
trails: bypassing closes the input but lets the repeats run out.

## Requirements

- macOS with Xcode Command Line Tools
- CMake and Ninja: `brew install cmake ninja`

## Setting up on another Mac

The repo carries no dependencies — JUCE is fetched by CMake at configure time
against a pinned tag, so the first configure needs an internet connection.

```bash
xcode-select --install          # if you have never installed the CLT
brew install cmake ninja
git clone <this repo> easy-effects && cd easy-effects
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Building on the machine you play on is the path of least resistance: the binary
matches that Mac's architecture, and locally built files carry no quarantine
flag, so Gatekeeper stays out of the way.

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

## Moving a build to another Mac

```bash
./scripts/package-macos.sh      # universal build, ad-hoc signed, zipped into dist/
```

Builds without a paid Apple Developer ID can only be ad-hoc signed, never
notarised. macOS flags anything transferred by AirDrop, download or iCloud with
`com.apple.quarantine` and refuses to load it — *"Apple could not verify ... is
free of malware"*. Clear it on the receiving machine:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Easy Reverb.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Easy Reverb.component"
```

Cloning the source and building there avoids the whole problem.

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
