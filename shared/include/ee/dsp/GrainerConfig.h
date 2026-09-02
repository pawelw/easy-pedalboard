#pragma once

/**
 * Voicing for ee::dsp::Grainer, the granular scatterer behind Peak Grain.
 *
 * This file holds the structural side: the knob ranges, the buffer the engine
 * records into, and how many grains may sound at once. Changing one of these
 * changes what the knobs can ask for.
 *
 * The *voicing* - how many grains run backwards, where they land across the
 * image, which intervals the pitched ones snap to, how ragged the timing is -
 * lives in GrainerTuning.h instead, because the development tuning panel drives
 * those live (-DEE_GRAIN_TUNER=ON).
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build-fast
 *   3. Relaunch the standalone.
 */

namespace ee::dsp::config
{

// ============================================================================
// BUFFER AND VOICES
// ============================================================================
// How much of the past the engine keeps. This has to cover the longest Spray,
// plus one longest grain of output, plus the source that grain spans at the
// fastest playback rate (an octave up eats two source samples per output
// sample), plus a guard. Everything below is bounded by these three, and the
// figure is deliberately loose enough that a backwards grain at the longest
// Spray still fits without being clamped back.
constexpr float kGrainBufferSeconds = 10.5f;

constexpr float kMinGrainSeconds    = 0.020f;
constexpr float kMaxGrainSeconds    = 0.500f;
constexpr float kMaxDecaySeconds    = 8.000f;

// Concurrent grains. At the top of the Density range with the longest grains
// the engine wants density x size = 40 x 0.5 = 20 of them, so 32 leaves room
// for the spawn jitter to bunch a few together without stealing.
constexpr int kMaxGrains = 32;

// Guard between a grain's read position and the write head, in samples. Small;
// it only has to cover the Hermite interpolator's four-sample window and the
// fractional part of the read.
constexpr int kGrainReadMarginSamples = 8;

// ============================================================================
// DENSITY
// ============================================================================
// Grains spawned per second. The knob is skewed so the sparse, countable end
// gets most of the travel - past about 20 /s the changes are textural rather
// than rhythmic.
constexpr float kMinDensityHz     = 1.0f;
constexpr float kMaxDensityHz     = 40.0f;
constexpr float kDensitySkewHz    = 12.0f;
constexpr float kDefaultDensityHz = 12.0f;

// ============================================================================
// GRAIN SIZE
// ============================================================================
// Below ~40 ms the grains stop being fragments of the input and turn into a
// metallic buzz at the spawn rate; above ~300 ms you hear whole notes.
constexpr float kMinGrainMs     = kMinGrainSeconds * 1000.0f;
constexpr float kMaxGrainMs     = kMaxGrainSeconds * 1000.0f;
constexpr float kGrainSkewMs    = 120.0f;
constexpr float kDefaultGrainMs = 120.0f;

// ============================================================================
// DECAY
// ============================================================================
// How long the cloud keeps going. Two things at once, because they are the
// same thing heard from either end: it sets how far back into the recording a
// grain may be drawn from, and how far a grain's level falls off with how old
// its source is. So a long Decay is a long tail, and the far end of that tail
// is quiet - the fragments fade out rather than stopping.
constexpr float kMinDecayMs     = 50.0f;
constexpr float kMaxDecayMs     = kMaxDecaySeconds * 1000.0f;
constexpr float kDecaySkewMs    = 700.0f;
constexpr float kDefaultDecayMs = 400.0f;

// Level a grain drawn from the far end of the Decay window plays at, as a
// gain. The falloff runs across the window, so Decay is a genuine tail length:
// at 8 s the grains fade over eight seconds, not over the first one of them.
//   0.10 = -20 dB by the end, the tail stays present a long time
//   0.03 = -30 dB by the end                               <-- default
//   0.005 = the far end drops away sharply
constexpr float kOldestGrainGain = 0.03f;

// ============================================================================
// ATTACK
// ============================================================================
// A plucked string is mostly its first fifty milliseconds, and a cloud built
// from the sustain alone loses whatever made the note identifiable. An onset
// detector marks where each attack landed in the recording, and this share of
// grains is drawn from there rather than from a random point in the window.
//   0.00 = the old behaviour, every grain placed at random
//   0.70 = the attack is the voice of the cloud            <-- default
//   1.00 = nothing but the attack, over and over
constexpr float kDefaultAttackShare = 0.70f;

// How far into the attack a grain may start, in milliseconds - a little spread
// so the repeats are not all bit-identical.
constexpr float kAttackJitterMs = 12.0f;

// ============================================================================
// REVERSE, STEREO, DETUNE
// ============================================================================
// Share of grains that play backwards. Forward-only is much more legible; past
// halfway the phrase stops being followable at all.
constexpr float kDefaultReversePct = 25.0f;

// Width of the random pan placement. 0 puts every grain in the centre, 100
// throws them hard left and right. Equal-power, so the middle does not dip.
constexpr float kDefaultStereoPct = 85.0f;

// Random detune on every grain, in cents either way. A few cents is what stops
// a stack of unshifted grains phasing into one flat tone; wound up it is a
// chorus of slightly wrong copies.
constexpr float kMinDetuneCents     = 0.0f;
constexpr float kMaxDetuneCents     = 100.0f;
constexpr float kDetuneSkewCents    = 12.0f;
// Zero by default: at anything above it every grain plays at a slightly
// different pitch, which reads as an unstable, out-of-tune cloud rather than
// as the note that was played. Dial it in deliberately.
constexpr float kDefaultDetuneCents = 0.0f;

// ============================================================================
// PITCH
// ============================================================================
// Three weights rather than one bipolar knob, so a cloud can carry octaves
// below, the root, and fifths above all at once. Each grain picks one of the
// three at random in proportion to these, then an interval from that group's
// table. All three at zero is treated as unison only - a face with no pitch
// dialled in should still make a sound.
constexpr float kDefaultPitchLowPct    = 0.0f;
constexpr float kDefaultPitchUnisonPct = 100.0f;
constexpr float kDefaultPitchHighPct   = 0.0f;

// ============================================================================
// LEVEL
// ============================================================================
// Points in the Hann window table. 2048 with linear interpolation is inaudible
// against a computed cosine and costs one multiply per sample.
constexpr int kWindowPoints = 2048;

// ============================================================================
// REVERB
// ============================================================================
// Peak Grain runs ee::dsp::FdnReverb plain: the two knobs are its mix and its
// decay, and everything else is pinned here. No shimmer - the header states 0
// means the pitch shifters never run, so it costs nothing.
constexpr float kVerbShimmer   = 0.0f;

// One knob, not two: it opens the reverb mix and lengthens its decay together,
// which is the only way the two are ever actually used. 0 is bone dry.
constexpr float kDefaultReverbPct = 35.0f;

// ============================================================================
// MIX
// ============================================================================
// Prefixed because ee::dsp::config is one flat namespace shared by every
// engine, and Chorus already owns the plain kDefaultMixPct.
constexpr float kDefaultGrainMixPct = 50.0f;

} // namespace ee::dsp::config
