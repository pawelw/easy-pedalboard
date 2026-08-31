#pragma once

/**
 * Voicing for ee::dsp::Overdrive.
 *
 * The clipping stage is a real diode-clipper circuit solved with a Wave Digital
 * Filter (chowdsp_wdf): a driven voltage source through a series resistor into a
 * capacitor and an anti-parallel silicon diode pair to ground. That is the same
 * skeleton as the clipping amp in a Boss SD-1 / Tube Screamer - the diode I-V
 * curve gives the soft knee, and the capacitor across the diodes rolls the fizz
 * off the distortion the way the feedback cap does in the real pedal.
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
// Gain into the clipper (stands in for the op-amp's non-inverting gain),
// swept exponentially by the Drive knob so the low, "just breaking up" end gets
// most of the travel.
//   kDriveMinGain : Drive = 0 %.   Barely tickling the diodes - a little hair.
//   kDriveMaxGain : Drive = 100 %. Everything slams the diode clamp: full sustain.
constexpr float kDriveMinGain = 1.4f;
constexpr float kDriveMaxGain = 300.0f;

// Knob position the pedal opens on. 0.35 is a mild, dynamic overdrive.
constexpr float kDefaultDrive01 = 0.35f;

// Asymmetry shaped into the drive signal just before the (symmetric) diode
// pair: y - kClipAsym * y * |y|. It squashes one half of the wave a hair more
// than the other - the even-harmonic warmth that separates an SD-1 from a
// dead-symmetric Tube Screamer - while staying exactly zero at zero, so a
// silent input stays silent with no DC to chase. Keep it small.
constexpr float kClipAsym = 0.16f;

// ============================================================================
// CLIPPER CIRCUIT (Wave Digital Filter)
// ============================================================================
// Series resistor between the driven source and the diode/cap node, in ohms.
// With kClipCapF it sets the in-loop roll-off: fc = 1 / (2*pi*R*C).
constexpr float kClipSeriesR = 2200.0f;

// Capacitor across the diodes, in farads. 2.2k + 10 nF -> ~7.2 kHz: highs in
// the distortion are shelved off so the clip reads as "overdrive", not "fuzz".
constexpr float kClipCapF = 10.0e-9f;

// Silicon small-signal diode (1N914 / 1N4148 class). Is = reverse saturation
// current, Vt = thermal voltage, nDiodes = series count per leg.
constexpr float kDiodeIs = 2.52e-9f;
constexpr float kDiodeVt = 25.85e-3f;
constexpr float kDiodeCount = 1.0f;

// ============================================================================
// OVERSAMPLING
// ============================================================================
// The diode clipper is run at 2x the host rate and band-limited back down, so
// the hard part of the curve does not fold aliases into the top octave. The
// in-loop cap already does most of the work; 2x mops up the rest.
constexpr int kOversampleFactor = 2;

// Anti-imaging / anti-aliasing filter corner, in Hz. Below every supported
// host Nyquist (>= 22.05 kHz) with room for the transition band.
constexpr float kOversampleCutoffHz = 19000.0f;

// ============================================================================
// OUTPUT MAKE-UP
// ============================================================================
// The clipper's output rides near the diode clamp voltage, which is quiet and
// only weakly dependent on Drive. This lifts it back to a usable level, tilting
// up a little as Drive rises so the knob keeps a roughly steady loudness rather
// than getting quieter as it saturates. makeup = kMakeupLow * (driveGain / kDriveMinGain)^kMakeupSlope.
constexpr float kMakeupLow   = 0.85f;
constexpr float kMakeupSlope = 0.05f;

// ============================================================================
// FILTERS
// ============================================================================
// One-pole high-pass ahead of the clipper. Holds the bottom octave out of the
// distortion so chords stay defined - the Tube-Screamer "flub filter".
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

// Fixed low-pass after the tone stage - a last touch of smoothing on the top.
constexpr float kPostLowpassHz = 12000.0f;

// DC blocker on the output. Below the lowest note, above nothing musical.
constexpr float kDcBlockerHz = 18.0f;

} // namespace ee::dsp::config
