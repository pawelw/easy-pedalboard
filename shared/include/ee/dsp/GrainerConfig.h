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

constexpr float kMinGrainSeconds = 0.020f;

// Raised from 0.5 s: synced, the Size knob can select a tempo division far
// longer than this, and every position past the cap produced the same grain -
// a dead band at the top of the travel (see PeakGrainProcessor::sizeReadout,
// which clamps its readout to exactly this for the same reason). 1.0 rather
// than longer because of the read-ahead above: a grain reads its own length
// times its playback rate of source, and pickRate() tops out near 3.2, so a
// 1 s grain spans 3.2 s. At the max-Time, max-Size, max-rate corner that
// leaves the read head just about level with the write head, which is where
// 0.5 s already sat; 2 s would send it through and read stale audio.
constexpr float kMaxGrainSeconds = 1.000f;

// Concurrent grains. At the top of the Density range with the longest grains
// the engine now wants density x size = 40 x 1.0 = 40 of them, which this pool
// deliberately does not cover: that corner is a drone of forty overlapping
// one-second grains, 32 of them is already indistinguishably dense, and the
// overlap normalisation divides the expected count out either way. Sizing for
// the corner would cost per-sample voice work on every patch to serve one.
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
constexpr float kMinDensityHz = 1.0f;
constexpr float kMaxDensityHz = 40.0f;
constexpr float kDensitySkewHz = 12.0f;
constexpr float kDefaultDensityHz = 12.0f;

// ============================================================================
// GRAIN SIZE
// ============================================================================
// Below ~40 ms the grains stop being fragments of the input and turn into a
// metallic buzz at the spawn rate; above ~300 ms you hear whole notes.
constexpr float kMinGrainMs = kMinGrainSeconds * 1000.0f;
constexpr float kMaxGrainMs = kMaxGrainSeconds * 1000.0f;
constexpr float kGrainSkewMs = 120.0f;
constexpr float kDefaultGrainMs = 120.0f;

// ============================================================================
// TIME
// ============================================================================
// The delay: how far behind the write head grains are tapped from. Skewed so
// the short, rhythmic end gets most of the travel; the top is a long wash.
// Grains are still scattered around this point by Scatter, so Time is the
// centre of the tap window rather than a single hard offset.
constexpr float kMinTimeMs = 20.0f;
constexpr float kMaxTimeMs = 2000.0f;
constexpr float kTimeSkewMs = 300.0f;
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
constexpr float kMaxFeedback = 0.92f;

// ============================================================================
// STRETCH  (frozen only)
// ============================================================================
// With Freeze engaged the read head scans the captured buffer at this rate,
// in multiples of realtime. The knob is bipolar: +1 scans forward at the
// speed it was recorded (the delay time holds steady), 0 holds the read head
// still (a stutter on one moment), -1 scans backwards. Pitch is untouched
// either way - only where the next grain is taken from moves.
constexpr float kStretchMax = 1.0f;
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

// Where in the note an attack-drawn grain actually starts. The anchor is the
// detected attack; these say how wide a window around it the grain may be
// taken from.
//
// This is not decoration, it is the difference between a cloud and a stutter.
// The offset an attack grain is placed at counts back from the write head by
// `sinceAttack`, which advances in lockstep with the write head - so without
// a spread every attack grain starts at *exactly* the same sample of the
// recording. Overlapping copies of the same audio comb-filter against each
// other and repeat at the spawn rate, which reads as metallic and machine-gun
// rather than as a cloud.
//
// kAttackJitterMs is the window at Scatter 0 (narrow, so that end of the knob
// still sounds like the transient it was before this existed) and Scatter
// widens it by up to kAttackSpreadMs, running *forward* from the attack into
// the body of the note - that direction because a grain placed well before
// the attack spends its loud opening on whatever came before it and meets the
// transient with its envelope already closing. kAttackPreRollMs is the small
// amount of reach back the other way, so some grains open just before the hit
// and carry the transient itself rather than starting on top of it.
constexpr float kAttackJitterMs  = 12.0f;
constexpr float kAttackSpreadMs  = 260.0f;
constexpr float kAttackPreRollMs = 6.0f;

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
constexpr float kMinWindowSeconds = 0.2f;
constexpr float kMaxWindowSeconds = 6.0f;
constexpr float kWindowSkewSeconds = 3.5f;
constexpr float kDefaultWindow01 = 0.5f;
constexpr bool kDefaultWindowSync = false;

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
// MONO / STEREO  (Haas width on the grain cloud)
// ============================================================================
// The header switch beside Live/Freeze. Mono leaves the cloud exactly as the engine and Drive
// made it (Random's Stereo knob still pans grains). Stereo runs it through
// ee::dsp::HaasWidener: the side channel gets the mid back 6.83 ms late, which
// widens the image and cancels exactly in a mono fold-down. Delay and width
// are the ones measured on the reference grainer this was modelled on (side
// gain ~0.9 there; full width here, as asked for). Cloud only - the dry path
// never reaches it.
constexpr float kHaasDelayMs = 6.83f;
constexpr float kHaasWidth = 1.0f;
constexpr float kHaasRampMs = 20.0f;
constexpr bool kDefaultStereoWidth = false;

