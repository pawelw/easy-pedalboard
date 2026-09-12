#pragma once

#include <algorithm>
#include <cmath>

/**
 * Voicing for ee::dsp::TubeDrive.
 *
 * A single-knob analog drive stage: an asymmetric rational soft-clipper (the
 * classic first-order approximation of a single-ended triode's grid curve -
 * one polarity compresses harder than the other, which is where a tube's even
 * harmonics come from) followed by a low-pass that darkens as Drive rises,
 * standing in for the Miller-effect rolloff a real tube stage picks up as it
 * is pushed harder. This is a shape of its own, not a tuning of
 * ee::dsp::Overdrive's diode-clipper WDF - the two should keep sounding
 * different.
 *
 * One knob only: Drive. Every stage - gain, asymmetry and the warmth low-pass -
 * is driven off the same 0..1 position, scaled here, so knob-down is an exact
 * pass-through (see the class note on the bypass fast path) and knob-up is the
 * full character.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::tubedrive
{

// ============================================================================
// DRIVE (the Drive knob)
// ============================================================================
// Gain ahead of the shaper. Knob down = unity (so the shaper below sees a
// small signal and folds back to identity - see kDefaultDrive01 and the
// bypass fast path), knob up = kDriveMaxGain, swept exponentially so the low
// end - "just waking the tube up" - gets most of the travel.
constexpr float kDriveMinGain = 1.0f;
constexpr float kDriveMaxGain = 14.0f;
constexpr float kDriveSkew = 0.6f;

// Knob position the pedal opens on (0..100, like the other Artifact knobs).
// Enough grit to be heard, well short of slammed.
constexpr float kDefaultDrivePct = 35.0f;

// ============================================================================
// ASYMMETRIC SHAPER
// ============================================================================
// y = x >= 0 ? x / (1 + satPos * x) : x / (1 - satNeg * x), a rational soft
// clip (softer than tanh in the knee, harder-saturating near its asymptote)
// with satPos < satNeg - the negative-going half compresses harder, the
// asymmetry a triode's grid curve has and the even-harmonic source of "tube"
// warmth. Both coefficients scale with Drive (see kSatSkew) so knob-down has
// no asymmetry at all and the shaper is the identity function.
constexpr float kPosSatMax = 0.55f;
constexpr float kNegSatMax = 1.05f;
constexpr float kSatSkew = 0.7f;

// ============================================================================
// WARMTH LOW-PASS
// ============================================================================
// One-pole low-pass after the shaper, corner sweeping down as Drive rises -
// the darkening a tube stage's Miller capacitance adds under heavier drive.
// Knob down sits at/above kWarmthBypassHz (skipped, bit-exact); knob up pulls
// the corner down to kWarmthMinHz.
constexpr float kWarmthMinHz = 3200.0f;
constexpr float kWarmthMaxHz = 20000.0f;
constexpr float kWarmthSkew = 0.5f;
constexpr float kWarmthBypassHz = 19500.0f;

// ============================================================================
// OUTPUT MAKE-UP
// ============================================================================
// The shaper's own asymptote is the real source of level here, not just a
// gain to lean back up: measured at kMakeupRefAmplitude, the positive-side
// curve alone already puts out +9 dB at Drive = 100 % with no makeup applied
// at all (gain = 14 into a 1 / kPosSatMax = 1.8x ceiling). So makeup is
// solved backwards from a target instead of forwards from the gain: at each
// Drive position, evaluate what the shaper alone does to a signal peaking at
// kMakeupRefAmplitude (the analytic positive-side formula, since that is the
// louder-going half - see TubeDrive::updateDrive), then scale by whatever
// makes that land exactly on kMaxMakeupDb * drive01. A real signal's own
// crest factor still moves the result off this line a little either way - a
// quieter pick lands louder than kMaxMakeupDb, a hot one lands under it - the
// same compression character a real driven stage has - but the reference
// level itself is exact.
constexpr float kMakeupRefAmplitude = 0.5f;
constexpr float kMaxMakeupDb = 4.0f;

// DC blocker on the output - the asymmetric shaper's only source of offset.
// Below the lowest note, above nothing musical.
constexpr float kDcBlockerHz = 15.0f;

// ============================================================================
// OVERSAMPLING
// ============================================================================
// The shaper's knee is run at 2x the host rate and band-limited back down, the
// same reason ee::dsp::Overdrive does - a folded harmonic in the top octave is
// the one thing that reads as digital rather than as a driven tube.
constexpr int kOversampleFactor = 2;
constexpr float kOversampleCutoffHz = 19000.0f;

// ============================================================================
// KNOB -> UNIT
// ============================================================================

/** Drive knob (0..1) -> gain ahead of the shaper. */
inline float gainFor (float drive01) noexcept
{
    const float t = std::pow (std::clamp (drive01, 0.0f, 1.0f), kDriveSkew);
    return kDriveMinGain * std::pow (kDriveMaxGain / kDriveMinGain, t);
}

} // namespace ee::dsp::tubedrive
