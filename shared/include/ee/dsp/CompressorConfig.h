#pragma once

#include <algorithm>
#include <cmath>

/**
 * Voicing for ee::dsp::Compressor - BitBit Artifact's Comp engine.
 *
 * The target is a Keeley Compressor: an OTA (CA3080) pedal descended from the
 * Ross / Dyna Comp. Two knobs - Sensitivity (the pedal's Sustain) and Attack
 * (the C4's; the two-knob original has it on a trimmer inside). There is no
 * Level knob: the output level follows the input's on its own (see AUTO
 * LEVEL), so turning Sensitivity changes how squeezed it is and not how loud.
 * Its Blend is the module's footer Mix and its Tone the footer Tone, as on
 * every Artifact engine.
 *
 * **FIRST SHOT, NOT FITTED.** Every number below is a behavioural guess at the
 * pedal from how the circuit works, not a measurement. The owner has the
 * two-knob pedal and is going to record it; when that lands, fit in this order
 * (each one only moves the ones after it a little):
 *
 *   1. The static curve - kThresholdDb, kRatioMin/Max, kKneeDb and the Sensitivity
 *      map - from stepped-level tone bursts, output level vs input level, at
 *      Sustain 0 / 50 / 100 %. Read the settled level at the end of each
 *      burst, and compare *shapes*: the pedal's Level knob sets its absolute
 *      output, which ours sets itself.
 *   2. The attack - the overshoot on a burst's onset (kAttackMinMs..Max).
 *   3. The release - the recovery after a burst's end: kReleaseFastMs is the
 *      first dB or two, kReleaseSlowMs the long tail after a held note.
 *   4. kOtaHeadroom - the gain cell's soft saturation, from the THD of a hot
 *      sine at full Sensitivity.
 *
 * What the circuit does, and what stands in for it here:
 *
 *   - Sensitivity (the pedal's Sustain) is the detector's sensitivity, which in
 *     effect is how much gain a quiet signal gets relative to a loud one.
 *     Modelled as a pre-gain (0..kSensitivityMaxDb) into a compressor with a
 *     fixed threshold. What gets louder or quieter overall is then undone by
 *     AUTO LEVEL below.
 *   - At Sensitivity 0 it is close to transparent on a normal guitar level:
 *     the knee only reaches a peak near 0 dBFS. The first cut (threshold -12,
 *     no make-up) was pulling a -6 dBFS-peak guitar down 4-5 dB there.
 *   - The OTA's gain law is soft and the loop is feedback, so there is no hard
 *     knee and the ratio is high but not infinite: kRatioMin..kRatioMax (see
 *     below) with a wide kKneeDb.
 *   - The release is a capacitor bleeding into a loop that keeps recharging it,
 *     which makes it program-dependent - a pick transient lets go quickly, a
 *     held chord slowly. Two release states (see Compressor.h) stand in for it.
 *   - The OTA's differential input clips as a tanh on hot peaks - the Dyna Comp
 *     "splat" on a hard pick before the detector catches up. Keeley tamed it;
 *     kOtaHeadroom keeps a trace of it.
 *
 * No added noise and no hiss - a real Dyna Comp brings its noise floor up by
 * the Sustain gain, and nobody wants that part.
 *
 * AUTO LEVEL. The owner's rule: the level with the compressor on is the level
 * with it off, at any Sensitivity. Two stages:
 *
 *   1. Computed, instant: make-up = -(pre-gain + curve (kRefDb + pre-gain)) -
 *      what a note at a typical guitar level would lose, given back. It moves
 *      with the Sensitivity knob and nothing else, so turning the knob changes
 *      how squeezed it is and not how loud, the moment you turn it.
 *   2. Measured, slow: the input's power and the compressed signal's, each
 *      averaged over kMatchMs (gated in silence), and their ratio slewed in over
 *      kTrimMs and applied as a trim, clamped to +-kMatchMaxDb. This is what
 *      makes it right for *your* guitar level and playing - stage 1 alone is
 *      within ~2 dB for a guitar near kRefDb, and plucks and held chords land
 *      ~4 dB apart, because they spend different time in the knee.
 *
 * Slow is the whole point of stage 2, and it was found the hard way:
 *   - A fast reference (30 ms) moved the make-up inside a note - a hard note
 *     after quiet playing raised its own make-up and swelled in.
 *   - A power average applied without the slew did the same, because a power
 *     average jumps within tens of ms of a loud note.
 *   - A reference following the player over ~1.5 s rose through a held chord
 *     and fell through its tail, cancelling the release bloom; the ~1 s loops
 *     tried after that did the same, and fought each other.
 * At kMatchMs / kTrimMs it follows how loud you play, not what a note does.
 * The cost: a big change of input level takes some seconds to be matched.
 *
 * Its own namespace, like ee::dsp::rust and ee::dsp::bitcrush, because every
 * header lands in the same test translation unit.
 */