// ============================================================================
// GRID  (grain read points on the tempo grid)
// ============================================================================
// Not a switch: always on whenever the host transport is rolling (the
// processor sets Transport::grid from that alone; standalone or stopped, the
// engine behaves as it did before Grid existed). It keeps where grains read
// from on sixteenth-note boundaries, so every fragment starts on a beat of the
// source as well as landing on one.
//
// FROZEN - bar-locked capture. The held slice is re-captured on every bar line
// instead of on loud onsets, and every grain for the rest of that bar replays
// it: a beat-repeat locked to the bar rather than to whichever note happened
// to be loud. Recording keeps running underneath, so the next bar always has
// its downbeat to take. Grains start from a sixteenth counted from the bar
// line and Scatter picks which: at 0 every grain takes the downbeat; wound up,
// one of the first kGridMaxSlices + 1 sixteenths (Scatter 25 %, the default,
// is an even split between the downbeat and the one after it). A slice not
// yet played - a grain right on the bar line - is read as late as is legal,
// i.e. it plays the downbeat live. Time, Window, Stretch and the attack
// detector do not apply.
//
// LIVE - a rhythmic granular delay. The Time tap is rounded to whole
// sixteenths (never under one, so a grain is always a repeat rather than the
// input itself), and Scatter moves it by up to kGridMaxSlices sixteenths either
// side - Scatter 25 % already lands half the grains one sixteenth early or
// late, 100 % anywhere within four. It is a count of sixteenths rather than a
// share of Time, so the knob does the same at every tempo. Attack-drawn grains are deliberately left alone: they follow
// where the note was actually played, and snapping them would clip the pick
// or quantize away the player's feel.
constexpr int kGridMaxSlices = 4;

// A synced spawn can land a few samples either side of the bar line while the
// spawn phase eases onto the host grid. A grain this close before the line
// counts as belonging to the new bar, rather than replaying the old bar's
// downbeat for one grain.
constexpr float kGridBarSnapMs = 5.0f;

// ============================================================================
// REVERSE, STEREO
// ============================================================================
// Share of grains that play backwards. Forward-only is much more legible; past
// halfway the phrase stops being followable at all.
constexpr float kDefaultReversePct = 25.0f;

// Width of the random pan placement. 0 puts every grain in the centre, 100
// throws them hard left and right. Equal-power, so the middle does not dip.
constexpr float kDefaultStereoPct = 85.0f;

