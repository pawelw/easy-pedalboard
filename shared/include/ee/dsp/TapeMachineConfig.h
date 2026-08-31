#pragma once

/**
 * Voicing for ee::dsp::TapeMachine.
 *
 * Peak Tape has six knobs - Saturation, Wear, Flutter, Tone, Stereo, Noise -
 * and each one only ever scales what this file sets up. Everything that makes
 * the machine the machine (how fast and how deep the capstan wanders, where the
 * record EQ pivots, how loud the tape floor is) lives here, so the whole
 * voicing can be read at a glance and retuned without touching the engine.
 *
 * Wear is the exception: it drives ee::dsp::TapeCharacter, the same stage Peak
 * Delay's Tape knob drives, so its voicing lives in ee/dsp/TapeTuning.h and the
 * two pedals stay in step by construction.
 *
 * Its own namespace (ee::dsp::tape) rather than the shared ee::dsp::config,
 * because it reuses names like kControlBlock / kDcBlockerHz that other voicings
 * also define and every header lands in the same test translation unit.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::tape {

// One-pole smoothing on every knob value, in Hz. Low enough that a mouse drag
// never zippers, high enough that a knob still feels immediate.
constexpr float kParamSmoothingHz = 12.0f;

// ============================================================================
// TRANSPORT (Flutter and Stereo)
// ============================================================================
// The whole signal is read off a delay line whose length wanders, which is what
// a real transport does to pitch.
//
// The depth and rate are measured off a reference recording of the effect at
// full tilt, by tracking the delay between it and the dry take: a near-pure
// 2 Hz wobble, ~70 samples (1.6 ms) of excursion at 44.1 kHz, which works out
// at roughly 2 % speed error - about 35 cents.
//
// Pure is the point. A real machine has filtered noise riding the wobble too -
// the rollers, and the tape scraping past the heads - and an earlier voicing
// had both. They roughen the vibe rather than adding to it, so the transport
// here is the sine and nothing else, with only a trace of rate wander to keep
// it off a perfect grid.
//
// Voicing it again: render the dry take through ee_tape_render with Flutter at
// 100 and everything else at 0, then track the two files against each other -
// the 2 Hz line should land on the reference's.
//
// The nominal delay has to clear the deepest excursion the knobs can ask for -
// Flutter and Stereo together - with room to spare, and it is the whole of the
// reported latency, so it is as short as that allows.
constexpr float kNominalDelayMs = 4.5f;

constexpr float kWowRateHz = 2.0f;
constexpr float kWowDepthMs =
    1.62f; // peak excursion at Flutter 100 %, matched to the reference
constexpr float kWowRateJitter =
    0.02f; // the reference wobble is near enough metronomic
constexpr float kWowJitterHz = 0.15f; // how fast that wander itself moves

// Hard ceiling on the excursion, as a fraction of the nominal delay. The read
// can never walk off either end of the line whatever the knobs say.
constexpr float kWobbleLimit = 0.9f;

// ============================================================================
// STEREO
// ============================================================================
// A second, slower modulation that the two channels read at different points of
// its cycle: the sides pull apart in pitch and the image opens out, the way a
// chorus widens without a wet/dry comb.
//
// kStereoPhaseSpanCycles stays clear of 0.5 on purpose. At exactly antiphase
// the right channel is a mirror of the left, and the image collapses to mono
// every time the LFO crosses zero; ~0.33 cycles is the widest offset that stays
// decorrelated right through the cycle. (Peak Chorus's Phase knob is capped for
// the same reason.)
constexpr float kStereoRateHz = 0.33f;
constexpr float kStereoDepthMs = 1.5f; // per channel, at Stereo 100 %
constexpr float kStereoPhaseSpanCycles = 0.33f;

// ============================================================================
// SATURATION
// ============================================================================
// The record head. Drive into an asymmetric tanh and a matched make-up, so the
// knob adds harmonics and squash rather than level.
//
// The shaper runs 2x oversampled, wrapped in a record/replay EQ pair: a shelf
// lifts the top end going in and its exact inverse puts it back coming out.
// That is what a real machine does with its record EQ, and it is why tape
// distorts the highs first - the treble arrives at the head hotter than the
// bass does.
constexpr float kSatMinDrive = 1.0f;
constexpr float kSatMaxDrive = 7.0f;

// Bias asymmetry at Saturation 100 %. This is where the even harmonics - the
// "warm" ones - come from. Subtracting tanh(bias) keeps the curve through the
// origin so quiet passages stay quiet.
constexpr float kSatBias = 0.16f;

// Record EQ: the shelf pivot, and its high-frequency gain at Saturation 100 %.
constexpr float kEmphasisPivotHz = 2000.0f;
constexpr float kEmphasisGain = 3.0f;

// The make-up is matched at a reference level rather than at the origin.
// Normalising by the small-signal slope alone would leave the stage quieter the
// harder it is driven, which reads as a volume control in disguise; matched at
// kSatMakeupLevel instead, quiet passages come up a little and loud ones are
// held - which is what tape actually does. kSatMakeupTrim is the trim on top:
// 1.0 is level-matched at that reference, above it the knob pushes.
constexpr float kSatMakeupLevel = 0.14f; // ~ -17 dBFS, a normal programme level
constexpr float kSatMakeupTrim = 1.0f;

constexpr int kOversampleFactor = 2;
constexpr float kOversampleCutoffHz = 19000.0f;

// ============================================================================
// TONE
// ============================================================================
// A tilt around the pivot, on a knob that rests in the middle: turning left
// leans the balance into the low band, right into the high one. Dead centre is
// flat and the stage is bypassed exactly - a hair either side of it is not, so
// there is no dead band around the middle to hunt through. The knob snaps onto
// the centre instead, so landing on flat is a flick rather than a nudge.
constexpr float kTonePivotHz = 700.0f;

// Symmetric on purpose: a centre-detented knob should lean as far one way as
// the other, so each band's two gains are the same factor above and below unity
// (+/- 5.6 dB, an 11 dB tilt end to end).
constexpr float kToneLowGainDark = 1.90f;
constexpr float kToneLowGainBright = 0.52f;
constexpr float kToneHighGainDark = 0.52f;
constexpr float kToneHighGainBright = 1.90f;

// ============================================================================
// NOISE
// ============================================================================
// The tape floor is a recording of one, played in a constant loop: it is there
// whether anything is playing or not, exactly like a machine with the transport
// running. It is not gated and it does not ride the programme - a floor that
// ducks when you play is a noise gate, not a tape.
//
// The knob is a straight linear gain on it, up to this ceiling: 1.0 is the
// recording at the level it was made, and anything below that is the whole
// knob range scaled down. Turn it down to make a fully open Noise knob quieter
// without re-recording the file.
//
//   1.0   = the recording as it is           (0 dB)
//   0.5   = half                            (-6 dB)
//   0.25  = a quarter                      (-12 dB)
//   0.125 = an eighth                      (-18 dB)
constexpr float kMaxFloorGain = 0.4f;

// The loop is seamed with a crossfade this long, so the wrap cannot tick. Long
// enough to hide the join in a hiss recording, short enough to leave most of
// the file playing untouched.
constexpr float kNoiseLoopFadeMs = 250.0f;

// Fallback voicing, for when no recording has been handed to the engine (the
// offline tests, and the render tool without a sample argument): band-limited
// white noise at a comparable level, so the stage is still exercised.
constexpr float kHissHighpassHz = 700.0f;
constexpr float kHissLowpassHz = 11000.0f;
constexpr float kMaxHiss =
    0.02f; // ~ -45 dBFS RMS at Noise 100 %, after the band limiting

// ============================================================================
// OUTPUT
// ============================================================================
// Only ever engaged with the saturation, which is the one stage that can leave
// an offset behind (its bias asymmetry is the whole point).
constexpr float kDcBlockerHz = 12.0f;

// ============================================================================
// DEFAULTS (knob positions the pedal opens on)
// ============================================================================
constexpr float kDefaultSaturationPct = 35.0f;
constexpr float kDefaultWearPct = 30.0f;
constexpr float kDefaultFlutterPct = 25.0f;
constexpr float kDefaultTonePct = 0.0f; // centred: flat
constexpr bool kDefaultStereoOn =
    true; // the machine is a stereo one unless asked otherwise
constexpr float kDefaultNoisePct = 50.0f;

} // namespace ee::dsp::tape