namespace ee::dsp::comp
{

// ============================================================================
// STATIC CURVE
// ============================================================================
// The compressor proper, after Sensitivity's pre-gain: soft knee kKneeDb
// wide, centred on kThresholdDb, the ratio above it. The threshold the display
// draws on the *input* is kThresholdDb - the pre-gain (inputThresholdDbFor).
constexpr float kThresholdDb = -2.0f;
// Sensitivity moves the ratio as well as the pre-gain - geometrically from
// kRatioMin at 0 to kRatioMax at 1 (ratioForPreGainDb) - so one knob goes from
// open and gentle at the bottom (a soft 3:1 that levels without flattening)
// to a hard squash at the top (36:1, the owner's choice - was 20:1). The
// pre-gain is what brings the tails up (sustain); the ratio is how flat the
// loud notes are (squash); the owner asked for the knob to raise both. The
// ratio is derived from the *smoothed* pre-gain, so it glides with the knob.
constexpr float kRatioMin    = 3.0f;
constexpr float kRatioMax    = 36.0f;
constexpr float kKneeDb      = 12.0f;

// Sensitivity knob (0..1) -> pre-gain in dB, linear in dB. At 0 a guitar's
// peaks just reach the knee; at 1 it is squashing everything above the noise.
constexpr float kSensitivityMaxDb      = 30.0f;
constexpr float kDefaultSensitivityPct = 50.0f;

// ============================================================================
// DETECTOR
// ============================================================================
// A full-wave rectifier into a peak hold that lets go over kDetectorReleaseMs.
// Not the compressor's release - that is below - but the ripple filter ahead
// of it: without it the level falls to nothing at every zero crossing, and a
// low E's 12 ms cycle becomes a gain wobble you hear as distortion.
constexpr float kDetectorReleaseMs = 30.0f;

// The sidechain filter (the face's SC switch, on by default): a 2nd-order
// Butterworth high-pass on what the *detector* hears - the audio is never
// filtered. A guitar's low E and A carry most of its energy, so without it the
// bass strings set the gain for a whole chord and duck the treble strings with
// them - a pump on strummed chords. Library compressors almost all have one
// (Ableton's "SC Filter"); 120 Hz leaves the low E's fundamental ~7 dB down in
// the detector and everything from the D string up untouched.
constexpr float kSidechainHpfHz = 120.0f;

// ============================================================================
// BALLISTICS
// ============================================================================
// Attack knob, in ms, log-mapped across the knob. The C4's range from its
// fastest (the pick flattened) to its slowest (the pick comes through, then the
// note is pulled down).
constexpr float kAttackMinMs     = 1.0f;
constexpr float kAttackMaxMs     = 25.0f;
constexpr float kAttackCentreMs  = 5.0f; // the knob's middle, for the host's range skew
constexpr float kDefaultAttackMs = 5.0f;

// How far an onset may beat the gain reduction, in dB - kMaxOvershootMinDb at
// the fastest Attack up to kMaxOvershootMaxDb at the slowest, log-mapped like
// the knob (maxOvershootDbFor). The Attack knob sets how long a pick gets
// through for; this caps how loud it gets meanwhile. It scales with Attack
// because a fixed 3 dB cap all but cancelled the knob: measured, the pick came
// through 0.1 dB at 1 ms and only 2.2 dB at 25 ms, where the pedal's slow end
// lets the pick clearly through first. A
// note arriving after a pause meets the full pre-gain, and without a cap its
// first milliseconds came out 10 dB over the settled level at half Sustain -
// past 0 dBFS on an ordinary guitar. 6 dB still went over. On the pedal the
// OTA's own clipping is what stops it.
constexpr float kMaxOvershootMinDb = 3.0f;
constexpr float kMaxOvershootMaxDb = 9.0f;

// Program-dependent release: a fast state that follows every transient and
// lets go over kReleaseFastMs, and a slow state that only builds under
// sustained compression (kSlowAttackMs) and lets go over kReleaseSlowMs. The
// gain reduction is whichever is deeper - so a pick transient recovers fast,
// and a held chord swells back slowly, the Dyna Comp bloom.
constexpr float kReleaseFastMs = 80.0f;
constexpr float kSlowAttackMs  = 300.0f;
constexpr float kReleaseSlowMs = 700.0f;

// ============================================================================
// GAIN CELL
// ============================================================================
// The OTA's soft saturation: y = h * tanh (y / h) after the gain. At h = 2
// (+6 dBFS) a settled note, which sits near 0 dBFS at most, is barely
// touched; an onset that beats the attack is rounded rather than clipped.
constexpr float kOtaHeadroom = 2.0f;

// ============================================================================
// AUTO LEVEL (no knob - see the class note)
// ============================================================================
// Stage 1's typical guitar level: the detector (peak) level a note is matched
// at. Swept -9 / -15 / -21 on the plucked and held takes in ee_dsp_tests:
// -15 put all six takes within 0.5 dB at every Sensitivity once stage 2 had
// settled; -21 left a hot held chord 4.8 dB down, -9 a quiet pluck 2 dB up.
constexpr float kRefDb = -15.0f;

// Stage 2: the averaging time, how far it may trim, and how fast the trim
// itself may move. Gated below kGateDb, so a pause holds it rather than
// dragging it somewhere and letting the next note in loud.
constexpr float kMatchMs    = 8000.0f;
constexpr float kTrimMs     = 4000.0f;
// 18 rather than 12 since the sidechain filter: with the lows out of the
// detector, a bass-heavy held chord is compressed less than stage 1 (which
// cannot know a chord's spectrum) assumes, and a hot one needed more than
// 12 dB of trim - it sat 1.6 dB under bypass at full Sensitivity.
constexpr float kMatchMaxDb = 18.0f;
constexpr float kGateDb     = -50.0f;

// ============================================================================
// DISPLAY FEED
// ============================================================================
// One meter point every kMeterPointMs: the input's peak, the output's peak and
// the deepest gain reduction over that window. A ring of kMeterRing points is
// ~5 s of history, plenty for an editor that polls it at 30-60 Hz.
constexpr float kMeterPointMs = 5.0f;
constexpr int   kMeterRing    = 1024;

// ============================================================================
// MAPS
// ============================================================================
/** The onset cap for an Attack in ms - see kMaxOvershootMinDb. */
inline float maxOvershootDbFor (float attackMs) noexcept
{
    const float t = std::clamp (std::log (attackMs / kAttackMinMs) / std::log (kAttackMaxMs / kAttackMinMs), 0.0f, 1.0f);
    return kMaxOvershootMinDb + t * (kMaxOvershootMaxDb - kMaxOvershootMinDb);
}

inline float sensitivityDbFor (float sensitivity01) noexcept
{
    return std::clamp (sensitivity01, 0.0f, 1.0f) * kSensitivityMaxDb;
}

/** Where the curve's knee sits on the *input*, for the display's threshold
    line: the fixed threshold less the pre-gain. */
inline float inputThresholdDbFor (float sensitivity01) noexcept
{
    return kThresholdDb - sensitivityDbFor (sensitivity01);
}

/** The ratio at a pre-gain in dB - kRatioMin at 0, kRatioMax at
    kSensitivityMaxDb, geometric between. From the pre-gain rather than the
    knob so the engine can read it off its smoothed value. */
inline float ratioForPreGainDb (float preGainDb) noexcept
{
    const float t = std::clamp (preGainDb / kSensitivityMaxDb, 0.0f, 1.0f);
    return kRatioMin * std::pow (kRatioMax / kRatioMin, t);
}

/** Gain reduction in dB (<= 0) for a level in dB, after the pre-gain, at
    `ratio` (ratioForPreGainDb). */
inline float gainReductionDbFor (float levelDb, float ratio) noexcept
{
    const float over  = levelDb - kThresholdDb;
    const float slope = 1.0f / ratio - 1.0f;

    if (2.0f * over <= -kKneeDb)
        return 0.0f;

    if (2.0f * over >= kKneeDb)
        return slope * over;

    const float t = over + 0.5f * kKneeDb;
    return slope * t * t / (2.0f * kKneeDb);
}

/** Stage 1 of the auto level, in dB, for a pre-gain in dB - see AUTO LEVEL in
    the class note. */
inline float makeupDbFor (float preGainDb) noexcept
{
    return -(preGainDb + gainReductionDbFor (kRefDb + preGainDb, ratioForPreGainDb (preGainDb)));
}

} // namespace ee::dsp::comp
