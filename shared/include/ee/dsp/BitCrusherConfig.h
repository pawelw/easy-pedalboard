#pragma once

#include <algorithm>
#include <cmath>

/**
 * Voicing for ee::dsp::BitCrusher.
 *
 * The crusher is sample-and-hold downsampling and bit-depth quantisation with a
 * post low-pass, plus jitter on the hold clock. Modelled on the JHS 3 Series
 * Bit Crusher (Bits, Sample Rate, Filter) and Arturia's Bitcrusher (Jitter).
 *
 * Tuned to stay musical on guitar rather than to reach every extreme: the ranges
 * stop where a played note stops being a note, the downsampling is integer so
 * its artefacts stay harmonically related, and the engine runs a tracking
 * anti-alias filter so the lo-fi character is bandwidth loss, not ring-mod hash.
 *
 * Its own namespace (ee::dsp::bitcrush) rather than the shared ee::dsp::config,
 * because it reuses names like kControlBlock that other voicings also define and
 * every header lands in the same test translation unit.
 *
 * The knob->unit maps live here too, as inline helpers, so the pedal's printed
 * readouts and the engine itself read one set of numbers (the pattern RateMap.h
 * uses). Knob positions are 0..1.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::bitcrush
{

// ============================================================================
// BITS (the Bits knob)
// ============================================================================
// Word length the signal is quantised to. The range stops well short of the
// gimmick end: below ~4 bits a guitar note is gated fuzz, not a note. Knob
// down = kBitsClean (transparent for float audio), knob up = kBitsCrushed.
// kBitsSkew keeps the musical stretch - roughly 13..7 bit - across the middle
// of the travel rather than bunched at one end.
constexpr float kBitsClean   = 16.0f;
constexpr float kBitsCrushed = 4.0f;
constexpr float kBitsSkew    = 0.7f;

// ============================================================================
// RATE (the Rate knob)
// ============================================================================
// Sample-and-hold as an integer decimation factor N: hold N input samples, so
// the effective rate is always host / N. Integer N keeps the aliasing
// phase-locked and periodic instead of beating and drifting in pitch, and the
// engine's tracking pre-filter (kAntiAliasFrac) folds the images cleanly - so
// what is left reads as lo-fi bandwidth loss rather than ring modulation.
//
// N runs 1 (no decimation) to kMaxDecimation. At 48 kHz that bottoms out near
// 3 kHz - broken enough to be the effect, high enough that the fundamental of
// a played note survives.
constexpr int   kMinDecimation = 1;
constexpr int   kMaxDecimation = 16;
constexpr float kRateKnobSkew  = 0.5f;

// Corner of the anti-alias / reconstruction low-pass the engine runs around the
// sample-and-hold, as a fraction of the hold rate (host / N). At or below the
// Nyquist of the decimated stream, so the images fold cleanly; a hair under 0.5
// leaves the 2-pole rolloff a little room and a trace of "character".
constexpr float kAntiAliasFrac = 0.45f;

// ============================================================================
// FILTER (the Filter knob)
// ============================================================================
// A 2-pole (Butterworth Q) low-pass after the crush. Knob fully up = kLpMaxHz,
// which is at or above kLpBypassHz so the stage is skipped and the path stays
// bit-exact; knob down sweeps the corner to kLpMinHz. It rests engaged (see
// kDefaultLpPct): the fizz a crusher adds is mostly above the guitar, and a
// gentle roll-off there is what makes the effect sit in a mix.
constexpr float kLpMinHz    = 400.0f;
constexpr float kLpMaxHz    = 20000.0f;
constexpr float kLpKnobSkew = 0.4f;
constexpr float kLpBypassHz = 19000.0f;

// One-pole glide on the corner (and on the anti-alias corner) so turning the
// knob, or stepping N, does not zipper.
constexpr float kLpSmoothMs = 4.0f;

// ============================================================================
// JITTER (the Jitter knob)
// ============================================================================
// Perturbs the hold length by +/- (jitter * kJitterDepth * N) samples, rounded.
// Shallow on purpose - it is meant to add instability and warble, not noise.
// The randomness is instance-owned and re-seeded on reset(), so a render stays
// reproducible.
constexpr float kJitterDepth = 0.3f;

// Corner refresh cadence, in samples per channel.
constexpr int kControlBlock = 16;

// ============================================================================
// DEFAULTS (knob positions the engine opens on, 0..100)
// ============================================================================
constexpr float kDefaultBitsPct   = 35.0f;   // ~10 bit
constexpr float kDefaultRatePct   = 30.0f;   // hold ~5 (about 10 kHz at 48k)
constexpr float kDefaultLpPct     = 45.0f;   // ~7 kHz - engaged, tames the fizz
constexpr float kDefaultJitterPct = 0.0f;

// ============================================================================
// KNOB -> UNIT
// ============================================================================

/** Bits knob (0..1) -> word length in bits (kBitsClean..kBitsCrushed). */
inline float bitsFor (float knob01) noexcept
{
    const float t = std::pow (std::clamp (knob01, 0.0f, 1.0f), kBitsSkew);
    return kBitsClean + (kBitsCrushed - kBitsClean) * t;
}

/** Rate knob (0..1) -> integer decimation factor N (kMinDecimation..
    kMaxDecimation). Knob 0 -> 1, where the hold stage passes every sample. */
inline int decimationFactorFor (float knob01) noexcept
{
    const float t = std::pow (std::clamp (knob01, 0.0f, 1.0f), kRateKnobSkew);
    const float n = std::pow (static_cast<float> (kMaxDecimation), t);
    return std::clamp (static_cast<int> (std::lround (n)), kMinDecimation, kMaxDecimation);
}

/** The effective sample rate at knob position `knob01`, for the printed
    readout (host / N). */
inline float rateHzFor (float knob01, double sampleRate) noexcept
{
    const float sr = static_cast<float> (sampleRate > 0.0 ? sampleRate : 44100.0);
    return sr / static_cast<float> (decimationFactorFor (knob01));
}

/** Filter knob (0..1) -> low-pass corner in Hz. Knob 1 -> kLpMaxHz (bypassed),
    knob 0 -> kLpMinHz. */
inline float lpHzFor (float knob01) noexcept
{
    const float t = std::pow (std::clamp (knob01, 0.0f, 1.0f), kLpKnobSkew);
    return kLpMinHz * std::pow (kLpMaxHz / kLpMinHz, t);
}

} // namespace ee::dsp::bitcrush
