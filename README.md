# Synth Peak

[synthpeak.com](https://synthpeak.com)

Guitar pedal-style audio plugins for Ableton Live, built with JUCE. Designed to
sit after an amp sim like NAM.

Builds as **VST3**, **AU** and a **Standalone** app.

## Plugins

### Peak Reverb

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
flip `showTuner` in `PluginProcessor.cpp`) to open Peak Reverb with a side panel
of live sliders for every value plus a copy-paste-ready readout of the struct —
a development build only, the way `-DEE_TAPE_TUNER=ON` works for Peak Delay.

The pedal carries no on/off switch of its own — use the host's device on/off.
The `on` parameter has **trails**: bypassing stops feeding the network but leaves
the wet path open, so the existing tail rings out instead of being cut off.

High and low frequency decay rates are fixed internally (lows ring slightly
longer, highs die faster) so the tail sits behind a guitar without getting fizzy.

### Peak Spring

A dispersive spring tank. Mono in, stereo out, two knobs and a switch:

| Control        | Range       | What it does                                       |
| -------------- | ----------- | -------------------------------------------------- |
| **Mix**        | 0 - 100 %   | Blend of dry signal and wet tank                   |
| **Decay**      | 0.4 - 8 s   | How long the springs ring on after the note stops  |
| **Mono / Stereo** | switch   | One tank feeding both outputs, or two a few per cent apart |

Each knob prints one line of text: the caption at rest, the reading only while
you are actually turning it.

**Mono** is the honest setting — a real tank is a single mono device — and it is
what to use if the mix has to fold down. **Stereo** runs a second tank whose
springs differ by about three per cent and crosses the two into each other, which
opens the tail out without either side sounding detuned or hollow in mono.

Where Peak Reverb models a plate, this models the steel box bolted into the
bottom of an amp. Three springs run in parallel, each a short delay loop with a
cascade of stretched all-pass sections *inside* the feedback path. Those
sections are flat in magnitude but not in group delay, so the top of the
spectrum takes longer round the loop than the bottom: a transient goes in and
comes back as a rising "boinngg" that stretches further on every bounce. That
dispersion is the whole sound — take it out and the pedal is a short, dull
delay.

The tank is driven through a band-pass (a transducer, not a speaker) and loses
energy on every pass, which is why a spring sits behind a guitar rather than on
top of it. Each spring's length wanders by a fraction of a per cent so the tank's
comb never rings on one fixed set of pitches.

The voicing is measured against a reference tank rather than invented. Two
renders of it — Mix 26 / Decay 3.58 s, and Mix 100 / Decay 8 s — were turned into
per-band decay times, and those into the per-trip loop gain that would produce
them. What that says is that a spring's loop is nearly flat right across the
midrange and falls off a cliff below it: about 4 % lost per trip through the ring
band and only a little more at 4 kHz, but an order of magnitude more under
200 Hz. The first attempt here used a one-pole low-pass at 4 kHz in the loop,
which loses 28 % per trip up there and killed the top of the tail three trips in.

That is why the loop damping is a pair of **shelves** (the shared `LoopDamper`,
the same absorber the plate reverb uses) rather than rolloffs: a rolloff keeps
eating the same band on every pass and collapses the top of the tail, where a
shelf holds a fixed ratio. The low end of a spring still has to die fast, so the
weight under 250 Hz is put back by a shelf on the finished wet output, outside
every feedback path — level without decay.

The Decay knob is calibrated against that reference, so 3.58 s on this face means
the same tail as 3.58 s on theirs. Measured band decay times land within about
10 % through the ring band, and the stereo tail correlation within 0.02.

Everything above — how many springs, how long, how hard they chirp, where the
shelves sit — is voicing, and lives in `shared/include/ee/dsp/SpringConfig.h`
with the trade-offs and the measurements written next to each value.
`tests/SpringMatch.cpp` (`ee_spring_match`) renders a file through the whole
processor for A/B-ing against a reference.

Like Peak Reverb, the pedal has no on/off switch of its own and the `on`
parameter has **trails**: bypassing stops driving the tank but leaves the wet
path open, so whatever is still ringing rings out.

### Peak Delay

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

### Peak EQ

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

The face reuses Peak Delay's `silver()` theme, and is the same width and height
as Peak Reverb, so the pedals line up on a rack.

