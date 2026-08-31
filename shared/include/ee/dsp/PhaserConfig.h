#pragma once

/**
 * Voicing for ee::dsp::Phaser.
 *
 * Peak Phase is a two-knob pedal - Rate and Depth - so everything that gives it
 * its character (stage count, sweep range, feedback, stereo spread, wet/dry
 * balance) is fixed here. The two knobs only ever scale what this file sets up.
 *
 * Its own namespace (ee::dsp::phaser) rather than the shared ee::dsp::config,
 * because it reuses names like kRateMinHz that the chorus voicing also defines
 * and both headers land in the same test translation unit.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::phaser
{

// ============================================================================
// STAGES
// ============================================================================
// Number of first-order all-pass sections in the cascade. Each pair of stages
// puts one notch in the summed (dry + wet) response, so 6 stages = 3 sweeping
// notches - the classic thick "script-logo" phaser. 4 is thinner and more
// vintage; 8 gets close to a vocal formant sweep. kMaxStages only sizes the
// state arrays.
constexpr int kMaxStages = 8;
constexpr int kStages    = 6;

// ============================================================================
// RATE
// ============================================================================
// LFO frequency range of the Rate knob, in Hz, skewed so the slow, musical end
// gets most of the knob travel. Matches Peak Chorus so the two pedals feel the
// same under the hand.
constexpr float kRateMinHz        = 0.03f;
constexpr float kRateMaxHz        = 8.0f;
constexpr float kRateSkewCentreHz = 0.7f;

// ============================================================================
// SWEEP
// ============================================================================
// The all-pass corner frequency rides the LFO between these two bounds (log
// spaced) at Depth = 100 %. Lower Depth shrinks the sweep symmetrically around
// its geometric centre, so even at Depth 0 the notches sit mid-spectrum and
// colour the tone rather than vanishing.
//   ~250-1800 Hz = broad, vocal sweep                        <-- default
//   tighten toward 400-1200 Hz for a subtler shimmer
constexpr float kSweepMinHz = 250.0f;
constexpr float kSweepMaxHz = 1800.0f;

// ============================================================================
// FEEDBACK
// ============================================================================
// Last stage fed back into the input. This is the "resonance" that sharpens the
// notches into peaks and gives the sweep its liquid, whistling edge. Stays well
// below 1 for stability; 0.5-0.7 is the sweet spot.
constexpr float kFeedback = 0.62f;

// ============================================================================
// STEREO
// ============================================================================
// The right channel's LFO is offset from the left's by this fraction of a
// cycle, so the notches sweep out of step across the image and it opens up.
// Kept clear of 0.5 (antiphase) for the same reason as the chorus.
constexpr float kStereoOffsetCycles = 0.25f;

// ============================================================================
// MIX
// ============================================================================
// Wet/dry balance inside the engine. A phaser gets its notches from summing the
// all-pass output with the dry signal, so 0.5 is the textbook setting - it is
// where the nulls are deepest. Nudge down for a gentler effect.
constexpr float kWetMix = 0.5f;

// One-pole high-pass on the wet path only, to keep the feedback from pumping
// the low end as the sweep passes through the bass.
constexpr float kWetHighPassHz = 90.0f;

// ============================================================================
// DEFAULTS (knob positions the pedal opens on)
// ============================================================================
constexpr float kDefaultRateHz  = 0.35f;
constexpr float kDefaultDepthPct = 75.0f;

} // namespace ee::dsp::phaser
