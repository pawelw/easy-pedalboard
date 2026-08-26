#pragma once

/**
 * Voicing for FdnReverb.
 *
 * The defaults here were fitted against a reference plate recording. The decay
 * knob sets the mid-band RT60; the two multipliers below only ever make a band
 * die *sooner* than that, never later.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::config
{

// ============================================================================
// LOW DECAY
// ============================================================================
// Fraction of the mid-band decay that the low end gets, and where the low
// shelf sits. This is the single biggest lever on "boomy / resonant /
// ringing". A real plate has very little low-end sustain.
//   0.35 = tight, almost no low tail
//   0.45 = plate (reference measures ~0.51 at 100-300 Hz)  <-- default
//   0.75 = hall-ish warmth
//   1.00 = lows ring as long as mids (boomy on guitar)
constexpr float kLowDecayRatio = 0.40f;
constexpr float kLowCornerHz = 450.0f;

// ============================================================================
// HIGH DECAY
// ============================================================================
// Fraction of the mid-band decay that the top end gets, and where the high
// shelf sits. Because this is a shelf and not a rolloff, the air band keeps
// the same ratio as the presence band instead of collapsing.
//   0.45 = dark, damped plate
//   0.50 = bright, airy plate  <-- default
//   1.00 = no absorption at all (metallic)
constexpr float kHighDecayRatio = 0.45f;
constexpr float kHighCornerHz = 5000.0f;

// ============================================================================
// INPUT DIFFUSION
// ============================================================================
// Base allpass coefficient of the input smearing ladder. Higher smears the
// attack more before it reaches the network, which is what stops individual
// reflections being audible as separate "bounces".
//   0.60 = you can hear the early reflections
//   0.80 = smooth  <-- default
//   0.85 = very washed out, soft transients
constexpr float kDiffusion = 0.80f;

// ============================================================================
// TANK DIFFUSION
// ============================================================================
// Allpass coefficient of the diffusers sitting inside the feedback loop, one
// per delay line. These are what keep the tail smooth: they multiply the echo
// density on every trip without changing the decay rate, because an allpass
// passes all its energy through. Without them the tail ripples audibly as
// energy sloshes between lines, which is heard as bouncing or repeating.
// 0 disables them.
constexpr float kTankDiffusion = 0.72f;

// ============================================================================
// STEREO
// ============================================================================
// Allpass coefficient of the output decorrelators, and the correlation the wet
// path is steered to afterwards.
//
// This is a *wet-only* figure and it is not the number that reaches the mix
// bus. The dry path is mono and the modulation pulls the channels further
// apart, so the finished signal always measures wider than this. Fitted so a
// 50 % mix lands near the reference reverb's image.
constexpr float kStereoSpread = 0.45f;
constexpr float kTargetCorrelation = 0.70f;

// ============================================================================
// PREDELAY
// ============================================================================
// Scales with the decay knob between these two values. A long predelay puts an
// audible gap between the note and the tail, which reads as a slap.
constexpr float kPredelayMinMs = 6.0f;
constexpr float kPredelayMaxMs = 28.0f;

// ============================================================================
// MODULATION DEPTH
// ============================================================================
// Peak delay-line deviation in samples at 44.1 kHz. The floor applies even at
// Mod = 0, because a completely static network parks its modes on fixed pitches
// and those are what get heard as ringing.
constexpr float kModFloorSamples = 2.5f;
constexpr float kModDepthSamples = 9.0f;

} // namespace ee::dsp::config