### Peak Trem & Pan

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
- A **Sync** button centred above the **Rate** knob - Peak Delay's `MiniToggle`,
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

### Peak Chorus

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

### Peak Overdrive

A diode-clipper overdrive with a Boss-OD voicing. Mono or stereo, in and out -
each channel is driven independently. Three knobs, on the small Peak Reverb
footprint: **Level** and **Drive** across the top, **Tone** centred in a row of
its own below.

| Knob      | Range        | What it does                                                              |
| --------- | ------------ | ----------------------------------------------------------------------- |
| **Level** | -30 - +6 dB  | Output volume after the drive. Unity at noon, a little boost on tap for pushing an amp |
| **Drive** | 0 - 100 %    | Gain into the clipper, swept exponentially. Always a little hair at 0; slammed and compressed at 100 |
| **Tone**  | 0 - 100 %    | A tilt around ~640 Hz: dark and thick at the bottom, bright and cutting at the top, near-flat in the middle |

The clipping stage is the circuit an SD-1 / Tube Screamer clips with, solved
sample-accurately as a **Wave Digital Filter** with
[`chowdsp_wdf`](https://github.com/Chowdhury-DSP/chowdsp_wdf): a driven voltage
source through a 2.2 kΩ series resistor into a node holding a 10 nF capacitor and
an anti-parallel silicon diode pair (`DiodePairT`, Werner model) to ground. The
diode I-V curve gives the soft knee; the cap across the diodes (~7 kHz corner)
rolls the fizz off the distortion the way the op-amp feedback cap does in the
real pedal, so it reads as *overdrive*, not *fuzz*.

Around it: a one-pole high-pass ahead of the clipper (~190 Hz, the Tube-Screamer
"flub filter" that keeps the low strings out of the distortion so chords stay
defined); an exponential gain stage (`×1.4` – `×300`) standing in for the op-amp
gain; a small asymmetry shaper (`y − k·y·|y|`, exactly zero at zero) for the
even-harmonic warmth an SD-1 gets from its uneven diode legs; then the tilt tone
control, a fixed 12 kHz low-pass and a DC blocker. The gain stage and the WDF
clipper run at **2× oversampling** with a 4th-order Butterworth anti-imaging /
anti-aliasing pair, so the hard part of the diode curve does not fold aliases
into the top octave (measured >30 dB down). A mild make-up lifts the clip output
- which rides near the diode clamp voltage - back to a usable level; the Level
knob trims the rest.

Like the other pedals it has no on/off switch of its own - the `on` parameter
crossfades to the dry signal so the host's device on/off never clicks. The face
uses a new `yellow()` theme: a warm `#e8b400` amber panel with a near-black
legend and a deep brown-amber value arc on black caps.

The full voicing - drive gain range, diode and RC values, asymmetry, pre-clip
high-pass, oversampling, tilt pivot and band gains, post low-pass, make-up, knob
defaults - lives in `shared/include/ee/dsp/OverdriveConfig.h`; retune it there
and rebuild.

### Peak Wah

An LFO-driven modulated filter that plays with your picking - the auto-wah's
tank, swept by a wave that speeds up when you dig in and restarts from its peak
on every note. Mono or stereo, in and out. A big row of headline knobs, a tight
row of LFO controls, a live filter-response scope, and a Mono/Stereo switch
level with the pedal name.

| Knob       | Range        | What it does                                                                                  |
| ---------- | ------------ | ------------------------------------------------------------------------------------------- |
| **Range**  | 0 - 100 %    | Depth of the sweep - how far the LFO pushes the cutoff either side of Freq (up to ~2.3 octaves) |
| **Freq**   | 200 - 1600 Hz | Centre cutoff the LFO sweeps around. Readout shows the mapped Hz                              |
| **Q**      | 0 - 100 %    | Resonance of the tank - a broad tone-shaping sweep at the bottom, a sharp vocal peak at the top |
| **Mix**    | 0 - 100 %    | Dry / wet blend (the spoon cap). 0 is bit-exact dry                                           |

| Small knob | What it does                                                                                     |
| ---------- | ---------------------------------------------------------------------------------------------- |
| **Decay**  | What happens after a pluck. At **0** the LFO runs exactly one cycle and then flattens - a single envelope sweep per note. In the middle it is a release-time follower (60 ms - 3 s tail). Fully up **latches the LFO on** so it just runs |
| **Shape**  | Morphs the LFO through the shared anchors - exp decay, ramp, triangle, soft square, hard chop; its value row shows the wave as a glyph |
| **Time**   | LFO rate. **Sync** button locks it to the host tempo - the readout flips from milliseconds to note divisions, and each mode remembers its own knob position |
| **Type**   | Low-pass, band-pass or high-pass, read off three taps of the one tank solve; the value row shows the curve shape as a glyph rather than a word |

**Mono / Stereo** (the switch, plain black-and-white, on the name row): Mono runs
one LFO for both channels; Stereo runs the right channel half a cycle out of
phase.

Every continuous control that feeds the filter - Mix, Range, Q, the Freq base -
is ramped over ~15 ms, so a knob nudge glides rather than steps (no tick). The
first block after a load takes the values as they stand.

Two touches make it feel played rather than mechanical, both always on:

- **the LFO speeds up with your picking** - a dynamics follower lifts the rate by
  up to 15 % on a hard hit, so the wobble breathes;
- **every note restarts the LFO at its peak** - a transient detector resets the
  phase to 0 (where the wave is at +1) on each pluck, so the sweep always kicks
  from the top. In Stereo the left channel starts at the peak, the right at its
  opposite.

The **scope** above the pedal name is a live filter-response graph: a blue
resonant bump sits at the Freq setting, and two warm bumps sweep either side of
it with the left and right channels' LFO. They collapse onto the blue one as the
gate closes, and the dot on the blue apex rises with Q. The processor publishes
the per-channel sweep amount to it each block.

The filter is a real wah's **LC tank** - a series RLC solved sample-accurately
as a **Wave Digital Filter** with
[`chowdsp_wdf`](https://github.com/Chowdhury-DSP/chowdsp_wdf), the same library
behind Peak Overdrive. One solve gives all three responses: the voltage across
the capacitor is a low-pass, across the resistor a band-pass, across the
inductor a high-pass. A fixed 0.5 H inductor plus a swept capacitor set the
centre frequency and the resistor sets Q; `C` and `R` are re-solved per channel
every 16 samples from `C = 1 / ((2πf₀)²L)`, `R = (1/Q)·√(L/C)`.

The LFO free-runs on a phase accumulator (aligned to the host grid when Sync is
on, the same snap/pull as Peak Delay). A gate scales the modulation depth: a
fast follower on the high-passed, rectified, noise-floored input, whose release
is set by Decay - with a floor under it that the top of the knob ramps to 1, and
a one-shot the bottom of the knob crossfades in that holds for one LFO cycle
after a pluck and then fades. So `cutoff = Freq · 5^(Range · gate · lfo)`.

After the tank: a per-type make-up gain (a band-pass tap throws away everything
off the peak and needs the most lift; the low- and high-pass taps keep a whole
half of the spectrum and need less), a `tanh` that is unity at normal levels and
only rounds the hottest peaks, then a DC blocker and a mild low-pass.

Like the other pedals it has no on/off switch of its own - the `on` parameter
crossfades to the dry signal so the host's device on/off never clicks. The face
uses a `pink()` theme: a light-pink (`#ffb6c1`) panel with a near-black legend
and a deep wine value arc on black caps, the same high-contrast recipe as the
orange and yellow faces.

The full voicing - gate high-pass / noise floor / sensitivity, attack and decay
range, the Decay latch knee, the one-shot knee and release, the dynamics
follower and rate depth, the retrigger thresholds, stereo offset, the param-ramp
time, frequency range and sweep ratio, inductor value, Q range, control-block
size, per-type make-up, grit, output filtering, knob defaults - lives in
`shared/include/ee/dsp/AutoWahConfig.h` (and the LFO rate range in
`plugins/peak-wah/src/RateMap.h`); retune there and rebuild.

### Peak Grain

A granular delay into a plain plate. Mono or stereo in, stereo out. Fifteen
knobs in four captioned sections, a **Live / Freeze** switch across the top, and
Reverb and Mix bare underneath.

**Delay** - the echo, and what you do to a frozen buffer:

| Knob         | Range           | What it does                                                              |
| ------------ | --------------- | ------------------------------------------------------------------------ |
| **Time**     | 20 ms - 2 s     | How far behind the write head grains are tapped from - the delay. Skewed so the short, rhythmic end gets most of the travel. Scatter sprays grains around this point, so it is the centre of a window rather than one hard offset |
| **Feedback** | 0 - 92 %        | Share of the granulated output written back into the buffer, so each repeat is granulated again on the way round. Capped below unity - the path is no longer feed-forward, and a gain of 1 would let a stuck level build without bound |
| **Stretch**  | -100 - +100 %   | **Frozen only.** The rate the read head scans the captured buffer, in multiples of realtime: `+100 %` forward (the delay time holds steady), `0` held on one moment, `-100 %` backwards. Pitch is untouched - only where the next grain is taken from moves. Reads as a dash while playing live |

**Grain** - what one grain is:

| Knob        | Range          | What it does                                                              |
| ----------- | -------------- | ------------------------------------------------------------------------ |
| **Size**    | 20 - 500 ms    | Grain length. Under ~40 ms the fragments stop being recognisable and turn into a metallic buzz at the spawn rate; over ~300 ms you hear whole notes come back |
| **Density** | 1 - 40 /s      | Grains spawned per second. Sparse and countable at the bottom, a continuous cloud at the top. It is not a volume knob - the engine divides out the overlap |
| **Shape**   | 0 - 100 %      | Grain envelope lean: `0` soft, a long fade-in with the energy spread the whole grain; `100` plucky, a click of an attack with the energy up front. The engine divides the envelope's own energy back out, so this does not double as a volume knob |

**Pitch** - weights against each other, not positions on a scale:

| Knob         | What it does                                                          |
| ------------ | --------------------------------------------------------------------- |
| **Low**      | How often a grain lands an octave down (a fourth down in one slot)    |
| **Unison**   | How often it plays at pitch                                           |
| **High**     | How often it lands an octave up (a fifth up in one slot)             |
| **Detune**   | Random detune on every grain, 0 - 100 ct either way. **Off by default**: above zero every grain plays slightly differently, which reads as an unstable cloud rather than as the note that was played |

Any two of Low/Unison/High at once is a chord rather than a transposition, which
is the whole reason they are separate knobs. All three at zero is treated as
unison, so a face with no pitch dialled in still makes a sound. Mostly octaves,
with a fifth up and a fourth down baked into one table slot each; the tables in
`GrainerTuning.h` will take anything you want, but stray far from those and the
cloud stops sounding like the note that was played.

**Random**:

| Knob         | Range          | What it does                                                             |
| ------------ | -------------- | ---------------------------------------------------------------------- |
| **Reverse**  | 0 - 100 %      | Share of grains that play backwards. Forward-only is much more legible; past halfway the phrase stops being followable at all |
| **Scatter**  | 0 - 100 %      | One knob over all the timing randomness: how much the gap between grains wanders, and how much each grain's length strays from Size. `0` is a metronome spraying identical grains; wound up the cloud stops repeating |
| **Stereo**   | 0 - 100 %      | Width of the random pan placement. `0` centres every grain, `100` throws them hard left and right. Equal-power, so the middle does not dip |

| Knob        | Range          | What it does                                                              |
| ----------- | -------------- | ------------------------------------------------------------------------ |
| **Reverb**  | 0 - 100 %      | One knob for the plate behind the cloud: it opens the mix and lengthens the decay together. The decay comes in over the top half of the travel, so the bottom half is a short room getting louder |
| **Mix**     | 0 - 100 %      | Blend of dry signal and the whole wet path, tail included                |

**Live** is a granular delay: the input is recorded into a ten-and-a-half second
circular buffer, and on a jittered timer the engine spawns a **grain** - a
windowed voice that reads that buffer from a point `Time` behind the write head
(scattered either side), at a random rate which is its pitch, in a random
direction, at a random pan position. Feedback writes the cloud back in, so the
repeats thicken as they granulate again. Up to 32 grains overlap.

**Freeze** stops the recording and holds the buffer. The read head then scans
the frozen capture at the **Stretch** rate - forwards at realtime holds the
delay steady, `0` stutters on one moment, backwards scrubs it - all without
shifting pitch, because each grain still plays at rate 1. A loud enough input
retriggers: the engine grabs a fresh `Time` window and re-freezes, so the loop
starts again on the new sound.

Live, most grains are the **attack**. A plucked string is mostly its first fifty
milliseconds, and a cloud built from the sustain alone loses whatever made the
note identifiable. An onset detector (`ee::dsp::OnsetGate`, the same one Peak Wah
retriggers from) marks where each attack landed, and 70 % of grains are drawn
from there rather than from the Time window - until `kAttackReachSeconds` after
the note, when it has rung out and the cloud moves on. Reads are cubic-Hermite,
the same interpolator the delay lines use - linear would take the top octave off
every backwards grain.

Each grain has a **percussive envelope**, not a symmetric window: a short
fade-in and then an exponential decay to zero. That asymmetry is the difference
between the effect sounding like a guitar and sounding like a tape running
backwards - a Hann window fades a grain in over its entire first half, which
throws away the transient and leaves a swell the ear reads as reverse playback.
Both ends still reach exactly zero, which is what stops a grain clicking whatever
its content. **Shape** morphs between the two ends the tuning header names.

The feedback path means the engine can, in principle, latch a non-finite value
or build without bound. Four things stop it: Feedback is capped below unity, the
fed-back sample is run through `tanh` so it is always under 1, a non-finite
buffer write is zeroed, and the processor guards its own output on top.
`ee_grain_stress` sweeps the feedback range against DC and NaN input to keep this
honest.

Everything about the character that is not on a knob - how Scatter and Shape map
onto the engine, the interval tables, the output trim - lives in
`shared/include/ee/dsp/GrainerTuning.h`. Build with `-DEE_GRAIN_TUNER=ON` to get
a side panel that drives all of it live. `GrainerConfig.h` keeps the structural
side: the knob ranges, the recording buffer and the voice count.

Behind the cloud is `ee::dsp::FdnReverb`, the same sixteen-line network as Peak
Reverb, run plain: no shimmer, and its resonance and low cut pinned in the
tuning header rather than put on the face. It is fed the grains and nothing
else. Bypass leaves the wet path open, so the cloud and its tail ring out
instead of being chopped off.

The face is `PedalTheme::onyx()` - the soft-UI style at night, a near-black card
with black caps, and one pale blue-grey carrying every reading on it.

### Peak Tape

A tape machine as a pedal. Mono or stereo, in and out. Five knobs and a switch
on the Peak Delay footprint:

| Control        | Range        | What it does                                                                                     |
| -------------- | ------------ | ------------------------------------------------------------------------------------------------ |
| **Saturation** | 0 - 100 %    | How hard the record head is driven. Level-matched, so the knob adds harmonics and squash rather than volume |
| **Tone**       | -100 - 100 % | Tilt around a fixed 700 Hz pivot, on a smaller centre-detented cap between the two big ones: left is dark, right is bright, `0 %` is flat and the stage is bypassed exactly |
| **Flutter**    | 0 - 100 %    | Depth of the wobble riding the transport — a pure ~2 Hz sine, voiced off a reference recording: 1.6 ms of excursion at 100 %, ~35 cents of pitch |
| **Wear**       | 0 - 100 %    | How tired the tape is. This *is* Peak Delay's **Tape** knob — the same stage, the same voicing, on a knob of its own |
| **Noise**      | 0 - 100 %    | The tape floor: a recording of a real one, looped. 100 % is that recording at the level it was made |
| **Mono / Stereo** | switch    | Mono, both channels read one transport and a mono source stays exactly mono. Stereo, they read a slow modulation a third of a cycle apart and the image opens out, chorus-like. Stereo by default |

Peak Delay already has a **Tape** knob — one macro voiced against a reference
machine, a colour you dial in behind a delay. Peak Tape is the machine around
it: the transport in front, the record head, the floor and the tone control
after, each on its own control, with that same stage carrying Wear. It is not a
second model of it — `TapeMachine` drives `TapeCharacter` directly, so the two
pedals cannot drift apart: retuning `shared/include/ee/dsp/TapeTuning.h` moves
both.

Signal path, per sample, per channel:

1. the whole signal is read off a delay line whose length wanders — a ~2 Hz sine
   (**Flutter**) plus the second, slower modulation the **Stereo** switch opens.
   Nothing else: a real transport has filtered noise riding the wobble too, and
   an earlier voicing had it, but it roughens the vibe rather than adding to it;
2. the **record head**: drive into an asymmetric `tanh`, 2x oversampled, wrapped
   in a record-EQ shelf and its *exact* inverse — so the treble arrives at the
   head hotter than the bass and distorts first, which is what a real machine
   does with its record EQ;
3. the **tape**: `TapeCharacter`, driven by Wear;
4. the **tape floor**, looped from the recording with a crossfaded seam;
5. the **tilt** tone control;
6. a DC blocker, engaged with the saturation that can leave an offset.

The floor is not gated and does not ride the programme — a floor that ducks when
you play is a noise gate, not a tape, so it is there whether anything is playing
or not. The recording lives in `assets/audio/tape-noise.wav` and is embedded in
its own binary-data target (`ee_tape_noise`), linked by this pedal alone rather
than riding along in every plugin. The engine itself stays pure DSP: the pedal
hands it the decoded samples, and with none supplied it falls back to
synthesised band-limited hiss.

Tone has no dead band around its centre — a hair off flat is not flat. Instead
the knob **snaps onto the middle** while you drag it (`KnobSpec::centreDetent`),
so landing on `0 %` is a flick rather than a nudge, and only exactly-centred
bypasses the stage.

Every stage except the floor is bypassed *exactly* at its resting position, and
the delay read lands on a whole sample when the transport is still — so with
Saturation, Wear, Flutter and Noise at 0 and Tone centred, the pedal is
bit-exact pass-through. Its latency (the transport line plus the tape stage's
own, 6 ms at 48 kHz) is constant whatever the controls do, and reported to the
host so it is compensated.

The face uses a new `green()` theme, struck in the deep green of Peak Delay's
Tape cap — the only dark-panel face in the range, which is the point. Tone's
value arc grows out of 12 o'clock in whichever direction it is turned, with a
tick marking the detent (`KnobSpec::bipolarArc`), and it takes a smaller cap
than its neighbours (`KnobSpec::diameter`) because it is the trim among them.
The middle of the bottom row is a spacer entry in the knob grid (a `KnobSpec`
with no parameter ID), and the Mono/Stereo switch is the same `SlideToggle` Peak
Trem & Pan uses for its mode, in the strip above the knobs.

The voicing — transport rate and depth, the width modulation, drive range, bias,
record-EQ pivot and gain, tilt gains, loop crossfade, control defaults — lives in
`shared/include/ee/dsp/TapeMachineConfig.h`; Wear's is the delay's own in
`TapeTuning.h`. Retune either and rebuild.

To voice it against a recording, `ee_tape_render` puts a file through the engine
with every control on the command line:

```bash
./build/tests/ee_tape_render_artefacts/Release/ee_tape_render dry.wav out.wav 0 0 100 0 0 0
```

Flutter was voiced that way: render the dry take with Flutter at 100 and
everything else at 0, track the result against the dry file, and the 2 Hz line
lands on the reference recording's (70.1 samples against 70.3).

## Requirements

- macOS with Xcode Command Line Tools
- CMake and Ninja: `brew install cmake ninja`
- Optional but recommended: `brew install ccache` — the build picks it up
  automatically and shared code compiles into every plugin here, so it helps a lot

## Setting up on another Mac

The repo vendors no dependencies — JUCE, DaisySP (the pitch shifter behind Peak
Reverb's shimmer) and chowdsp_wdf (the Wave Digital Filter behind Peak
Overdrive's diode clipper) are all fetched by CMake at configure time against
pinned tags, so the first configure needs an internet connection.

```bash
xcode-select --install          # if you have never installed the CLT
brew install cmake ninja ccache
git clone <this repo> synth-peak && cd synth-peak
cmake --preset dev
cmake --build build
```

Building on the machine you play on is the path of least resistance: the binary
matches that Mac's architecture, and locally built files carry no quarantine
flag, so Gatekeeper stays out of the way.

## Build

```bash
cmake --preset dev
cmake --build build
```

While working on one pedal, the `fast` preset is about five times quicker — it
builds Standalone only, without LTO, and installs nothing:

```bash
cmake --preset fast -DEE_PLUGINS="peak-wah"
cmake --build build-fast
```

`EE_INSTALL_PLUGINS` (on by default outside `fast`) installs to `~/Library/Audio/Plug-Ins/VST3` and
`~/Library/Audio/Plug-Ins/Components` automatically. Rescan in Live to pick up
changes.

For a build to hand to someone else, produce a universal binary:

```bash
cmake --preset release
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
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Peak Reverb.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Peak Reverb.component"
```

Cloning the source and building there avoids the whole problem.

## Verifying

```bash
./build/tests/ee_dsp_tests_artefacts/Release/ee_dsp_tests   # DSP: decay accuracy, stability, levels
./build/tests/ee_ui_snapshot_artefacts/Release/ee_ui_snapshot /tmp   # renders the UI to PNG
auval -v aufx Prvb Peak                                     # Apple's AU validation
auval -v aufx Ptpn Peak                                     # Peak Trem & Pan
auval -v aufx Pchr Peak                                     # Peak Chorus
auval -v aufx Povd Peak                                     # Peak Overdrive
auval -v aufx Pwah Peak                                     # Peak Wah
auval -v aufx Ptap Peak                                     # Peak Tape
```

The tape machine also has its own sweep, which walks every knob combination and
a handful of adverse inputs looking for a non-finite or runaway output:

```bash
./build/tests/ee_tape_stress_artefacts/Release/ee_tape_stress
```

`pluginval` (`brew install --cask pluginval`) covers the VST3:

```bash
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --strictness-level 10 --validate-in-process \
  --validate ~/Library/Audio/Plug-Ins/VST3/"Peak Reverb.vst3"
```

## Layout

```
shared/
  include/ee/dsp/    reusable DSP primitives + the reverb engine
  include/ee/ui/     the pedal UI framework
plugins/
  peak-reverb/       processor + parameter definitions
  peak-delay/        processor + tape colour stage
  peak-eq/           processor + juce::dsp IIR band filters
  peak-trem-pan/     processor + phase-accumulator LFO, hand-written trem/pan
  peak-overdrive/    processor + WDF diode-clipper drive stage (chowdsp_wdf)
  peak-wah/          processor + LFO-swept WDF LC-tank filter, LP/BP/HP (chowdsp_wdf)
  peak-tape/         processor + the full tape machine: transport, record head, floor
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
spec.name = "Peak Drive";
spec.tagline = "...";
spec.knobs = { { "gain", "Gain" }, { "tone", "Tone" }, { "level", "Level" } };

return new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::dark());
```

For a pedal with vertical faders instead of knobs (a graphic EQ), fill
`spec.sliders` instead of `spec.knobs` — same `{ parameterID, caption }` pairs.
They lay out in one row across the face. `plugins/peak-eq` is the worked
example.

`spec.centreKnob` drops one small cap into the middle of the knob block for a
secondary trim (`plugins/peak-reverb` puts Resonance there). Give it
`compact = true` for the small size and `compactCaption = true` to print the
caption on its one text line instead of the value.

A `KnobSpec` with no parameter ID is a **spacer**: it holds its column in the
grid and draws nothing, so a face can leave a hole in its block (`peak-tape`
leaves the middle of its bottom row open). Point a toggle's `afterKnobIndex` at
a spacer and the button is centred in that empty cell.

Three more `KnobSpec` fields suit a control that rests at its centre, all of
which `peak-tape` uses on Tone: `bipolarArc` grows the value arc out of 12
o'clock either way with a tick on the detent, `centreDetent` makes the knob snap
onto the middle while dragging (mouse only — automation and typed values pass
through), and `diameter` gives that one knob a smaller cap without moving it off
the grid or dropping its caption the way `compact` would.

`spec.knobGroups` sorts the main knobs into captioned boxes — one centred row
per group, each wrapped in the rounded outline with its name let into the top
edge. List `{ caption, count }` entries; they consume `spec.knobs` in order and
any left over form a trailing bare row. `plugins/peak-grain` uses four (Delay,
Grain, Pitch, Random) with Reverb and Mix bare underneath. Leave it empty for
the plain `knobsPerRow` grid.

Then give it a `plugins/peak-drive/CMakeLists.txt`:

```cmake
peak_add_plugin(PeakDrive
    CODE       Pdrv
    PRODUCT    "Peak Drive"
    BUNDLE     com.synthpeak.peakdrive
    CATEGORIES "Fx Distortion"
)
```

`peak_add_plugin` (in `cmake/AddPeakPlugin.cmake`) carries the rest of the
`juce_add_plugin` boilerplate. Pass `LIBS` for extra link targets and `SOURCES`
for extra `.cpp` files. Add the directory name to `EE_ALL_PLUGINS` in the
top-level `CMakeLists.txt`, and mirror the parameter layout into
`tests/UiSnapshot.cpp` so the pedal renders in the snapshot tool.

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
