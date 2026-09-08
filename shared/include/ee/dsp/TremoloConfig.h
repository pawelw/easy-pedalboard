#pragma once

/**
 * Voicing for ee::dsp::Tremolo.
 *
 * The engine's four controls - Amount, Rate, Shape and Bias - only ever scale
 * what this file sets up. Everything that gives the bias-tube stage its
 * character, and the two time constants that keep the modulation from stepping,
 * are fixed here.
 *
 * Its own namespace (ee::dsp::tremolo) rather than the shared ee::dsp::config,
 * for the reason PhaserConfig.h gives: several engines' voicing headers land in
 * the same test translation unit and would collide on names this general.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::tremolo
{

// ============================================================================
// BIAS-TUBE STAGE
// ============================================================================
// An opto/photocell tremolo just fades the level with a smooth LFO; a
// brownface-style bias tremolo modulates a power tube's bias, which does two
// audible things the clean fade does not:
//
//   1. the ducking envelope stops being a mirror of the LFO - the tube snaps
//      toward cutoff and lingers there, so the throb reads as a harder,
//      flatter-bottomed pulse (kDuckSkew bends the duck curve for this);
//   2. as the operating point nears cutoff the signal grinds - an asymmetric,
//      level-dependent distortion that swells and clears in time with the
//      throb, clean on the loud peaks and dirtiest at the bottom of the dip.
//
// Modelled the same way as ee::dsp::TapeCharacter's stage: a hand-rolled tanh
// with a one-sided bias, no oversampling (the drive is program-dependent and
// mostly gentle), and a one-pole DC blocker to mop up the offset the moving
// bias leaves. The Bias knob crossfades the whole thing in; at 0 the clean opto
// law is untouched.
constexpr float kDuckSkew = 0.6f;  // how far the duck curve bends toward a hard pulse
constexpr float kDrive    = 10.0f; // peak extra drive into the tanh at the bottom of the dip
constexpr float kAsym     = 0.7f;  // one-sided bias offset - the pulsing even harmonics
constexpr float kTrim     = 5.0f;  // output trim that tracks the drive, leaving a little sag
constexpr float kDcHz     = 20.0f; // DC-blocker corner: below the lowest note, above the LFO's pump

// ============================================================================
// SMOOTHING
// ============================================================================
// One-pole slew on the modulation signal, so a phase snap or a division/mode
// switch can never step the gain in a single sample. Short enough not to round
// the square shape off audibly.
constexpr float kModSlewSeconds = 0.0025f;

// How long the three control smoothers take to reach a new value. Equal to
// ee::plugin::kRampSeconds, and restated here rather than included: nothing
// else under ee/dsp reaches up into ee/plugin, and an engine that did would
// stop being usable without the plugin layer. PeakTremPanProcessor sees both
// and static_asserts they are the same, so the two cannot drift in silence.
constexpr float kSmoothingSeconds = 0.02f;

// How hard the phase is pulled back onto the host grid, per block, while
// synced and playing. A fraction of the error rather than the whole of it, and
// capped, so host ppq jitter and division changes stay click-free and just
// re-settle over a fraction of a second. A transport jump larger than
// kJumpPpq snaps instead - see ee::dsp::Tremolo.
constexpr double kPhasePullFraction = 0.15;
constexpr double kPhasePullMax      = 0.006;
constexpr double kJumpPpq           = 0.25;

} // namespace ee::dsp::tremolo
