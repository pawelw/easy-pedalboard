#pragma once

#include <algorithm>
#include <cmath>

/**
 * Voicing for ee::dsp::Grainer, the granular delay behind Peak Grain.
 *
 * This file holds the structural side: the knob ranges, the buffer the engine
 * records into, and how many grains may sound at once. Changing one of these
 * changes what the knobs can ask for.
 *
 * The *voicing* - how many grains run backwards, where they land across the
 * image, how the grain envelope morphs, which intervals the pitched ones snap
 * to - lives in GrainerTuning.h instead, because the development tuning panel
 * drives those live (-DEE_GRAIN_TUNER=ON).
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
// How much of the past the engine keeps. This has to cover the longest Time,
// plus one longest grain of output, plus the source that grain spans at the
// fastest playback rate (an octave up eats two source samples per output
// sample), plus a guard. Everything below is bounded by these, and the figure
// is deliberately loose enough that a backwards grain drawn from the far end of
// the Time window still fits without being clamped back.
constexpr float kGrainBufferSeconds = 10.5f;

constexpr float kMinGrainSeconds    = 0.020f;
constexpr float kMaxGrainSeconds    = 0.500f;

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
// TIME
// ============================================================================
// The delay: how far behind the write head grains are tapped from. Skewed so
// the short, rhythmic end gets most of the travel; the top is a long wash.
// Grains are still scattered around this point by Scatter, so Time is the
// centre of the tap window rather than a single hard offset.
constexpr float kMinTimeMs     = 20.0f;
constexpr float kMaxTimeMs     = 2000.0f;
constexpr float kTimeSkewMs    = 300.0f;
constexpr float kDefaultTimeMs = 300.0f;

// ============================================================================
// FEEDBACK
// ============================================================================
// Share of the granulated output written back into the buffer, so each repeat
// is granulated again on its way round. Hard-capped below unity: the path is no
// longer feed-forward and a gain of 1 would let a stuck level or a denormal
// build without bound.
//   0.30 = a couple of audible repeats                       <-- default
//   0.92 = a long, self-thickening wash (the ceiling)
constexpr float kDefaultFeedbackPct = 30.0f;
constexpr float kMaxFeedback        = 0.92f;

// ============================================================================
// STRETCH  (frozen only)
// ============================================================================
// With Freeze engaged the read head scans the captured buffer at this rate,
// in multiples of realtime. The knob is bipolar: +1 scans forward at the
// speed it was recorded (the delay time holds steady), 0 holds the read head
// still (a stutter on one moment), -1 scans backwards. Pitch is untouched
// either way - only where the next grain is taken from moves.
constexpr float kStretchMax        = 1.0f;
constexpr float kDefaultStretchPct = 0.0f;

// ============================================================================
// SHAPE
// ============================================================================
// Morphs the grain envelope between the two ends the tuning header names:
//   0   = soft - a long fade-in, energy spread the whole grain
//   100 = plucky - a click of an attack, most of the energy up front
// A symmetric window is deliberately not on the travel: it throws the
// transient away and a plucked note comes back sounding reversed.
constexpr float kDefaultShapePct = 55.0f;

// ============================================================================
// SCATTER
// ============================================================================
// One knob over all the timing randomness: how much the gap between grains
// wanders, and how much each grain's length strays from Size. 0 is a metronome
// spraying identical grains; wound up the cloud stops repeating.
constexpr float kDefaultScatterPct = 25.0f;

// ============================================================================
// ATTACK
// ============================================================================
// A plucked string is mostly its first fifty milliseconds, and a cloud built
// from the sustain alone loses whatever made the note identifiable. An onset
// detector marks where each attack landed in the recording, and this share of
// grains is drawn from there rather than from a random point in the Time
// window. Live only: a frozen buffer plays from wherever Stretch has the read
// head, attack or not.
//   0.00 = every grain placed by Time and Scatter alone
//   0.70 = the attack is the voice of the cloud            <-- default
//   1.00 = nothing but the attack, over and over
constexpr float kDefaultAttackShare = 0.70f;

// How far into the attack a grain may start, in milliseconds - a little spread
// so the repeats are not all bit-identical.
constexpr float kAttackJitterMs = 12.0f;

// How long after an attack grains may still be drawn from it. Past this the note
// has rung out and the cloud moves on to whatever the Time window currently
// holds, so a long silence really does fall silent. The Window knob (below)
// is what actually sets this at runtime - this is just what a fresh
// ee::dsp::Grainer starts at before anything calls setAttackReachSeconds.
constexpr float kAttackReachSeconds = 3.5f;

// ============================================================================
// WINDOW  (attack reach, live)
// ============================================================================
// Window is the Attack Reach above on a knob: one normalised value with a
// Sync switch beside it, the same "duration, GrainSyncMap" shape as Size -
// how long the cloud keeps drawing from the struck note before it moves on
// to whatever Time and Scatter are currently offering. Short is a clean
// single pass; long is a cloud that keeps re-singing the same attack for
// several seconds. The skew centres the knob's middle on the figure
// kAttackReachSeconds used to be fixed at, so the default position sounds
// the same as the old always-3.5s behaviour.
constexpr float kMinWindowSeconds  = 0.2f;
constexpr float kMaxWindowSeconds  = 6.0f;
constexpr float kWindowSkewSeconds = 3.5f;
constexpr float kDefaultWindow01   = 0.5f;
constexpr bool kDefaultWindowSync  = false;

// When a loud input retriggers a frozen buffer, how much fresh audio is
// captured before it re-freezes and loops again - a Time window plus a grain,
// floored so a very short Time still grabs something to work with.
constexpr float kMinRecaptureSeconds = 0.5f;

// The longest stretch of the buffer a plain Freeze loops. The read head scans
// this window and wraps inside it, so Stretch never runs off the recorded
// audio into the unwritten tail of the buffer. A retrigger loops only what it
// just captured instead.
constexpr float kFreezeLoopSeconds = 3.0f;

// ============================================================================
// REVERSE, STEREO, DETUNE
// ============================================================================
// Share of grains that play backwards. Forward-only is much more legible; past
// halfway the phrase stops being followable at all.
constexpr float kDefaultReversePct = 25.0f;

// Width of the random pan placement. 0 puts every grain in the centre, 100
// throws them hard left and right. Equal-power, so the middle does not dip.
constexpr float kDefaultStereoPct = 85.0f;

// Random detune on every grain, in semitones either way - half tones up or
// down from the played note, not a cents-scale chorus wobble. Bipolar knob,
// -7..+7 (up to a fifth either side): Grainer::process() already draws its
// own random sign per grain (nextBipolar() * detuneSemitones), so the two
// halves of the knob's travel are audibly symmetric by construction - what
// the sign buys is a centred rest position at 0 rather than a magnitude knob
// starting at one end of its travel, matching the face's bipolar cap (Grain
// Knob.dc.html's bipolar="true"). Linear, not skewed: whole semitones are
// already coarse enough that skewing toward the centre would just waste
// travel.
constexpr float kMinDetuneSemitones     = -7.0f;
constexpr float kMaxDetuneSemitones     = 7.0f;
// Zero by default: at anything above it every grain can land on a different
// note, which reads as a scattered chord rather than the note that was
// played. Dial it in deliberately.
constexpr float kDefaultDetuneSemitones = 0.0f;

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
// POST DELAY
// ============================================================================
// A plain digital delay (ee::dsp::TapeDelay with modulation pinned at 0) sitting
// after the grain stage and before the reverb, so the pedal sounds like the
// grains fed an outboard delay into an outboard reverb. Its Time knob is one
// normalised control whose Sync switch flips it between free milliseconds - over
// the same span the granular Time uses - and a note division. Feedback is capped
// well below unity for the usual runaway reasons.
constexpr float kDefaultDelayTime01     = 0.357f; // ~1/8 when synced, ~150 ms free
constexpr float kDefaultDelayFeedbackPct = 30.0f;
constexpr float kDefaultDelayMixPct      = 30.0f;
constexpr bool  kDefaultDelaySync        = true;

// ============================================================================
// REVERB
// ============================================================================
// Peak Grain runs ee::dsp::FdnReverb plain: the two knobs are its decay (in
// seconds, straight onto the network) and its mix, and everything else is pinned
// here. No shimmer - the header states 0 means the pitch shifters never run, so
// it costs nothing. The reverb now hears the whole post-delay blend rather than
// a grain-only send.
constexpr float kVerbShimmer   = 0.0f;

constexpr float kDefaultReverbDecaySeconds = 2.5f;
constexpr float kDefaultReverbMixPct       = 30.0f;

// Low Cut is a real face knob now (it used to be GrainerTuning::verbLowCutHz,
// fixed and never exposed); this is that same resting point, kept here as the
// parameter's default so the two stay obviously in step.
constexpr float kDefaultReverbLoCutHz = 120.0f;

// ============================================================================
// GRAIN SIZE / DENSITY SYNC
// ============================================================================
// Size and Density are normalised 0..1 knobs (see GrainSyncMap): the Sync switch
// on each flips it between its free unit - milliseconds for Size, grains per
// second for Density - and a note division. 0.5 is the middle of each skewed
// free range, i.e. the old kDefaultGrainMs / kDefaultDensityHz landing spots.
constexpr float kDefaultSize01    = 0.5f;
constexpr float kDefaultDensity01 = 0.5f;
constexpr bool  kDefaultSizeSync    = false;
constexpr bool  kDefaultDensitySync = false;

// How hard the grain-spawn phase is pulled back onto the host grid, per block,
// while Density is synced and playing. Same numbers and the same reasoning as
// ee::dsp::tremolo's pull/snap pair (TremoloConfig.h) - a fraction of the
// error rather than the whole of it, and capped, so host ppq jitter re-settles
// without a click; a jump bigger than kSpawnJumpPpq snaps instead of easing.
constexpr double kSpawnPhasePullFraction = 0.15;
constexpr double kSpawnPhasePullMax      = 0.006;
constexpr double kSpawnJumpPpq           = 0.25;

// ============================================================================
// MOD  (per-grain drift)
// ============================================================================
// The same slow sine ee::dsp::TapeDelay's own Mod knob rides
// (shared/src/dsp/TapeDelay.cpp's kWowHz, pinned at 0 for Grain's own
// post-delay stage - see PluginProcessor.cpp's `delay.setModulation
// (0.0f)`), reused here as a *pitch* drift rather than a read-position
// wobble. TapeDelay modulates one continuously-playing tap, so a few
// milliseconds of position wobble is an audible vibrato over its whole
// ringing tail; a grain lasts at most half a second and only ever samples a
// sliver of one slow (~2.4 s) cycle, so the equivalent position wobble here
// came out under 15 cents at its peak and inaudible under Scatter/Detune -
// the first cut at this knob had exactly that bug.
//
// Sampled once per grain at spawn instead (frozen for its life, like
// Detune - see pickRate()), not reapplied every sample: every grain spawned
// near the same point in the shared cycle bends the same way, so the whole
// cloud's pitch rises and falls together over one kModWowHz cycle - audible
// drift, not per-grain jitter. Detune stays the independent per-grain
// scatter; Mod is what moves the cloud as one voice, same as TapeDelay's Mod
// moves its one tap.
constexpr float kModWowHz      = 0.42f;
constexpr float kModMaxCents   = 40.0f;
constexpr float kDefaultModPct = 0.0f;

// ============================================================================
// BIT  (per-grain crush)
// ============================================================================
// Peak Artifact's Amp engine's own Bit knob (ee::fx::ArtifactModule::
// ampRateHzFor) - a sample-and-hold rate reducer only, no bit-depth
// quantisation and no anti-alias filter (that class's own note: the reference
// unit it was measured against holds full amplitude resolution). Restated
// here rather than included: ee::dsp must not depend on ee::fx, which is
// built on top of it. Each active grain holds independently rather than
// sharing one clock, so overlapping grains land on different hold phases
// instead of crushing in lockstep. bitHoldNFor() is the same formula as
// ampRateHzFor() plus the last step (Hz -> an integer hold count), so a
// render can be checked against Amp's own numbers directly - 32 at the top of
// the knob's travel, 5 at half, both at 48 kHz.
constexpr float kBitCrushRateHz   = 1500.0f;
constexpr float kBitCrushRateSkew = 1.1f;
constexpr float kDefaultBitPct    = 0.0f;

/** Bit knob (0..1) -> the sample-and-hold hold count at `sampleRate`. 0 always
    returns 1 (hold every sample = pass-through), whatever the sample rate -
    see the class note above. */
inline int bitHoldNFor (float bit01, double sampleRate) noexcept
{
    const double host = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double floorHz = std::min (static_cast<double> (kBitCrushRateHz), host);
    const double t = std::pow (std::clamp (static_cast<double> (bit01), 0.0, 1.0),
                               static_cast<double> (kBitCrushRateSkew));
    const double rateHz = host * std::pow (floorHz / host, t);
    return std::max (1, static_cast<int> (std::lround (host / std::max (1.0, rateHz))));
}

// ============================================================================
// MIX
// ============================================================================
// Prefixed because ee::dsp::config is one flat namespace shared by every
// engine, and Chorus already owns the plain kDefaultMixPct.
constexpr float kDefaultGrainMixPct = 50.0f;

} // namespace ee::dsp::config
