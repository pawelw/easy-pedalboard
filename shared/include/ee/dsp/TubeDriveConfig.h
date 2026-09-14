#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/**
 * Voicing for ee::dsp::TubeDrive - Peak Artifact / Peak Alpine's Amp Drive.
 *
 * MATCHED, NOT DESIGNED. Every value at the 100 % end of this file was fitted
 * against a recording of a real reference unit with its drive fully up, over
 * the same dry take (dist-dry.wav / dist-wet-100%.wav, 48 kHz, not in this
 * repo), by minimising the time-domain residual of the whole render against
 * the wet file after removing the reference's 187-sample latency and its
 * output level (level was told to be ignored). What the recording showed, in
 * the order it was established:
 *
 *   - Quiet passages come out ~10 dB louder, loud ones ~7 dB quieter - a
 *     12 dB decay leaves as 3 dB - with the original harmonic balance mostly
 *     intact. Heavy compression, not hard clipping.
 *   - The small-signal response (from the noise floor between notes, where
 *     coherence is 0.99) is a mid lift: 0 dB at 100 Hz, +10.8 dB at 1-2 kHz,
 *     +6.8 dB at 6 kHz.
 *   - That lift is split around the nonlinearity, not ahead of it: a +20 dB
 *     pre-emphasis shelf into the curve, a -15 dB de-emphasis shelf after it.
 *     Both "all ahead" and "all after" fit ~3 dB worse, and so does any
 *     envelope follower / limiter (the fit drove it out of the model).
 *   - The curve is logarithmic rather than a clip: asinh, log1p, a power law
 *     and a very soft rational knee all land on the same -10.6 dB residual
 *     with the same filters around them; tanh and atan, which saturate,
 *     fit worse. asinh is the one kept.
 *   - It is biased, slightly asymmetric, and DC-blocked just under 10 Hz -
 *     the reference leaves a small low thump after each note ends that is
 *     exactly that bias settling through the blocker.
 *
 * Knob-down is an exact pass-through (see the class note). In between, one
 * curve (kDriveSkew) walks the input gain, the bias and both shelves from
 * flat to the fitted values together, so the voicing arrives as one thing.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. ee_bit_check <outDir> dist-dry.wav, and A/B real_drive_100pct.wav.
 */

