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

### Easy Delay

A tempo-synced stereo delay with independent left and right times. Six controls:

| Knob           | Range         | What it does                                                                   |
| -------------- | ------------- | ------------------------------------------------------------------------------ |
| **Left Time**  | 1/32 - 1/1.   | Note value for the left channel, always locked to the host tempo               |
| **Right Time** | 1/32 - 1/1.   | Same for the right channel                                                     |
| **Feedback**   | 0 - 100 %     | 0 % is a single slap; 100 % is a long run of repeats that still lands          |
| **Mix**        | 0 - 100 %     | Blend of dry signal and repeats                                                |
| **Mod**        | 0 - 100 %     | Slow, wide wow inside the feedback loop — the warm, moving end                  |
| **Tape**       | 0 - 100 %     | A tape machine in front of the delay: flutter, drive, head loss and grit        |

The **Sync** button between the two time knobs links them: with it on, moving
either knob moves the other, so the two channels stay on the same note value.

Every division comes in straight, dotted (`.`) and triplet (`T`) flavours.

**Tape** is not part of the delay. It sits in front of it, the way a separate
pedal would sit earlier in a chain, so it colours the dry signal whether or not
any delay is being heard. Turn **Mix** all the way down and the repeats go
silent but the tape keeps working on the dry signal. Its knob is the pale one,
because it is not really part of the same effect as the rest of the face.

Mod is the one that lives inside the feedback loop and compounds with every
pass.

The voicing is measured against a reference machine rather than invented. It is
not a bit crusher — decimation and quantisation read as digital however they are
dressed up. What the reference actually does at full tilt is leave the midband
alone within about 0.2 dB from 50 Hz to 3 kHz, hold the level, take a quarter of
a dB off the crest, wobble the whole signal by about 0.17 ms at 3-5 Hz, and lay
a broad band of noise over the top that follows the signal instead of sitting
under it. That last part is the difference between grit and hiss, and it is why
the stage is silent on silence and quietens as the signal does.

Two harnesses keep it honest: `tests/ee_tape_match` renders a file through the
tape stage on its own, and `tests/ee_delay_match` renders one through the whole
plugin, so the chain can be checked end to end. With Mix at 0 % and Tape at
100 %, the plugin's dry output carries grit 36.6 dB below the signal against the
reference's 36.4 dB, where the untouched input sits at 55.8 dB.

At 0 % the stage is bit exact, and it reports a constant 1.5 ms of latency so
the timing never shifts as the knob moves.

Like the reverb it has no on/off switch of its own, and the `on` parameter has
trails: bypassing fades the tape off the dry path and closes the delay input,
letting the repeats run out.

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
