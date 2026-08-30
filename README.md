# Easy Effects

Guitar pedal-style audio plugins for Ableton Live, built with JUCE. Designed to
sit after an amp sim like NAM.

Builds as **VST3**, **AU** and a **Standalone** app.

## Plugins

### Easy Reverb

A modulated feedback delay network reverb. Mono in, stereo out. Four knobs, plus
a small **Resonance** cap in the middle of them:

| Knob         | Range        | What it does                                                                 |
| ------------ | ------------ | ---------------------------------------------------------------------------- |
| **Decay**    | 0.5 - 8 s    | Sets the tail length, and derives room size and predelay from it behind the scenes |
| **Mix**      | 0 - 100 %    | Blend of dry signal and wet tail                                              |
| **Shimmer**  | 0 - 100 %    | Feeds an octave-up copy of the tail back into the reverb. 0 % is off; up high each pass stacks another octave into a rising pad |
| **Low Cut**  | off - 800 Hz | Highpass across the wet tail, for keeping the bottom end out of the reverb    |
| **Resonance** (centre) | 0 - 100 % | Fully open is a still, lush tail. Backing it off sets the delay lines moving, which smears the modes but is heard as movement |

Resonance sits on a small cap between the four main knobs, marked `RESO` with no
value printed — it is a voicing trim, not a headline control.

Modulation is not a knob. It rides the Mix control: dry-heavy settings leave the
tail still, and as the wet takes over the extra movement smears the modes that
would otherwise ring through.

