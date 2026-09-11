#pragma once

/**
 * Voicing for ee::dsp::ResonatorBank - Peak Sympathy's sympathetic-resonance
 * engine.
 *
 * The pedal is ~20 % resonator bank and ~80 % exciter, ducking and tuning: a
 * bank of tuned string loops is trivial and sounds like flanged mush on its
 * own. Everything that turns "a comb bank" into "strings ringing behind the
 * player" - how an attack is detected, the shape and brightness of the noise
 * burst fired into the strings, how weakly the continuous signal bleeds in,
 * how each string's loop gain is derived from a decay *time* rather than a
 * fixed feedback number, the per-string energy limiting and the bank-wide soft
 * limiter, the envelope-driven dry duck, the coupling matrix and the tuning
 * tables - lives here, not inline in the processor.
 *
 * Its own namespace (ee::dsp::sympathy) rather than the shared
 * ee::dsp::config, for the reason the other *Config.h headers give: several
 * voicing headers land in one test translation unit and would collide on names
 * this general.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host (or re-run ee_sympathy_match).
 */

#include <array>

namespace ee::dsp::sympathy
{

// ============================================================================
// EXCITER - GATE FOLLOWER
// ============================================================================
// The signal fed to ee::dsp::OnsetGate is a smoothed envelope, not the raw
// signal (a low note's own waveform would ripple through the detector window).
// Same recipe as ee::dsp::AutoWah: a one-pole DC/rumble highpass, full-wave
// rectify, a small noise floor subtracted, then an asymmetric one-pole with a
// fast attack and a slow release.
constexpr float kEnvHpHz      = 25.0f;   // strip rumble before rectifying
constexpr float kEnvAttackMs  = 1.0f;    // follower attack
constexpr float kEnvReleaseMs = 60.0f;   // follower release - holds through the attack ripple
constexpr float kNoiseFloor   = 0.0006f; // ~ -64 dBFS subtracted off the rectified signal

// ============================================================================
// EXCITER - ONSET DETECTION
// ============================================================================
// ee::dsp::OnsetGate, ported from Cycfi Q. It fires once per attack: the rise
// of the follower over kOnsetAttackWidthMs, measured as a fraction of the
// pre-attack level, has to clear kOnsetRiseRatioOn (and an absolute floor,
// kOnsetMinRise, so plain noise near silence cannot ratio its way to a
// trigger); it re-arms once that ratio falls back under kOnsetRiseRatioOff,
// and cannot fire again for kOnsetLockoutMs so one bloom-y pluck kicks the
// bank exactly once. Values tracked from AutoWahConfig.h, with a shorter
// lockout - a sympathetic bank wants to be re-excited by fast playing.
constexpr float kOnsetEnvDecayMs    = 120.0f;
constexpr float kOnsetAttackWidthMs = 15.0f;
constexpr float kOnsetRiseRatioOn   = 0.50f;
constexpr float kOnsetRiseRatioOff  = 0.15f;
constexpr float kOnsetMinRise       = 0.004f;
constexpr float kOnsetLockoutMs     = 60.0f;

// ============================================================================
// EXCITER - THE BURST
// ============================================================================
// On a detected attack a short packet of white noise is windowed into the
// bank: a quick linear fade-in (kBurstAttackMs) so it does not start with a
// step, then an exponential decay - amplitude is exp(-kBurstDecayShape * pos /
// length). A gentle one-pole lowpass (kBurstLpHz) turns the noise "tick" into
// a "pluck". Burst amplitude scales with the attack strength (follower value
// at the onset) times kBurstEnvScale, clamped to kBurstGainMax.
//
// kBurstGainMax and kBurstEnvScale were originally 1.0 / 6.0, which measured
// (on a real guitar take, comparing the isolated wet signal's first 10 ms
// against its own level 100-300 ms later) at 1.8-2.3x louder right at the hit
// than the tuned ring that follows it - the burst was arriving pinned at full
// gain for essentially any normal pluck, so it read as a fixed, unpitched
// "hit" sample sitting on top of a comparatively quiet chord. Turned down
// here, and kBurstMs shortened, so the transient kicks the strings rather than
// out-singing them.
constexpr float kBurstMs         = 11.0f;
constexpr float kBurstAttackMs   = 1.0f;
constexpr float kBurstDecayShape = 4.5f;
constexpr float kBurstLpHz       = 5200.0f;
constexpr float kBurstEnvScale   = 3.2f;
constexpr float kBurstGainMax    = 0.55f;

// ============================================================================
// EXCITER - CONTINUOUS BLEED
// ============================================================================
// The player's signal also passes into the strings continuously, but far more
// weakly than the burst - enough to keep a held note or a bowed swell feeding
// the bank without turning the dry tone into a comb filter. Highpassed first
// (kBleedHpHz) so sub-fundamental energy does not just make the delay line
// wander. kBleedGain is the linear weight, ~ -30 dB.
constexpr float kBleedHpHz = 120.0f;
constexpr float kBleedGain = 0.03f;

// ============================================================================
// EXCITER - SENSITIVITY
// ============================================================================
// The Sensitivity knob (0..1) is exciter drive: a log map from kSensMin to
// kSensMax applied to burst + bleed together. 0.5 on the knob is unity.
constexpr float kSensMin        = 0.25f;
constexpr float kSensMax        = 4.0f;
constexpr float kSensZipSeconds = 0.02f; // one-pole slew on the drive, anti-zip

// ============================================================================
// BLOOM  (chosen meaning: a rising-damping sweep)
// ============================================================================
// "Bloom" is a post-onset upward brightness sweep. On each detected attack the
// bank's effective damping snaps toward kBloomFloor (dark) and relaxes back to
// the Damping knob's setting over kBloomMaxMs * bloom01. The Bloom knob (0..1)
// scales both the depth of the initial darkening and the length of the sweep,
// so the resonance *opens up* behind the note instead of arriving fully bright.
// At 0 it does nothing and Damping is static.
//
// Picked over "attack envelope on the wet" (that just sounds like a slow gate)
// and "extra energy injection" (fights the Stage 2 limiters for headroom).
constexpr float kBloomFloor  = 0.05f;   // effective brightness right after an onset at full Bloom
constexpr float kBloomMaxMs  = 1400.0f; // sweep length at bloom01 = 1
constexpr float kBloomMinMs  = 180.0f;  // ... and at bloom01 just above 0

// ============================================================================
// STRING LOOP
// ============================================================================
// A tuned waveguide: integer delay tap -> damping one-pole -> first-order
// allpass fraction -> loop gain -> back into the line. No interpolator sits in
// the loop (see ResonatorBank.h for why - Hermite HF loss would become the
// dominant damping at hundreds of loop trips a second).
//
// Damping is the one-pole  H(z) = (1-b)/(1 - b z^-1)  - unity DC gain by
// construction. Brightness (0 dark .. 1 bright) maps to b from kDampBMax down
// to kDampBMin; its phase delay at the fundamental is measured and taken out
// of the delay-line length so the string does not detune as Damping moves.
constexpr float kDampBMin = 0.02f; // brightest - almost no in-loop rolloff
constexpr float kDampBMax = 0.55f; // darkest

// Loop gain is derived per string from the target decay time: to lose 60 dB in
// t60 seconds a string of fundamental f0 needs a per-trip gain of
// exp(ln(0.001) / (t60 * f0)), then divided by the damping filter's own
// magnitude at f0. Capped just under unity so a long decay on a high string
// can never ask for a runaway loop.
constexpr float kLn001       = -6.90775527898214f; // ln(0.001)
constexpr float kLoopGainMax = 0.9995f;

// Guard rails for the feedback state: a marginal instability climbs without
// ever going non-finite, so clamp what is written back, and treat anything
// past the ceiling as a NaN would be treated. ~ +28 dBFS.
constexpr float kStateCeiling = 24.0f;

// Output-tap DC blocker corner. On the tap only, never in the loop.
constexpr float kDcBlockHz = 8.0f;

// Playable fundamental range. The delay line is sized for the lowest.
constexpr float kMinF0 = 30.0f;
constexpr float kMaxF0 = 5000.0f;
constexpr float kMaxDelaySeconds = 1.0f / kMinF0 + 0.005f;

// Decay-time (T60) range the Decay knob spans, in seconds, log-mapped. Freeze
// drives the effective loop gain to unity independently of this.
constexpr float kDecayMinSeconds = 0.20f;
constexpr float kDecayMaxSeconds = 30.0f;

// ============================================================================
// STAGE 2 - GAIN STAGING AND DUCKING
// ============================================================================
// Per-string energy limiter. A high-Q loop accumulates energy; a sustained
// chord tuned near a string's mode drives it far past any single pluck. Each
// string tracks the mean square of its own output through a one-pole
// (kEnergyFollowHz) and, above kStringEnergyCeiling, scales its own feedback
// write-back by sqrt(ceiling / energy) - a soft, per-sample ceiling on stored
// energy, not a clipper on the output.
constexpr float kEnergyFollowHz      = 6.0f;
constexpr float kStringEnergyCeiling = 0.18f; // mean-square, ~ -7.5 dBFS RMS per string

// Bank-wide soft limiter across the summed output: x / (1 + |x| * kSoftLimitK),
// odd and smooth, unity slope at the origin so quiet passages are untouched.
// A safety stage, not a voicing one - if it is audible something upstream is
// already wrong.
constexpr float kSoftLimitK       = 0.55f;
constexpr float kBankRunawayLevel = 48.0f; // past this, reset the bank (NaN-equivalent)

// Envelope-driven dry duck. Not a static mix offset: the dry level dips under
// the Mix setting while the player digs in and recovers after, so the
// sympathetic wash steps forward during and just behind a phrase. kDuckDepth
// is the most it can pull the dry down (linear), reached at a hard attack.
constexpr float kDuckDepth      = 0.35f;
constexpr float kDuckAttackMs   = 8.0f;
constexpr float kDuckReleaseMs  = 420.0f;
constexpr float kDuckDriveScale = 5.0f; // follower -> duck amount, before the clamp

// ============================================================================
// STAGE 3 - THE BANK AND COUPLING
// ============================================================================
// Sixteen strings. Fixed rather than a knob: the coupling matrix size is baked
// into the mixing transform.
constexpr int kNumStrings = 16;

// Cross-coupling is a feedback *matrix*, not a naive additive sum of
// neighbours (which self-oscillates). Each sample the sixteen pre-gain loop
// signals go through the doubly-stochastic circular smoother C
// (coupleCircular), and each string's feedback becomes the blend
// (1-c)*own + c*(C*loop)[i] with c = coupling01 * kMaxCoupling. C has spectral
// radius exactly 1, so the loop stays bounded by kLoopGainMax for any Coupling
// - the long stress run at Coupling = Decay = max proves it.
//
// Honest tradeoff: because C low-passes the (mostly uncorrelated) per-string
// signals, a strong blend also shrinks each string's own coherent feedback, so
// Coupling costs a little sustain and level as it rises - physically what
// energy exchange between strings does. kMaxCoupling keeps the knob's top
// where that cost stays a "tightening", not a drop, and kCouplingMakeup adds
// back most of the level. The lush inter-string beating is Spread's job, not
// this - see the note there and in the build brief.
constexpr float kMaxCoupling = 0.30f;

// Bank-output makeup, (1 + kCouplingMakeup * coupling01), for the sustain the
// blend trades for movement. Calibrated by ee_sympathy_match on the guitar riff.
constexpr float kCouplingMakeup = 0.8f;

// Micro-detune / Spread. The beating comes from here, not from Coupling: each
// string carries a fixed offset in [-1, 1] (kDetunePattern) scaled by
// spread01 * kMaxDetuneCents. A little spread is lush; a lot is a detuned piano.
constexpr float kMaxDetuneCents = 22.0f;

// A fixed, evenly-scattered but non-repeating detune pattern for the sixteen
// strings - low-discrepancy so no two adjacent strings share an offset and the
// set is symmetric about zero (mean detune stays put as Spread moves).
constexpr std::array<float, kNumStrings> kDetunePattern = {
    -0.94f, 0.63f, -0.31f, 0.88f, -0.69f, 0.19f, -0.50f, 1.00f,
    -1.00f, 0.44f, -0.13f, 0.75f, -0.81f, 0.31f, -0.56f, 0.50f
};

// Stereo placement: strings fan across the field by index so the bank has
// width. kStereoSpread scales how far out the outermost strings sit (1 = hard
// L/R). Equal-power pan.
constexpr float kStereoSpread = 0.85f;

// Bank output trim, folded into the per-string pan gains. A pure incoherent-sum
// normalisation would be 1/sqrt(16) = 0.25; a real note only lights a few
// strings, so this sits well above that so the wet reaches the dry's level and
// Mix has something to balance. The bank-wide soft limiter and kBankRunaway
// guard sit downstream of it.
constexpr float kBankOutputGain = 0.85f;

// ============================================================================
// STAGE 4 - TUNING
// ============================================================================
// Retune must not zipper. Each string glides its own total loop delay
// geometrically (cents-linear) toward the new target; kRetuneGlideMs is the
// time constant. A 2-bank crossfade is the documented fallback if a glide this
// fast ever audibly bends - it does not at these rates.
constexpr float kRetuneGlideMs   = 90.0f;
constexpr float kControlGlideMs  = 40.0f; // brightness / decay / coupling smoothing

// How far Freeze pushes the loop: exactly unity, so a frozen bank neither
// grows (the per-string limiter holds it) nor decays.
constexpr float kFreezeLoopGain      = 1.0f;
constexpr float kFreezeExciterBleed  = 0.0f;  // no new plucks into a frozen bank

// The pitch of key "C" before the per-string tuning ratios and the Octave knob
// move it: C2, so key "A" lands on 110 Hz and the bank sits in a guitar's
// register. Key is 0..11 semitones above this.
constexpr float kRootBaseHz = 65.406f;

// Octave knob: an integer transpose of the whole bank, -2..+2.
constexpr int kOctaveMin = -2;
constexpr int kOctaveMax = 2;

enum class TuningMode
{
    octaves = 0, // 1, 2, 1/2, 4 ... octaves only. Cannot clash.
    fifths,      // stacked 3:2 and octaves. Cannot clash.
    harmonic,    // the harmonic series over a root an octave down. Cannot clash.
    majorJI,     // just-intonation major scale relative to the key root
    minorJI,     // just-intonation natural minor relative to the key root
    chroma,      // 12-TET, nearest-semitone quantise - most flexible, least lush
    count
};

// Just-intonation scale ratios within one octave. The bank walks these across
// several octaves; degrees repeat an octave up as *2.
constexpr std::array<float, 7> kMajorJI = {
    1.0f, 9.0f / 8.0f, 5.0f / 4.0f, 4.0f / 3.0f, 3.0f / 2.0f, 5.0f / 3.0f, 15.0f / 8.0f
};
constexpr std::array<float, 7> kMinorJI = {
    1.0f, 9.0f / 8.0f, 6.0f / 5.0f, 4.0f / 3.0f, 3.0f / 2.0f, 8.0f / 5.0f, 9.0f / 5.0f
};

// Key-learn: the processor runs the detector; these bound it. A pitch estimate
// has to hold within kLearnStableCents of a candidate for kLearnHoldMs before
// the key is moved, so a bend or a passing note does not retune the bank.
constexpr float kLearnStableCents = 35.0f;
constexpr float kLearnHoldMs      = 220.0f;
constexpr float kLearnMinHz       = 55.0f;
constexpr float kLearnMaxHz       = 880.0f;

} // namespace ee::dsp::sympathy
