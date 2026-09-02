# Peak Grain — implementation plan

A granular scatterer with a plain reverb behind it. Our simplified take on the
Eventide "Grainer / Stutter": seven knobs instead of thirty, no LFO section, no
envelope triggering, no shimmer.

Status: **built.** `plugins/peak-grain` compiles, the face renders, and
`scripts/dev-check.sh peak-grain` is green. What follows is the design as
built; the three places reality differed from the plan are noted in §9.

---

## 1. What the effect is

The reference is not a pitch shifter. It is a granular buffer scatterer:

1. Input is recorded continuously into a short circular buffer.
2. On a timer, a **grain** is spawned: a windowed playback voice that reads from
   a random point in the recorded past, at a random rate (= pitch), in a random
   direction, at a random pan position.
3. Many grains overlap. The result is a cloud of fragments of what you just
   played.
4. That cloud is fed into a reverb, which is where most of the length in the
   reference recordings comes from.

Measured from `grainer.wav` / `grainer2.wav` (17.0 s, 44.1 kHz, stereo): each hit
decays over roughly 8–10 seconds. That is dominated by the patch's
`Reverb Decay 30.00 s`, not by the granular engine — hence bringing the reverb
back in. See §7 for the one place we cannot match it.

---

## 2. The controls — seven knobs

### Grain engine (four)

| ID        | Caption   | Range                       | Default | Readout        |
|-----------|-----------|-----------------------------|---------|----------------|
| `size`    | Size      | 20–500 ms, skewed low       | 120 ms  | `msToText`     |
| `density` | Density   | 1–40 grains/s, skewed low   | 12 /s   | `"12 /s"`      |
| `spray`   | Spray     | 0–2000 ms, skewed low       | 400 ms  | `msToText`     |
| `pitch`   | Pitch     | −100…+100, bipolar          | 0       | `percentToText`|

- **Size** is the main timbral control. Below ~40 ms grains stop being fragments
  and become a metallic buzz; at 300 ms+ you hear recognisable notes.
- **Density** turns sparse plinks into a continuous cloud. It is *not* a level
  control — see §5, gain compensation.
- **Spray** is how far back into the buffer grain start points are drawn from.
  At 0 the effect is a stutter on the present; turned up it is a smear.
- **Pitch** is a *spread*, not a transpose. At 0 every grain plays at 1.0.
  Turned right, a growing share of grains land on random upward intervals;
  turned left, downward. The intervals are quantised to a table
  (−12, −7, −5, 0, +7, +12 semitones) so a dense cloud stays consonant instead
  of turning into a detune smear. Use `bipolarArc = true` and
  `centreDetent = true` on the `KnobSpec`.

### Reverb (two)

Our own `ee::dsp::FdnReverb`, run plain: **no shimmer, no exposed resonance, no
tuning panel.**

| ID      | Caption | Range                    | Default | Readout           |
|---------|---------|--------------------------|---------|-------------------|
| `verb`  | Verb    | 0–100 %                  | 35 %    | `percentToText`   |
| `decay` | Decay   | 0.5–8.0 s (`kMinDecay`…`kMaxDecay`) | 4.0 s | `secondsToText` |

Everything else on the reverb is nailed down in `GrainerConfig.h` and never
touched at runtime:

- `setShimmer (0.0f)` — the header states 0 means the shimmer path does not run
  at all, so this costs nothing in CPU. No DaisySP pitch shifters spin up.
- `setResonance (kVerbResonance)` — one constant, around 0.35. Low is the
  smeared, plate-like end, which suits a dense grain cloud. Never exposed.
- `setLowCut (kVerbLowCutHz)` — around 120 Hz. Grains stack up and a flat
  reverb under them turns to mud fast. Also never exposed.
- `setDecayTilt` — leave at the `ReverbConfig.h` defaults.

### Output (one)

| ID    | Caption | Range   | Default | Readout         |
|-------|---------|---------|---------|-----------------|
| `mix` | Mix     | 0–100 % | 50 %    | `percentToText` |

Global wet/dry, equal-power, applied after the reverb. House convention: every
pedal has one.

### Bypass

The standard `on` bool + `ee::plugin::crossfadeToDry`, as every other pedal.

### Mapping from the reference faces

| Eventide control                  | Here                          |
|-----------------------------------|-------------------------------|
| Mix                               | Mix                           |
| Smear, Length                     | Size                          |
| Rate, Chance                      | Density                       |
| Length (how far back)             | Spray                         |
| Pitch, Detune, Pitch Chance       | Pitch                         |
| Reverb Mix, Reverb Decay          | Verb, Decay                   |
| Spread Type, Spread Amt, Width    | baked into `GrainerConfig.h`  |
| Direction Mod (Grainer 2)         | baked: reverse-grain chance   |
| Interrupt, Trigger Mode, Env Sens | dropped — the engine free-runs |
| LFO Dest / Rate / Amount / Shape  | dropped                       |
| Filter, Filter Mod                | dropped (the reverb low cut stays) |

