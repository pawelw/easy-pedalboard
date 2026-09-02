#pragma once

/**
 * Voicing for ee::dsp::AutoWah.
 *
 * Peak Wah is an LFO-driven modulated filter: a wave sweeps a series-RLC tank's
 * cutoff around the Freq setting, tapped anywhere on a continuous low- / band- /
 * high-pass morph. A fast envelope opens the modulation on a note and the
 * Decay knob is how quickly it flattens once you stop - turned fully up it
 * latches on and runs continuously.
 *
 * Its own namespace (ee::dsp::autowah) rather than the shared ee::dsp::config,
 * because it reuses names like kControlBlock / kDcBlockerHz that other voicings
 * also define and every header lands in the same test translation unit.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::autowah
{

// ============================================================================
// GATE (the Decay knob)
// ============================================================================
// A one-pole follower on the high-passed, rectified, noise-floored input. Fast
// attack so the wobble is at full depth almost immediately; release is the
// Decay knob (kDecayMinMs..kDecayMaxMs, exponential) - short flattens the sweep
// right after each note, long lets it keep moving. kGateSensitivity is high on
// purpose: the gate is essentially on/off with a release tail, not a dynamics
// follower.
constexpr float kDetectorHighpassHz = 30.0f;
constexpr float kNoiseFloor         = 0.0016f;   // ~ -56 dBFS; below this the gate reads zero
constexpr float kGateSensitivity    = 8.0f;

constexpr float kAttackMs   = 3.0f;
constexpr float kDecayMinMs = 60.0f;
constexpr float kDecayMaxMs = 3000.0f;

// The top slice of the Decay knob (from kLatchKnee0 to 1.0) ramps a floor under
// the gate, so Decay fully up pins it open and the LFO never stops - a plain
// tempo/rate-synced filter with no dynamics.
constexpr float kLatchKnee0 = 0.90f;

// The bottom slice (from 0 to kOneShotKnee) crossfades to a one-shot: on a
// pluck the LFO runs kOneShotCycles of a cycle and then the gate fades out over
// kOneShotReleaseMs - a single envelope sweep per note. Half a cycle stops the
// sweep at the bottom of the wave (up then down), rather than a whole one
// carrying it back up to where it started.
constexpr float kOneShotKnee = 0.16f;
constexpr float kOneShotCycles = 0.5f;
constexpr float kOneShotReleaseMs = 18.0f;

// ============================================================================
// LFO
// ============================================================================
// Shape is ee::dsp::lfoValue's 0..1 morph (exp decay .. ramp .. triangle ..
// soft square .. hard chop). The Stereo switch offsets the right channel by
// kStereoOffset of a cycle (½ = anti-phase).
constexpr float kStereoOffset = 0.5f;

// The LFO free-run period range and default live in plugins/peak-wah/src/RateMap.h.

// ============================================================================
// PLAYABLE DYNAMICS
// ============================================================================
// A separate follower (kDynAttackMs / kDynReleaseMs), scaled and clamped by
// kDynSensitivity, drives two touches that make the pedal feel played:
//
//   * RATE - the LFO runs up to kRateEnvDepth faster at a hard hit, so the
//     wobble breathes with your picking;
//   * RETRIGGER - ee::dsp::OnsetGate, ported from Cycfi Q's onset_gate. It is
//     fed the smoothed gate follower; a peak follower (kOnsetEnvDecayMs release)
//     holds it through the ripple and its rise over a fixed kOnsetAttackWidthMs
//     window, taken as a fraction of the pre-attack level, is the attack. Grow
//     by kOnsetRiseRatioOn of where it started and it resets the LFO phase to 0
//     so the sweep kicks from the top of the wave; it re-arms once that ratio
//     falls back under kOnsetRiseRatioOff, and cannot fire again for
//     kOnsetLockoutMs so one bloom-y hard pluck kicks the sweep once. The ratio
//     (rather than an absolute rise) is what keeps a quiet note as detectable as
//     a loud one; kOnsetMinRise is an absolute floor so noise near silence
//     can't ratio its way in.
constexpr float kDynAttackMs   = 5.0f;
constexpr float kDynReleaseMs  = 180.0f;
constexpr float kDynSensitivity = 3.0f;
constexpr float kRateEnvDepth  = 0.15f;

constexpr float kOnsetEnvDecayMs    = 120.0f;  // release of the onset peak follower
constexpr float kOnsetAttackWidthMs = 15.0f;   // window the envelope rise is measured over
constexpr float kOnsetRiseRatioOn   = 0.55f;   // grow by this fraction of the pre-attack level to fire
constexpr float kOnsetRiseRatioOff  = 0.15f;   // ratio falling back below this re-arms it
constexpr float kOnsetMinRise       = 0.005f;  // absolute rise the window must also clear (~ -46 dB)
constexpr float kOnsetLockoutMs     = 90.0f;   // minimum time between retriggers

// A retrigger snaps the LFO phase, which steps its output - and by the time the
// onset detector fires, the gate is already open, so that step lands straight on
// the cutoff and is heard as a click at the start of a note. The jump is carried
// as an offset that starts equal and opposite to it (so the modulation is
// continuous through the retrigger) and decays away over this long, leaving the
// sweep on its new phase. Short enough that the kick is still a kick; long
// enough that no single sample has to carry the whole step.
constexpr float kRetriggerGlideMs = 12.0f;

// ============================================================================
// CUTOFF SWEEP
// ============================================================================
// mod = Amount * gate * lfo  (lfo in [-1, 1]); the tank centre frequency is
// fBase * kSweepRatioMax^mod, so at Amount = 1 and a full swing it covers
// fBase / kSweepRatioMax .. fBase * kSweepRatioMax (~2.3 octaves either way).
// The Freq knob sets fBase, log spaced; kFreqKnobSkew < 1 gives the low end
// more of the travel.
constexpr float kFreqMinHz    = 200.0f;
constexpr float kFreqMaxHz    = 1600.0f;
constexpr float kFreqKnobSkew = 0.8f;
constexpr float kSweepRatioMax = 5.0f;

// One-pole smoothing on the target centre frequency, so the per-control-block
// retune of the tank can never step the pitch of the peak.
constexpr float kFreqSmoothingMs = 4.0f;

// Ramp on the knob-derived values that otherwise step the output when a control
// is nudged - Mix (dry/wet), Amount, Q and the Freq base.
constexpr float kParamSmoothMs = 15.0f;

// ============================================================================
// RESONANT TANK (chowdsp_wdf)
// ============================================================================
// A series RLC solved as a Wave Digital Filter. L is fixed; the Freq knob and
// LFO set C = 1 / ((2 pi f0)^2 L); the Q knob sets R = (1 / Q) sqrt(L / C).
// Filter type is which element's voltage is tapped:
//   V_C = low-pass   V_R = band-pass   V_L = high-pass
// One solve gives all three, so the Type knob crossfades between them
// continuously - LP at 0, BP at 50 %, HP at 100 % - the way Shape morphs the
// LFO wave.
// C and R are refreshed every kControlBlock samples per channel.
constexpr float kInductanceH = 0.5f;
constexpr float kQMin        = 1.8f;
constexpr float kQMax        = 10.0f;
constexpr float kQKnobSkew   = 1.0f;
constexpr int   kControlBlock = 16;

// ============================================================================
// OUTPUT STAGE
// ============================================================================
// Per-tap make-up, crossfaded in dB with the taps themselves so the level holds
// both as Type is turned and as Mix is turned up: a band-pass tap throws away
// everything off the peak and needs the biggest lift; the low- and high-pass
// taps keep a whole half of the spectrum and need less. These are half of what
// full wet-vs-dry RMS parity asks for: matching the level exactly made turning
// Mix up feel like a volume boost, since the wet peak sits on top of the dry
// rather than replacing it. Half the compensation lands the blend where the ear
// expects it. kGritDrive is a tanh soft-clip - unity at normal levels, only
// rounding the hottest peaks so the make-up can't run away. Then a DC blocker
// and a mild low-pass.
constexpr float kMakeupDbLP = 4.5f;
constexpr float kMakeupDbBP = 10.0f;
constexpr float kMakeupDbHP = 2.5f;
constexpr float kGritDrive  = 0.9f;
constexpr float kDcBlockerHz = 12.0f;
constexpr float kOutputLowpassHz = 8000.0f;

// ============================================================================
// DEFAULTS (knob positions the pedal opens on, 0..100 unless noted)
// ============================================================================
constexpr float kDefaultRangePct  = 61.0f;
constexpr float kDefaultFreqPct   = 48.7f;   // 644 Hz
constexpr float kDefaultQPct      = 58.0f;
constexpr float kDefaultMixPct    = 52.0f;
constexpr float kDefaultDecayPct  = 3.0f;    // one sweep per pluck
constexpr float kDefaultStereoPct = 0.0f;
constexpr float kDefaultShapePct  = 60.0f;   // triangle, leaning square
constexpr float kDefaultTypePct   = 50.0f;   // band-pass, midway between LP and HP

} // namespace ee::dsp::autowah