**Shimmer** is a stereo pair of time-domain pitch shifters (DaisySP's), fed a
tap of the wet output through a predelay that grows with the decay knob, so the
octave blooms behind the note rather than piling onto it. The two shifters read
the predelay a Haas offset apart and their internal random modulation
decorrelates them, so the octave comes back wide rather than as a mono point.
Each side is band-limited — a highpass keeps the stack from growing a sub rumble
as the shifter's tracking drifts flat, a lowpass keeps stacked octaves from
piling into hiss — with an optional high shelf for sparkle, then soft-clipped
through a `tanh` so no setting can let it run away. It re-enters the network as a
correlated centre plus an L/R difference scaled by `width`, so it spreads without
gutting a mono sum. The knob is the feedback gain, tapered and capped below
unity. At 0 % neither shifter runs and the reverb is exactly what it was.

The full shimmer voicing lives in `shared/include/ee/dsp/ShimmerTuning.h`; the
defaults there are a tuned setting. Configure with `-DEE_SHIMMER_TUNER=ON` (and
flip `showTuner` in `PluginProcessor.cpp`) to open Easy Reverb with a side panel
of live sliders for every value plus a copy-paste-ready readout of the struct —
a development build only, the way `-DEE_TAPE_TUNER=ON` works for Easy Delay.

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
silent but the tape keeps working on the dry signal. Its knob is the green one,
because it is not really part of the same effect as the rest of the face.

The face uses a mustard `gold()` theme (`#c09d28`), with the green (`#375916`)
Tape cap and the amber `Sync` toggle set apart from the black caps.

Mod is the one that lives inside the feedback loop and compounds with every
pass.

The voicing is measured against a reference machine rather than invented. It is
not a bit crusher — decimation and quantisation read as digital however they are
dressed up. What the reference actually does at full tilt is leave the midband
alone within about 0.2 dB from 50 Hz to 3 kHz, hold the level, and take a
quarter of a dB off the crest.

Most of what you hear is the wobble, not the noise. Wow and flutter on a real
machine is random rather than a pair of sine oscillators: its sidebands spread
evenly from about 1.5 Hz out past 70 Hz, falling roughly 3 dB per octave, which
is 1/f. Three one-pole filters on white noise get close enough for almost no
arithmetic. That movement alone accounts for about nine tenths of the energy the
stage adds above 6 kHz; the remaining tenth is grit, which rides the programme
instead of sitting under it, so it is silent on silence and quietens as the
signal does.

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

### Easy EQ

A seven-band graphic EQ modelled on the Boss GE-7, driven by vertical faders
instead of knobs. Mono or stereo, in and out. Eight faders:

| Fader     | Range          | What it does                                            |
| --------- | -------------- | ------------------------------------------------------ |
| **Level**       | -15 - +15 dB | Output make-up gain after the bands                     |
| **100 - 6.4k**  | -15 - +15 dB | Cut or boost at 100, 200, 400, 800, 1.6k, 3.2k, 6.4k Hz |

Each fader is a graph node — a stem to the baseline with a round handle at the
value, joined to its neighbours by a live green response curve (corners eased
off) over a faint grid. Any value that is not 0 is printed in the same green.
The **Level** node is drawn light-grey with a black outline and sits outside
the curve, since it is not part of the frequency response. The handle grabs
where you press and drags from there (it never jumps on touch); it detents
onto 0 dB when a drag lands near it, and double-click snaps back to 0.
**RESET**, top-left above the faders, flattens every band. The bands are broad, overlapping bells (`Q ≈ 1.4`) built
from JUCE's `juce::dsp::IIR` peak filters, so the curve follows the fader
positions rather than showing seven isolated spikes.

Two small knobs top-right trim the ends of the spectrum: **low cut** (a
high-pass, `20 Hz – 1.2 kHz`, default off) and **high cut** (a low-pass,
`1.2 kHz – 20 kHz`, default off). Each reads `∞ / Hz / kHz`. The high cut is
drawn inverted — a full white ring at rest, with the value arc growing back
from the top as it is turned down. As a knob engages, a faint shaded band grows
in from that side of the grid, its inner edge on a shared log-frequency axis;
because the two ranges meet at 1.2 kHz, pushing both knobs there lands the
shading on the same point.

The green curve is a live response readout: between the band faders it follows
their positions, and where a cut is engaged it bends down at roughly the
filter's slope and runs off the bottom of the grid, reshaping as any fader or
cut knob moves.

Like the other pedals it carries no on/off switch of its own — use the host's
device on/off. The `on` parameter crossfades to the clean dry signal so
toggling it never clicks.

The face reuses Easy Delay's `silver()` theme, and is the same width and height
as Easy Reverb, so the pedals line up on a rack.

### Easy Trem & Pan

One LFO that either chops the level (tremolo) or sweeps the stereo position
(auto-pan), modelled on Ableton's Auto Pan. Mono or stereo in, stereo out.
Three knobs shape the LFO:

| Knob       | Range            | What it does                                                     |
| ---------- | ---------------- | -------------------------------------------------------------- |
| **Amount** | 0 - 100 %        | Depth of the effect - chop depth in tremolo, pan width in panning |
| **Rate**   | -                | LFO speed. Reads a note value when synced, a period in ms when free |
| **Shape**  | 0 - 100 %        | Sweeps the LFO waveform (see below). Default 50 %              |

**Shape** morphs the LFO through five anchors, crossfading between them: `0 %`
exponential decay (a plucked feel), `25 %` falling ramp, `50 %` triangle, `75 %`
soft rounded square, `100 %` a hard rounded-corner rectangle. Even the square end
keeps its corners eased, so the chop never steps between two levels in one sample
and never clicks. The audio path and the on-screen preview both read
`ee::dsp::lfoValue`, so the drawing is a picture of the wave being heard.

Two switches sit above the knobs:

- A **Tremolo / Panning** slider, top-left: a light knob on a dark track (about
  two circles wide), left for tremolo (default), right for panning.
- A **Sync** button centred above the **Rate** knob - Easy Delay's `MiniToggle`,
  with the same amber lit colour. Lit locks **Rate** to the host tempo (note
  divisions); off runs it free, where the knob reads one LFO cycle in
  milliseconds (10 ms - 2 s). Turning the knob up speeds the LFO up either way.
  **Rate** remembers where each mode was left, so flipping Sync back and forth
  keeps both settings; the first switch to free lands on 124 ms.

The LFO free-runs on a phase accumulator. When synced to a running transport it
also aligns to the host grid: a hard snap on the first playing block or a
transport jump (so the same bar always starts at the same phase), otherwise a
small per-block pull that shrugs off host `ppq` jitter and lets a division change
re-settle over a fraction of a second instead of clicking. The modulation signal
is slew-limited (~2.5 ms) as a backstop, so no snap or switch can ever step the
gain in a single sample.

Between the knobs and the pedal name is a live LFO preview: it redraws from the
Amount, Rate and Shape values, and switches to a mirrored pair of traces in
Panning to show the left and right motion.

The LFO is a plain phase accumulator. JUCE ships no tremolo primitive, and its
`juce::dsp::Panner` bakes in a 50 ms gain ramp that swallows LFO-rate motion, so
both laws are written out in the processor: tremolo as
`g = 1 - depth·(0.5 - 0.5·lfo)`, pan as a sample-accurate equal-power curve.

Like the other pedals it has no on/off switch of its own - the `on` parameter
crossfades to the dry signal so the host's device on/off never clicks. The face
uses a new `teal()` theme: a `#2d8a8e` panel with `#fee1b8` legend and a darker
teal value arc on black caps.

### Easy Chorus

A wide stereo chorus. Mono or stereo in, stereo out. Four knobs:

| Knob      | Range        | What it does                                                              |
| --------- | ------------ | ----------------------------------------------------------------------- |
| **Rate**  | 0.05 - 8 Hz  | LFO speed. Free-running, not tempo-synced. Skewed so the slow end has most of the travel |
| **Depth** | 0 - 100 %    | How far the LFO swings the delay taps - subtle at the bottom, detuned and seasick at the top |
| **Phase** | 0 - 180°     | The width control: how far the right channel's LFOs lag the left's. Narrow at the bottom, wide at the top. The offset it drives is capped short of true antiphase, so the image stays wide at every setting instead of periodically folding to mono |
| **Mix**   | 0 - 100 %    | Blend of dry signal and chorus. 50 % is a strong, usable chorus         |

The wet path is fed from a **mono sum** of the input and read back through two
modulated delay taps per channel (`ee::dsp::ModDelayLine`, cubic-Hermite
fractional reads). The two taps sit at different base delays - roughly 9 / 14 ms
on the left, 12.5 / 18 ms on the right - and that left/right asymmetry opens the
image before the LFOs do anything, and keeps it open at the instants the
modulation is momentarily still. Turning **Phase** up offsets the right
channel's sine LFOs from the left's, which spreads the chorus wider; the two
taps of each channel are held off exact antiphase so the effect never briefly
cancels itself. The
dry signal passes straight through untouched; only the wet is coloured, by a
one-pole highpass (~100 Hz, keeps the low end tight and centred) and lowpass
(~9 kHz, keeps the moving reads from adding fizz).

Like the other pedals it has no on/off switch of its own - the `on` parameter
crossfades to the dry signal so the host's device on/off never clicks. The face
uses a new `sky()` theme: a pale `#8bcbdb` cyan panel with a near-black legend
and a deep-teal value arc on black caps.

The full voicing - voice count, per-voice base delays, depth range, wet filter
corners, knob defaults - lives in `shared/include/ee/dsp/ChorusConfig.h`; retune
it there and rebuild.

### Easy Overdrive

A soft-clipping overdrive with a Boss-OD voicing. Mono or stereo, in and out -
each channel is driven independently. Three knobs, on the small Easy Reverb
footprint: **Level** and **Drive** across the top, **Tone** centred in a row of
its own below.

| Knob      | Range        | What it does                                                              |
| --------- | ------------ | ----------------------------------------------------------------------- |
| **Level** | -30 - +6 dB  | Output volume after the drive. Unity at noon, a little boost on tap for pushing an amp |
| **Drive** | 0 - 100 %    | Gain into the clipper, swept exponentially. Always a little hair at 0; slammed and compressed at 100 |
| **Tone**  | 0 - 100 %    | A tilt around ~640 Hz: dark and thick at the bottom, bright and cutting at the top, near-flat in the middle |

The path per sample is a one-pole high-pass ahead of the clipper (~190 Hz, the
Tube-Screamer trick that keeps the low strings out of the distortion so chords
stay defined), an exponential gain stage (`×2.5` – `×260`), an asymmetric `tanh`
clip with a small DC bias for even harmonics, then the removed sub-bass folded
back in unclipped so the note keeps its body. After the clip comes the tilt tone
control, a fixed 11 kHz low-pass to tame the buzz (the stage does **not**
oversample - the clipper is soft and, at any real instrument level, fed nowhere
near a hard corner), and a DC blocker. The engine loudness-compensates Drive
roughly so winding it up trades headroom for saturation rather than volume; the
Level knob trims the rest.

Like the other pedals it has no on/off switch of its own - the `on` parameter
crossfades to the dry signal so the host's device on/off never clicks. The face
uses a new `yellow()` theme: a warm `#e8b400` amber panel with a near-black
legend and a deep brown-amber value arc on black caps.

The full voicing - drive gain range, clip bias, pre-clip high-pass, tilt pivot
and band gains, post low-pass, loudness-compensation trim, knob defaults - lives
in `shared/include/ee/dsp/OverdriveConfig.h`; retune it there and rebuild.

## Requirements

- macOS with Xcode Command Line Tools
- CMake and Ninja: `brew install cmake ninja`

## Setting up on another Mac

The repo vendors no dependencies — JUCE and DaisySP (the pitch shifter behind
Easy Reverb's shimmer) are both fetched by CMake at configure time against
pinned tags, so the first configure needs an internet connection.

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
auval -v aufx Etpn Eefx                                     # Easy Trem & Pan
auval -v aufx Echr Eefx                                     # Easy Chorus
auval -v aufx Eovd Eefx                                     # Easy Overdrive
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
  easy-delay/        processor + tape colour stage
  easy-eq/           processor + juce::dsp IIR band filters
  easy-trem-pan/     processor + phase-accumulator LFO, hand-written trem/pan
  easy-overdrive/    processor + soft-clipping drive stage
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

For a pedal with vertical faders instead of knobs (a graphic EQ), fill
`spec.sliders` instead of `spec.knobs` — same `{ parameterID, caption }` pairs.
They lay out in one row across the face. `plugins/easy-eq` is the worked
example.

`spec.centreKnob` drops one small cap into the middle of the knob block for a
secondary trim (`plugins/easy-reverb` puts Resonance there). Give it
`compact = true` for the small size and `compactCaption = true` to print the
caption on its one text line instead of the value.

Then copy `plugins/easy-reverb/CMakeLists.txt`, change `PLUGIN_CODE` and
`PRODUCT_NAME`, and add it to the top-level `CMakeLists.txt`.

## Resizing

Every pedal is resizable for free. `PedalEditor` draws the face at its design
size in an inner component and scales that to fill the window, with the aspect
ratio locked and the range clamped to `kMinZoom`..`kMaxZoom`. Drag the faint
diagonal grip in the bottom-right corner, or the host's own window edge. No
per-pedal code — it all lives in `PedalEditor`. (The zoom is not persisted; the
editor opens at `kDefaultZoom`, currently 85 %.)

Knob columns are spaced the same on every pedal: a face with a knob row sets
`spec.width = ee::ui::knobRowWidth (spec.knobsPerRow)`, which derives the width
from shared metrics in `PedalSpec.h` rather than a hand-picked number.

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