Total: Size, Density, Spray, Pitch, Verb, Decay, Mix = **seven knobs**. If that
is one too many on the face, **Spray is the one to fold into Size** (long grains
drawn from far back) — not Pitch.

---

## 3. Signal flow

```
in ──┬─────────────────────────────────────────────► dry
     │
     └─► record buffer ─► grain voices ─┬──────────► grain wet (stereo)
                                        │
                                        └─► mono sum ─► FdnReverb ─► verb wet (stereo)

grain wet ·(1 − Verb)  +  verb wet · Verb   =   wet
wet · Mix  +  dry · (1 − Mix)               =   out      (equal-power)
```

Two things this ordering gets right:

- The reverb is fed the **grain output only**, never the dry input. That is what
  the Eventide's "Reverb Mix" does, and it keeps the dry signal clean.
- `Verb` is *inside* the wet path; `Mix` is global. Turning Mix down fades the
  whole effect, tail included.

`FdnReverb::process()` is **mono in, stereo out** — check the signature. The
grain wet must be summed to mono for the send. Mirror the chunked call in
[peak-reverb](../plugins/peak-reverb/src/PluginProcessor.cpp) around line 181.

Trails on bypass: copy peak-reverb's approach — stop feeding the network but
leave the wet path open so the existing tail rings out instead of being chopped.

`getTailLengthSeconds()` = grain buffer seconds + `reverb.getTailSeconds()`.

---

## 4. Files

### New

| File | What |
|------|------|
| `shared/include/ee/dsp/GrainerConfig.h` | Voicing constants. Column-aligned tables, matching the other `*Config.h`. |
| `shared/include/ee/dsp/Grainer.h` | The engine, header-only like `Chorus.h` / `Phaser.h`. |
| `plugins/peak-grain/CMakeLists.txt` | `peak_add_plugin(PeakGrain CODE Pgrn PRODUCT "Peak Grain" BUNDLE com.synthpeak.peakgrain CATEGORIES "Fx")`. `Pgrn` does not collide with the nine existing codes. |
| `plugins/peak-grain/src/PluginProcessor.{h,cpp}` | Modelled on peak-chorus (engine does the work) plus peak-reverb's mono-send and trails handling. |
| `tests/GrainStress.cpp` | → `ee_grain_stress`. |

### Edited

- `CMakeLists.txt:102` — add `peak-grain` to `EE_ALL_PLUGINS`.
- **`tests/UiSnapshot.cpp`** — the documented trap. Mirror the parameter layout
  and the `PedalSpec` or the snapshot silently drifts from the real plugin.
  Nothing catches this automatically.
- `tests/CMakeLists.txt`, `tests/DspTests.cpp` — new suite entries.
- `README.md` — user-facing entry for the pedal.
- `CLAUDE.md` — "Ten JUCE audio plugins" → eleven, plus the new test binary.
- `scripts/dev-check.sh` — run `ee_grain_stress` with the rest.

---

## 5. Engine design

**Record buffer.** Stereo circular, sized
`kMaxSpraySeconds + 2 × kMaxGrainSeconds + guard` ≈ 3.5 s. Allocated in
`prepare()`. Never in `processBlock`.

**Voice pool.** `std::array<Grain, kMaxGrains>`, start at 32. Each `Grain` is a
POD: read position (double), rate, samples remaining, total length, pan L/R
gains, window phase. No allocation, no locks, no `std::function`. Steal the
oldest voice when the pool is full.

**Scheduler.** A sample countdown. At zero: spawn one grain, then reload the
counter from `Density` with ±`kSpawnJitter` variation. Roll the start offset,
interval, direction and pan from a per-instance `juce::Random` seeded in
`prepare()` — not the global one.

**Interpolation.** 4-point Hermite, the same maths as
[ModDelayLine.h:44](../shared/include/ee/dsp/ModDelayLine.h). Decide once at
implementation time: lift that kernel into a small `ee/dsp/Hermite.h` shared by
both, or copy the four lines. Lifting is safe here because it is literally the
same expression — this is not the formatter situation `CLAUDE.md` warns about.

**Window.** Hann, from a 2048-point static table with linear interpolation.
Guarantees zero at both grain ends, so no clicks, ever, at any Size.

### Three things that will bite if not designed in from the start