namespace ee::dsp::tubedrive
{

// ============================================================================
// DRIVE (the Drive knob)
// ============================================================================
// Gain into the curve, swept exponentially over pow (drive01, kDriveSkew).
// kDriveMinGain is where the curve is still near-straight for a full-scale
// signal (asinh (0.25) / 0.25 = 0.99), so the first few percent of travel
// fade the character in rather than stepping into it. kDriveMaxGain is the
// fitted reference.
constexpr float kDriveMinGain = 0.25f;
constexpr float kDriveMaxGain = 19.9f;
constexpr float kDriveSkew = 0.6f;

// Knob position the pedal opens on (0..100, like the other Artifact knobs).
constexpr float kDefaultDrivePct = 35.0f;

// ============================================================================
// KNOB MOVEMENT
// ============================================================================
// The knob position glides and every stage is re-voiced from the glided value
// every sample while it moves - a jump in gain, bias and both shelf corners at
// once is a click otherwise. The glide is two one-poles in series, kSmoothMs
// each: critically damped, so it also sets off with zero slope. A single pole
// starts at full speed, and that corner in the gain is itself a small tick.
constexpr float kSmoothMs = 10.0f;

// Leaving 0 and arriving back at it crossfade linearly against the dry input
// over kFadeMs, so the bit-exact pass-through is never switched in or out
// under a signal. On the way down the fade waits until the glide is under
// kFadeDrive01, so what fades out is already nearly clean.
constexpr float kFadeMs = 10.0f;
constexpr float kFadeDrive01 = 0.02f;

// ============================================================================
// PRE-EMPHASIS (ahead of the curve)
// ============================================================================
// First-order shelf (1 + s/zero) / (1 + s/pole): unity in the bass, lifting
// by pole/zero = +19.9 dB above it - so the top of the spectrum is what gets
// compressed hardest, and the bass passes through the curve more gently. The
// pole walks down onto the zero as the knob comes down, which is flat.
constexpr float kPreShelfZeroHz = 164.9f;
constexpr float kPreShelfPoleHz = 1618.0f;

// ============================================================================
// CURVE
// ============================================================================
// y = asinh (v) for v >= 0, -kNegScale * asinh (-v / kNegScale) below it,
// where v = gain * x + bias. Logarithmic, never flat: a louder input always
// comes out a little louder, which is what keeps the reference's harmonic
// balance mostly intact under that much gain reduction. The bias moves the
// operating point off the middle (even harmonics), scaled with the knob, and
// the curve's own offset at rest is subtracted so silence stays silent.
constexpr float kBias = -0.461f;
constexpr float kNegScale = 0.961f;

// ============================================================================
// DE-EMPHASIS (after the curve)
// ============================================================================
// First-order shelf the other way: unity in the bass, down by
// zero/pole = -15 dB above it. Together with the pre-emphasis it leaves the
// reference's net small-signal mid lift (+10.8 dB peak around 1-2 kHz),
// and it takes the edge off the harmonics the curve made from the lifted top.
constexpr float kPostShelfZeroHz = 1923.0f;
constexpr float kPostShelfPoleHz = 344.0f;

// DC blocker on the output (first-order high-pass). Fitted, not a safety
// margin: the bias thump after a note ends settles at this corner.
constexpr float kDcBlockerHz = 8.43f;

// ============================================================================
// LEVEL
// ============================================================================
// Drive changes the character, not the volume - the reference's own level was
// ignored for the match, and this is ours. Two parts (TubeDrive::updateDrive):
//
//   1. the curve's own small-signal gain - the gain into it times its slope at
//      the bias point - is divided back out, which is already exact while the
//      stage is near-linear low on the knob;
//   2. kLevelTrimDb makes up what that misses further up, the compression and
//      the shelves' net mid lift, measured on real playing: dist-dry.wav's RMS
//      in against out at every 10 % of the knob, linearly interpolated between.
//      The trim sits after the curve and ahead of the linear filters, so it
//      scales the output exactly and one measuring pass is enough -
//      `ee_bit_check <outDir> dist-dry.wav` prints what each entry should be.
//
// It holds on average playing, not at every level: the drive compresses, so
// quieter playing still comes out a little louder and harder playing a little
// quieter. That is the drive, not the knob.
//                                       0 %    10 %    20 %    30 %    40 %    50 %
constexpr std::array<float, 11> kLevelTrimDb { 0.00f, -1.63f, -2.14f, -2.18f, -1.82f, -1.12f,
                                               -0.18f, 0.95f, 2.19f, 3.52f, 4.89f };
//                                      60 %    70 %    80 %    90 %   100 %

// ============================================================================
// OVERSAMPLING
// ============================================================================
// The curve runs at 2x the host rate and is band-limited back down. The fit
// was done without it and put ~6 dB too much into the 16 kHz octave against
// the reference - folded harmonics the reference does not have.
constexpr int kOversampleFactor = 2;
constexpr float kOversampleCutoffHz = 19000.0f;

// ============================================================================
// KNOB -> UNIT
// ============================================================================

/** Drive knob (0..1) -> the 0..1 voicing position every stage is walked by. */
inline float voicingFor (float drive01) noexcept
{
    return std::pow (std::clamp (drive01, 0.0f, 1.0f), kDriveSkew);
}

/** Drive knob (0..1) -> kLevelTrimDb, linearly interpolated. */
inline float levelTrimDbFor (float drive01) noexcept
{
    const float pos = std::clamp (drive01, 0.0f, 1.0f) * static_cast<float> (kLevelTrimDb.size() - 1);
    const size_t i = std::min (static_cast<size_t> (pos), kLevelTrimDb.size() - 2);
    const float frac = pos - static_cast<float> (i);
    return kLevelTrimDb[i] + frac * (kLevelTrimDb[i + 1] - kLevelTrimDb[i]);
}

/** Drive knob (0..1) -> gain into the curve. */
inline float gainFor (float drive01) noexcept
{
    return kDriveMinGain * std::pow (kDriveMaxGain / kDriveMinGain, voicingFor (drive01));
}

} // namespace ee::dsp::tubedrive
