#pragma once

/**
 * Voicing for ee::dsp::Overdrive.
 *
 * A single gain stage into a soft asymmetric clipper, wrapped in the filtering
 * that turns "a diode" into "a pedal": a high-pass ahead of the clip so the low
 * strings do not turn to mush, a tilt tone control after it, and a fixed
 * low-pass to keep the top end from fizzing.
 *
 * The three knobs (Drive, Tone, Level) only ever scale what is set up here.
 * Level lives in the processor; Drive and Tone map 1:1 to the constants below.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::config
{

// ============================================================================
// DRIVE
// ============================================================================
// Gain into the clipper, swept exponentially by the Drive knob so the low,
// "just breaking up" end gets most of the travel.
//   kDriveMinGain : Drive = 0 %.   ~+8 dB - always a little hair, never fully clean.
//   kDriveMaxGain : Drive = 100 %. Everything slams the rails: full sustain, compressed.
constexpr float kDriveMinGain = 2.5f;
constexpr float kDriveMaxGain = 260.0f;

// Knob position the pedal opens on. 0.35 is a mild, dynamic overdrive.
constexpr float kDefaultDrive01 = 0.35f;

// ============================================================================
// CLIPPER
// ============================================================================
// A DC bias folded into the shaper before tanh. Non-zero makes the curve clip
// one half of the wave harder than the other, which is what puts even harmonics
// in and separates an "overdrive" from a clean fuzz. Kept small - larger values
// just sound like a fault.
constexpr float kClipBias = 0.18f;

// How much of the sub-bass that the pre-clip high-pass removed is folded back in
// after the clipper, so the note keeps its body without the clip stage ever
// having to chew on it. 0 = surgical / mid-forward, 1 = full range into the diode.
constexpr float kLowKeep = 0.28f;

// ============================================================================
// FILTERS
// ============================================================================
// One-pole high-pass ahead of the clipper. This is the Tube-Screamer trick: hold
// the bottom octave out of the distortion so chords stay defined.
constexpr float kPreClipHighpassHz = 190.0f;

// Pivot of the tilt tone control. Content below this is the "low" band, content
// above it the "high" band; the Tone knob crossfades their weights.
constexpr float kToneTiltPivotHz = 640.0f;

// Tilt band gains at the two ends of the Tone knob. Dark (Tone = 0) lifts the
// lows and pulls the highs down; Bright (Tone = 100) does the opposite. The
// midpoint is close to flat.
constexpr float kToneLowGainDark   = 1.45f;
constexpr float kToneLowGainBright = 0.55f;
constexpr float kToneHighGainDark   = 0.30f;
constexpr float kToneHighGainBright = 1.70f;

// Knob position the pedal opens on. 0.5 is the near-flat midpoint.
constexpr float kDefaultTone01 = 0.5f;

// Fixed low-pass after the tone stage - tames the buzz the hard corners of the
// clipper throw off (this engine does not oversample, see Overdrive.h).
constexpr float kPostLowpassHz = 11000.0f;

// DC blocker on the output, mopping up the offset the asymmetric clip leaves.
// Below the lowest note, above nothing musical.
constexpr float kDcBlockerHz = 18.0f;

// ============================================================================
// OUTPUT
// ============================================================================
// Rough loudness compensation applied inside the engine so that winding Drive up
// does not just make the pedal louder. Applied as kOutputTrim / driveGain^kMakeupExponent.
// The exponent is deliberately small: tanh already pins its output near +/-1 once
// it is driven hard, so only a gentle taper is needed to keep the perceived
// level roughly steady across the knob - a big exponent would crush the clipped
// signal to nothing and let the folded-back lows take over. The Level knob trims
// whatever is left.
constexpr float kOutputTrim      = 0.72f;
constexpr float kMakeupExponent  = 0.15f;

} // namespace ee::dsp::config