1. **Forward grains must not overrun the write head.** A grain at rate 2.0
   consumes two samples of source per output sample; if its start offset is too
   small it catches up to the write pointer and reads unwritten garbage.
   Constrain the spawn offset to at least `grainLength × rate` plus a margin.
   This is *the* classic granular bug.
2. **Density is a volume knob unless compensated.** N overlapping Hann grains
   sum. Normalise by the expected overlap (`density × size`), roughly
   `1 / sqrt(overlap)`, clamped to a sane floor and ceiling.
3. **Pitch changes source consumption**, which is why the buffer sizing above
   carries the `2 ×` for the octave-up case.

### Stability

The grain path is strictly feed-forward — no feedback loop — so it structurally
cannot latch a NaN the way `FdnReverb` once did. `FdnReverb` itself already
hardens both ends: it sanitises its input (`FdnReverb.cpp:424`) and resets the
whole network on a non-finite output (`FdnReverb.cpp:562`). Nothing further is
needed, but `ee_grain_stress` should still sweep the knobs hunting non-finites,
the way `ee_reverb_stress` does.

---

## 6. UI

Seven knobs. `spec.knobsPerRow = 4`, `spec.width = knobRowWidth (4)` = 650 px:

```
row 1:  Size   Density   Spray   Pitch      <- the grain engine
row 2:  Verb   Decay     Mix                <- output stage
```

- Verify how the framework lays out a ragged final row (three in a four-wide
  grid) — I have not confirmed it centres it. If it does not, the fallback is
  `knobsPerRow = 4` with a spacer `KnobSpec` (empty `parameterID`) in the last
  cell, which the spec explicitly supports.
- `spec.knobDividerAfterColumn` can draw the line between the grain block and
  the output block if it reads better.
- Optionally give `verb` / `decay` a smaller `diameter` to mark them as a
  secondary section without dropping their captions.
- Theme: `PedalTheme::pink()`, which was unused. (`gold()` was the first pick
  and turned out to be a sage green too close to Peak Tape's.)
- No side panel. `ShimmerTunerPanel` is peak-reverb's and stays there.

---

## 7. The one thing we cannot match

The reference patch runs a 30-second reverb decay. `FdnReverb::kMaxDecay` is
**8.0 s**, and that constant is shared with peak-reverb — raising it would
silently change peak-reverb's knob range too.

Recommendation: ship with the 8 s ceiling. It is a long tail already, and the
grain cloud plus Spray adds its own smear on top. If 8 s genuinely is not
enough after listening, the clean fix is a separate `kMaxDecayLong` and an
explicit opt-in, not a change to the shared constant.

---

## 8. Build order

1. **`GrainerConfig.h` + `Grainer.h` + DSP tests first.** The engine is testable
   offline before any plugin exists: silence in → silence out, no non-finite
   output, output bounded across a full knob sweep, and grain count never
   exceeding the pool.
2. **Plugin skeleton**: seven parameters, `PedalSpec`, grain engine only —
   reverb not wired yet. Confirm the face lays out.
3. **Wire the reverb**: mono send, plain settings, trails on bypass.
4. `cmake --preset fast -DEE_PLUGINS="peak-grain"` and listen. Standalone is a
   real app; roughly a minute per iteration for a single pedal.
5. **Voice it by editing `GrainerConfig.h` only** — pan spread, reverse chance,
   interval weights, jitter, the two reverb constants.
6. Mirror into `tests/UiSnapshot.cpp`, add `ee_grain_stress`, run
   `scripts/dev-check.sh peak-grain`, update `README.md` and `CLAUDE.md`.

---

## 9. Where the build differed from the plan

**The tail estimate was wrong.** `Spray + Size` undercounts it. A backwards
grain starts one Spray back and then walks *further* back by as much source as
it spans, which at the top of the interval table is several times its own
length, and only then plays out. The bound is `Spray + Size x (maxRate + 1)`.
`testGrainerTailStops` caught this on the first run.

**`ee::dsp::config` is one flat namespace** shared by every engine, and Chorus
already owned `kDefaultMixPct`. Peak Grain's is `kDefaultGrainMixPct`. Worth
knowing before adding the next `*Config.h`.

**The ragged last row is centred** by `PedalEditor::Face::resized` already — the
spacer `KnobSpec` fallback was not needed.

Everything else landed as planned, including the two things flagged as likely to
bite: the spawn-offset constraint that keeps forward grains behind the write
head (`testGrainerReadsStayBehindTheWriteHead`, worst jump 4.9 % of peak) and
the overlap normalisation that stops Density doubling as a volume knob
(`testGrainerLevelHoldsAcrossDensity`, 0.2 dB across the useful range).
