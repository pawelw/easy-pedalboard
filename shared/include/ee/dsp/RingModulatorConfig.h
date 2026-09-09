#pragma once

#include <algorithm>
#include <cmath>

/**
 * Voicing for ee::dsp::RingModulator.
 *
 * The ring modulator multiplies the input by a sine carrier (classic DSB-SC).
 * Modelled on the JHS 3 Series Ring Modulator - Blend, Frequency, Tweak and a
 * Mode toggle - with a post low-pass added to tame the metallic top:
 *
 *   - Earworm (Mode 0), after the Way Huge Ringworm: a straight carrier
 *     multiply, with Tweak running a slow LFO on the carrier frequency so a
 *     fixed tone comes alive and wobbles.
 *   - Green Lantern (Mode 1), after the Green Ringer: a full-wave rectifier
 *     octave-up crossfaded in by Tweak, on top of the same carrier multiply.
 *
 * Its own namespace (ee::dsp::ringmod) rather than the shared ee::dsp::config,
 * because it reuses names like kControlBlock that other voicings also define and
 * every header lands in the same test translation unit.
 *
 * The knob->unit maps live here too, as inline helpers, so the pedal's printed
 * readouts and the engine itself read one set of numbers (the pattern
 * BitCrusherConfig.h / RateMap.h use). Knob positions are 0..1.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::ringmod
{

// ============================================================================
// FREQUENCY (the Freq knob)
// ============================================================================
// Carrier frequency, log spaced. Knob down = kFreqMinHz, a few Hz - a slow
// tremolo-like pulse; knob up = kFreqMaxHz, well into the bell-like metallic
// range. kFreqKnobSkew < 1 keeps the low, musical end spread across the first
// half of the travel rather than bunched at the bottom.
constexpr float kFreqMinHz   = 2.0f;
constexpr float kFreqMaxHz   = 3000.0f;
constexpr float kFreqKnobSkew = 0.6f;

// ============================================================================
// TWEAK (the Tweak knob)
// ============================================================================
// Earworm: a fixed-rate sine LFO on the carrier frequency. Tweak scales its
// depth from 0 to +/- kTweakDepth of the carrier frequency, so at full Tweak a
// 200 Hz carrier swings between 200*(1-kTweakDepth) and 200*(1+kTweakDepth).
constexpr float kTweakLfoHz = 4.0f;
constexpr float kTweakDepth = 0.6f;

// Green Lantern: Tweak crossfades from the plain ring mod (0) to a full-wave
// rectified octave-up (1). The rectified signal keeps roughly the input's RMS
// but the crossfade still reads as a small dip without a touch of make-up; a DC
// blocker removes the rectifier's offset.
constexpr float kOctaveMakeupGain = 1.4f;
constexpr float kDcBlockR         = 0.999f;

// ============================================================================
// FILTER (the Filter knob)
// ============================================================================
// A 2-pole (Butterworth Q) low-pass on the engine output. Knob fully up =
// kLpMaxHz, at or above kLpBypassHz so the stage is skipped and the ring mod
// runs unfiltered; knob down sweeps the corner to kLpMinHz. It rests engaged
// (see kDefaultLpPct): a ring modulator throws a lot of energy above the guitar
// and a gentle roll-off there is what makes it sit in a mix.
constexpr float kLpMinHz    = 300.0f;
constexpr float kLpMaxHz    = 20000.0f;
constexpr float kLpKnobSkew = 0.4f;
constexpr float kLpBypassHz = 19000.0f;

// One-pole glide on the carrier frequency and the low-pass corner so turning a
// knob does not zipper.
constexpr float kSmoothMs = 4.0f;

// Control-rate refresh cadence, in samples per channel.
constexpr int kControlBlock = 16;

// ============================================================================
// DEFAULTS (knob positions the engine opens on, 0..100)
// ============================================================================
constexpr float kDefaultFreqPct  = 40.0f;  // ~125 Hz - a clear ring, not a pulse
constexpr float kDefaultTweakPct = 0.0f;
constexpr float kDefaultLpPct    = 60.0f;  // ~10 kHz - engaged, tames the fizz

// Mode 0 = Earworm, Mode 1 = Green Lantern.
constexpr int kModeEarworm     = 0;
constexpr int kModeGreenLantern = 1;

// ============================================================================
// KNOB -> UNIT
// ============================================================================

/** Freq knob (0..1) -> carrier frequency in Hz (kFreqMinHz..kFreqMaxHz). */
inline float freqHzFor (float knob01) noexcept
{
    const float t = std::pow (std::clamp (knob01, 0.0f, 1.0f), kFreqKnobSkew);
    return kFreqMinHz * std::pow (kFreqMaxHz / kFreqMinHz, t);
}

/** Filter knob (0..1) -> low-pass corner in Hz. Knob 1 -> kLpMaxHz (bypassed),
    knob 0 -> kLpMinHz. */
inline float lpHzFor (float knob01) noexcept
{
    const float t = std::pow (std::clamp (knob01, 0.0f, 1.0f), kLpKnobSkew);
    return kLpMinHz * std::pow (kLpMaxHz / kLpMinHz, t);
}

} // namespace ee::dsp::ringmod