// ============================================================================
// SCALE  (what the Low/High pitch groups draw their intervals from)
// ============================================================================
// There is no pitch tracking anywhere in this engine - it has no idea what
// note is actually playing - so this cannot be a real harmoniser locked to
// the input. What it can do is the trick most granular and harmoniser boxes
// fall back to without pitch detection: quantize the transposition to a
// chosen scale, measured from unison (an untransposed grain) as if unison
// were that scale's root. Root then rotates the pattern - set it to the
// actual key of the material and the notes land harmonically related to it,
// dialled in by ear rather than detected.
//
// Only the High group draws from the scale, and it is anchored an octave up
// (+12 and above - see Grainer::setScale). Low is an octave down and nothing
// else. Both groups used to draw from the whole range either side of unison,
// which sounds like nothing at all: most of the candidates sat a semitone or
// two off the note, so neither knob audibly did anything. A group has to be
// plainly higher or lower than the note to read as a group.
//
// This replaces what used to be Detune (a continuous +/-7 semitone wobble
// added on top of a fixed, hidden interval table) - see Grainer::setScale()
// and pickRate() for where these get used.
constexpr int kScaleMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
constexpr int kScaleMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
constexpr int kScalePentatonicMajor[] = { 0, 2, 4, 7, 9 };
constexpr int kScalePentatonicMinor[] = { 0, 3, 5, 7, 10 };
constexpr int kScaleChromatic[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

struct ScaleDegrees
{
    const int* degrees;
    int count;
};

constexpr ScaleDegrees kScales[] = {
    { kScaleMajor, 7 },           { kScaleMinor, 7 },      { kScalePentatonicMajor, 5 },
    { kScalePentatonicMinor, 5 }, { kScaleChromatic, 12 },
};
constexpr int kNumScales = 5;

// Same bound the old fixed interval table respected - a grain playing back
// faster than this spans more source than the buffer guarantees (see
// Grainer's own kMaxRate note).
constexpr int kMaxScaleSemitones = 19;

constexpr int kDefaultScaleIndex = 0;   // Major
constexpr int kDefaultRootSemitone = 0; // C

// ============================================================================
// PITCH
// ============================================================================
// Three weights rather than one bipolar knob, so a cloud can carry octaves
// below, the root, and fifths above all at once. Each grain picks one of the
// three at random in proportion to these, then an interval from that group's
// table. All three at zero is treated as unison only - a face with no pitch
// dialled in should still make a sound.
constexpr float kDefaultPitchLowPct = 0.0f;
constexpr float kDefaultPitchUnisonPct = 100.0f;
constexpr float kDefaultPitchHighPct = 0.0f;

// How much of the scale reaches the grain cloud: 0 sends the High group back
// to the original pitch, 100 lets it land on whatever notes Root and Scale
// pick out. Low's octaves are not affected - an octave is consonant whatever
// the key is - see PeakGrainProcessor::processBlock's own blend.
constexpr float kDefaultPitchMixPct = 100.0f;

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
constexpr float kDefaultDelayTime01 = 0.357f; // ~1/8 when synced, ~150 ms free
constexpr float kDefaultDelayFeedbackPct = 30.0f;
constexpr float kDefaultDelayMixPct = 30.0f;
constexpr bool kDefaultDelaySync = true;

// ============================================================================
// REVERB
// ============================================================================
// Peak Grain runs ee::dsp::FdnReverb plain: the two knobs are its decay (in
// seconds, straight onto the network) and its mix, and everything else is pinned
// here. No shimmer - the header states 0 means the pitch shifters never run, so
// it costs nothing. The reverb now hears the whole post-delay blend rather than
// a grain-only send.
constexpr float kVerbShimmer = 0.0f;

constexpr float kDefaultReverbDecaySeconds = 2.5f;
constexpr float kDefaultReverbMixPct = 30.0f;

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
constexpr float kDefaultSize01 = 0.5f;
constexpr float kDefaultDensity01 = 0.5f;
constexpr bool kDefaultSizeSync = false;
constexpr bool kDefaultDensitySync = false;

// How hard the grain-spawn phase is pulled back onto the host grid, per block,
// while Density is synced and playing. Same numbers and the same reasoning as
// ee::dsp::tremolo's pull/snap pair (TremoloConfig.h) - a fraction of the
// error rather than the whole of it, and capped, so host ppq jitter re-settles
// without a click; a jump bigger than kSpawnJumpPpq snaps instead of easing.
constexpr double kSpawnPhasePullFraction = 0.15;
constexpr double kSpawnPhasePullMax = 0.006;
constexpr double kSpawnJumpPpq = 0.25;

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
// came out under 15 cents at its peak and inaudible under Scatter -
// the first cut at this knob had exactly that bug.
//
// Sampled once per grain at spawn instead (frozen for its life, like the
// scale interval pickRate() picks), not reapplied every sample: every grain
// spawned near the same point in the shared cycle bends the same way, so the
// whole cloud's pitch rises and falls together over one kModWowHz cycle -
// audible drift, not per-grain jitter. The scale pick stays the independent
// per-grain choice; Mod is what moves the cloud as one voice, same as
// TapeDelay's Mod moves its one tap.
constexpr float kModWowHz = 0.42f;
constexpr float kModMaxCents = 40.0f;
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
constexpr float kBitCrushRateHz = 1500.0f;
constexpr float kBitCrushRateSkew = 1.1f;
constexpr float kDefaultBitPct = 0.0f;

/** Bit knob (0..1) -> the sample-and-hold hold count at `sampleRate`. 0 always
    returns 1 (hold every sample = pass-through), whatever the sample rate -
    see the class note above. */
inline int bitHoldNFor (float bit01, double sampleRate) noexcept
{
    const double host = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double floorHz = std::min (static_cast<double> (kBitCrushRateHz), host);
    const double t =
        std::pow (std::clamp (static_cast<double> (bit01), 0.0, 1.0), static_cast<double> (kBitCrushRateSkew));
    const double rateHz = host * std::pow (floorHz / host, t);
    return std::max (1, static_cast<int> (std::lround (host / std::max (1.0, rateHz))));
}

// ============================================================================
// MIX
// ============================================================================
// Two faders rather than one crossfade: the dry path and the cloud are set
// against each other freely, which a crossfade cannot do (it has no position
// meaning "all of both"). Grains rests at 71 % because that is where the old
// equal-power crossfade sat at its own halfway point - cos/sin of 45 degrees -
// so a face opened at the defaults sounds the way it always did.
// The two mixer faders are gain in disguise: they read in decibels, and the
// percentage is only how far up the track the thumb sits. Unity is deliberately
// not at the top - it sits at kLevelUnityPct so there is somewhere to go when a
// part needs lifting, with kLevelBoostDb of headroom above it. See
// levelGainFor() in the processor for the one power curve that joins the two:
// silence at 0 %, exactly unity at kLevelUnityPct, exactly +kLevelBoostDb at
// 100 %.
constexpr float kLevelUnityPct = 75.0f;
constexpr float kLevelBoostDb = 6.0f;

// Dry rests at unity, so an untouched instance passes the input through at
// exactly the level it arrived. Grains rests a hair under, at -3 dB - the
// level the old 71 % linear fader sat at, so the pedal sounds as it always did.
constexpr float kDefaultDryLevelPct = 75.0f;
constexpr float kDefaultGrainLevelPct = 65.0f;

// ============================================================================
// CLOUD FILTER
// ============================================================================
// Two one-poles run once per sample on the summed cloud (not per grain - that
// would cost CPU per voice for a difference nobody asks to hear on this face).
// They do quite different jobs, and only one of them is on the face.
//
// The lowpass IS the Filter knob: a plain cutoff, resting wide open at the
// corner below and closing to kCloudLowpassMinHz. 6 dB/oct, no resonance -
// gentle by choice, not a ladder. Even wide open it still takes the edge off
// the extra high-frequency content a pitched-up or bit-crushed grain adds
// that the source never had.
//
// The highpass is hidden and fixed: nothing on the face moves it. It bleeds
// off the DC and rumble that a short grain envelope's asymmetry and the
// feedback recirculation both accumulate, and it keeps the cloud tight
// underneath the dry signal instead of letting it silt up the low mids. It
// sat at 60 Hz while it shared the knob with the lowpass, which is low enough
// to catch only genuine rumble; at 110 it also trims the mud a dense cloud
// builds, while staying well under anything a grain carries as real low end.
constexpr float kCloudHighpassHz = 110.0f;
constexpr float kCloudLowpassHz = 13000.0f;

// The shut end of the Filter knob's travel - see Grainer::setCloudFilter,
// which walks the cutoff down to here geometrically. The open end is
// kCloudLowpassHz above and the knob rests there, so a face that never
// touches this sounds exactly as it did before the knob existed.
constexpr float kCloudLowpassMinHz = 320.0f;

// Both stages have their own state now, so the cloud can go on decaying for a
// moment after the engine itself has stopped feeding them anything. The
// highpass is the slower of the two (its pole sits closer to 1): from its
// 110 Hz corner its state needs ~2,900 samples (~0.07 s, independent of sample
// rate to a first order) to decay past cloudHighpass()'s denormal squelch -
// see that function's own note on why the squelch exists at all. That is half
// what the old 60 Hz corner needed, so this figure is now a generous
// over-estimate rather than a tight one; getTailSeconds() adds it on top of
// the engine's own estimate so the figure a host trims to stays honest.
constexpr float kCloudFilterSettleSeconds = 0.4f;

// ============================================================================
// OUTPUT LIMITER
// ============================================================================
// A grain landing back on top of the transient that spawned it - a doubled
// kick, say - can sum past what the dry signal alone ever reached, and the
// grain/delay/reverb sends can push the same way. Not something the Level
// knob can see coming, so ee::dsp::PeakLimiter runs always-on at the very end
// of the chain as a safety net, not a face control. The attack is sub-sample
// fast on purpose - PeakLimiter has no lookahead, so anything slower lets the
// leading edge of exactly the transient this exists for through mostly
// unchecked (verified empirically: 1 ms let a stacked kick through 4 dB over
// ceiling; 0.02 ms holds it within a few hundredths of a dB). The 60 ms
// release is the other half of that asymmetry - slow enough to stay out of
// the way of the granular texture itself rather than pumping with it.
constexpr float kLimiterCeilingDb = -0.3f;
constexpr float kLimiterAttackMs = 0.02f;
constexpr float kLimiterReleaseMs = 60.0f;

} // namespace ee::dsp::config
