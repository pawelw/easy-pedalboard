#pragma once

#include <algorithm>
#include <cmath>

/**
 * Voicing for ee::dsp::Rust.
 *
 * Rust is degradation with a memory. A per-channel "wear" state tracks the
 * recent input level - it climbs while you play and heals back toward zero when
 * you stop - and drives a corrosion chain (tape-style warble, tanh grit, and
 * bit/rate crumble) whose depth is that wear times Grind. So the corrosion
 * follows how hard you are playing, and it always shapes the signal - nothing is
 * layered on top, so the output falls silent the instant the input does.
 *
 * Two user knobs, Grind and Tone. Wear and its recovery time are fixed at the
 * top of their range - the corrosion is always allowed to reach full and heals
 * quickly - so there is nothing to dial there.
 *
 * Two voicings picked by Mode:
 *   - Oxide (Mode 0), a decaying magnetic coating: full pitch/time warble, and
 *     wear darkens the post low-pass. Soft, wobbly.
 *   - Contact (Mode 1), a failing jack: much less warble, no wear darkening, but
 *     wear pulls a hard-clip threshold down and deepens the sample-rate crumble.
 *     Harder and much brighter, so the Tone knob is scaled darker in this mode
 *     (see kContactToneScale) - the same knob position lands lower.
 *
 * Its own namespace (ee::dsp::rust), like ee::dsp::ringmod / ee::dsp::bitcrush,
 * because it reuses names (kControlBlock, kSmoothMs) other voicings also define
 * and every header lands in the same test translation unit.
 *
 * The Tone knob->Hz map lives here as an inline helper so the pedal's printed
 * readout and the engine read one set of numbers. Knob positions are 0..1.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::rust
{

// ============================================================================
// WEAR ENVELOPE (no knob - fixed at full)
// ============================================================================
// The wear detector is an attack/release envelope follower on |x| - fast up,
// slow down - so it reads "a note is sounding" and holds through the troughs of
// the waveform rather than tracking the instantaneous rectified sample (which,
// on a low guitar note, sits near zero for much of every cycle). Below
// kWearFloor nothing accumulates; by kWearKnee the corrosion is at full; a
// smoothstep between the two, so quiet playing corrodes less than hard playing.
// The knee is set for a guitar's short-term level, not a normalised sine.
constexpr float kEnvAttackMs  = 10.0f;
constexpr float kEnvReleaseMs = 300.0f;
constexpr float kWearFloor    = 0.006f;
constexpr float kWearKnee     = 0.10f;

// How fast wear itself rises toward that target and heals back down. Rise is a
// few hundred ms (the note "rusts" as it rings); heal is ~1.5 s. Both only
// shape how the sound evolves across a phrase - it is silent between phrases.
constexpr float kWearAttackMs = 400.0f;
constexpr float kWearHealSec  = 1.5f;

// ============================================================================
// GRIND (the Grind knob) - the character of corrosion at full wear
// ============================================================================
// Scales the destructive stages together: 0 is gentle grime (a little
// saturation), 1 is full breakup (hard quantise, sample-rate crumble). Wear *
// Grind is the amount actually applied, so a stage only bites once you lean in.
constexpr float kGritDrive     = 7.0f;    // extra tanh drive at Wear*Grind = 1
// The tanh stage's saturation raises RMS (harmonics filling in a squarer wave)
// faster than a trim scaling linearly with Wear*Grind pulls it back down, so
// the loudest point isn't at Wear*Grind = 1 - it's around Grind 50%, where
// Rust measured a couple of dB louder than Bit Crush or Ring Mod at matched
// settings. 0.80 (was 0.55) was picked by sweeping Grind 0..1 against those
// two siblings and landing the worst case under +1 dB rather than +2.25 dB.
constexpr float kGritTrim      = 0.80f;   // level pulled back by this * Wear*Grind
constexpr float kMinBits       = 5.0f;    // word length at Wear*Grind = 1
constexpr int   kMaxDecimate   = 12;      // sample-and-hold length at Wear*Grind = 1

// How much of the warble each voicing uses. Oxide is wobbly; Contact only
// flickers.
constexpr float kWarbleWeightOxide   = 0.60f;
constexpr float kWarbleWeightContact = 0.15f;

// ============================================================================
// WARBLE - a short modulated delay, tape-style pitch/time instability
// ============================================================================
// A steady sine plus a slow random walk drive the read position; depth scales
// with wear and the mode weight above. The blended copy is the pre-corrosion
// input, so this reads as wow/flutter under the grime rather than as an echo.
constexpr float kWarbleBaseMs  = 6.0f;
constexpr float kWarbleDepthMs = 5.0f;
constexpr float kWarbleRateHz  = 0.7f;
constexpr float kWarbleWalk    = 0.015f;  // random-walk step per control block

// Delay line length in ms; the buffer is sized for this at up to 192 kHz.
constexpr float kWarbleLineMs  = 24.0f;

// ============================================================================
// OXIDE darkening
// ============================================================================
constexpr float kOxideDarken = 0.65f;  // post-LP corner *= (1 - this * wear)

// ============================================================================
// CONTACT - hard clip, deeper crumble, darker Tone
// ============================================================================
// In Contact the threshold slides from "never" down to kContactClipMin as wear
// climbs, so a worn-in signal squares off on its peaks; the sample-and-hold
// length is multiplied by kContactDecimateMul so the crumble bites harder for
// the same Grind; and the Tone knob position is multiplied by kContactToneScale
// before the map, because Contact throws a lot more energy up top - so 75 % of
// Tone in Contact lands around where 50 % would in Oxide.
constexpr float kContactClipMin     = 0.30f;
constexpr float kContactDecimateMul = 1.8f;
constexpr float kContactToneScale   = 0.667f;

// ============================================================================
// TONE (the Tone knob) - post low-pass, the same shape the other engines use
// ============================================================================
constexpr float kToneMinHz    = 300.0f;
constexpr float kToneMaxHz    = 20000.0f;
constexpr float kToneKnobSkew = 0.4f;
constexpr float kToneBypassHz = 19000.0f;

// One-pole glide on the smoothed Tone corner so a turn does not zipper.
constexpr float kSmoothMs     = 4.0f;
constexpr int   kControlBlock = 16;

// ============================================================================
// DEFAULTS (knob positions the engine opens on, 0..100)
// ============================================================================
constexpr float kDefaultGrindPct = 50.0f;
constexpr float kDefaultTonePct   = 65.0f; // engaged, tames the grit's fizz

constexpr int kModeOxide   = 0;
constexpr int kModeContact = 1;

// ============================================================================
// KNOB -> UNIT
// ============================================================================

/** Tone knob (0..1) -> post low-pass corner in Hz. Knob 1 -> kToneMaxHz
    (bypassed), knob 0 -> kToneMinHz. Contact scales the knob down first (see
    kContactToneScale); this map is the raw curve either mode feeds. */
inline float toneHzFor (float knob01) noexcept
{
    const float t = std::pow (std::clamp (knob01, 0.0f, 1.0f), kToneKnobSkew);
    return kToneMinHz * std::pow (kToneMaxHz / kToneMinHz, t);
}

} // namespace ee::dsp::rust
