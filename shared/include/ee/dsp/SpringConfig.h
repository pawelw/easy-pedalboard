#pragma once

/**
 * Voicing for ee::dsp::SpringReverb.
 *
 * Peak Spring is a two-knob pedal - Decay and Mix - so everything that gives it
 * its character (how many springs, how long they are, how hard they chirp, how
 * the tank is filtered) is fixed here. The two knobs only ever scale what this
 * file sets up.
 *
 * Its own namespace (ee::dsp::spring) rather than the shared ee::dsp::config,
 * because it reuses names like kHighCornerHz that the plate voicing also
 * defines and both headers land in the same test translation unit.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::spring
{

// ============================================================================
// SPRINGS
// ============================================================================
// How many springs the tank is strung with. A real two-spring tank sounds
// noticeably more "one note" than a three; more than three and the individual
// boings smear into a plate. Three is the Accutronics long-decay tank.
//   2 = boingier, more obviously a single spring
//   3 = the classic tank  <-- default
constexpr int kSprings = 3;

// ============================================================================
// SPRING LENGTHS
// ============================================================================
// Round-trip time of each spring, in milliseconds, INCLUDING the chirp chain
// below - the delay line is shortened by whatever the chain already costs. The
// spacing between them sets how much the three answer each other; make them
// close and the tank rings on one note, spread them and it washes out.
//
// Deliberately not in simple ratios: a 2:3 pair reinforces every other bounce
// and turns the tail into a flutter.
constexpr float kSpringMs[kSprings] = { 38.6f, 44.9f, 51.7f };

// Right-hand tank, as a multiple of those lengths. A real tank is a mono
// device; the stereo image here is two tanks of very slightly different
// springs, which is enough to open the tail up without either side sounding
// detuned from the other.
//   1.000 = mono (both sides identical)
//   1.031 = wide but still one tank  <-- default
constexpr float kRightTankRatio = 1.031f;

// How much of each tank is crossfed into the other side's output. Two
// independent tanks come out uncorrelated, which sounds wide but hollow and
// collapses in mono; a real stereo spring send is nearly correlated. The
// measured target is +0.87, and the correlation this produces is 2c/(1+c^2).
//   0.00 = two separate tanks, correlation 0
//   0.52 = correlation +0.87, the reference  <-- default
//   1.00 = fully mono
constexpr float kStereoCrossfeed = 0.52f;

// ============================================================================
// CHIRP
// ============================================================================
// The dispersion inside each spring's feedback loop: a cascade of stretched
// all-pass sections, so high frequencies take longer round the loop than low
// ones. This IS the spring sound - a transient goes in and comes back as a
// rising "boinngg" that stretches further on every bounce. Take it out and the
// pedal is a short, dull delay.
//
// Stage count sets how far the chirp sweeps; the delay sets its pitch centre.
//   8  = a light metallic edge
//   12 = surf tank  <-- default
//   20 = a dustbin lid
constexpr int kChirpStages = 12;

// Nominal delay of one all-pass section, in milliseconds. The chain also
// repeats at its own period, which is the tank's metallic ring; kChirpSpread
// staggers the sections either side of the nominal so that ring lands on a
// band rather than a single pitch.
//
// This one was swept against the reference rather than reasoned about: the
// chain's period lands the tank's own peaks and troughs, and 0.30 put ours
// nearest the reference's. The error surface is bumpy - 0.42 and 0.54 are both
// visibly worse, 0.75 and 1.00 nearly as good - so treat it as one setting
// that happened to line up, not as a law.
constexpr float kChirpDelayMs = 0.30f;
constexpr float kChirpSpread  = 0.36f;   // +/- this fraction of kChirpDelayMs

// All-pass coefficient. NEGATIVE on purpose: it puts the longest group delay
// at the top of the spectrum, so the chirp sweeps upward the way a real spring
// does. Flip the sign and the boing falls instead of rising.
//   -0.45 = soft, nearly a plain delay
//   -0.62 = surf tank  <-- default
//   -0.78 = extreme, almost a pitch sweep
constexpr float kChirpCoefficient = -0.62f;

// ============================================================================
// LOOP FILTERING
// ============================================================================
// What the wire loses on every trip - shelves rather than rolloffs, through
// the shared LoopDamper, the same absorber the plate reverb uses. A rolloff
// keeps eating the same band on every pass, which collapses the top of the
// tail; a shelf holds a fixed ratio.
//
// The four numbers below are not guesses. They were fitted to the reference
// tank's own band decay times, measured off two renders of it (decay 3.58 s
// and 8.00 s), by turning each band's T60 into the per-trip loop gain that
// would produce it. What that says about the reference is that its loop is
// nearly flat right across the midrange and drops sharply below it - it loses
// around 4 % per trip through the ring band and only a little more at 4 kHz,
// but an order of magnitude more under 200 Hz.
//
// The first pass here used a one-pole low-pass at 4 kHz instead, which loses
// 28 % per trip at 4 kHz. That killed the top of the tail three trips in and
// was the single biggest error in the first voicing.
//
// Both ratios are fractions of the mid-band decay and are clamped to 1, so no
// band can ever ring longer than the decay knob says.
constexpr float kLowDecayRatio = 0.09f;    // below kLowCornerHz
constexpr float kHighDecayRatio = 0.74f;   // above kHighCornerHz
constexpr float kLowCornerHz   = 198.0f;
constexpr float kHighCornerHz  = 5760.0f;

// ============================================================================
// DRIVE FILTERING
// ============================================================================
// The transducer that shakes the springs, as a band-pass on the way in. Its
// narrowness is most of why a spring tank sits behind a guitar instead of on
// top of it.
constexpr float kInputLowCutHz  = 60.0f;
constexpr float kInputHighCutHz = 5500.0f;

// The pickup at the far end, on the wet output only - outside every feedback
// path, so it colours what you hear and nothing else.
constexpr float kOutputLowCutHz  = 60.0f;
constexpr float kOutputHighCutHz = 6000.0f;

// Body, as a shelf on that finished output. The reference tank carries far
// more weight under 250 Hz than the loop alone can account for, but its low
// end still dies quickly - so this lifts the level without touching the decay,
// which is the one thing a shelf inside the loop could not do.
//   1.0 = flat
//   1.9 = +5.6 dB of low shelf, the reference  <-- default
constexpr float kWetLowShelfGain = 1.9f;
constexpr float kWetLowShelfHz   = 260.0f;

// ============================================================================
// MODULATION
// ============================================================================
// A very slow, very small wander on each spring's length. Springs are never
// perfectly still, and without this the tank's comb sits at exactly one set of
// frequencies and rings like a resonator.
//   0.000 = static comb, metallic
//   0.0009 = alive  <-- default
//   0.005 = audibly chorused, no longer a spring
constexpr float kModDepth      = 0.0009f;   // fraction of the spring length
constexpr float kModRateHz     = 0.31f;     // first spring
constexpr float kModRateSpread = 0.37f;     // each further spring is this much faster

// ============================================================================
// DECAY
// ============================================================================
// Range of the Decay knob, in seconds of RT60, and where the skew puts the
// centre of its travel. A real long-decay tank is around 2 s; the top of this
// range is longer than any tank ever built, which is the point of doing it in
// software. The 8 s ceiling matches the reference tank's own knob, so the same
// number on both faces means the same tail.
constexpr float kMinDecaySeconds    = 0.4f;
constexpr float kMaxDecaySeconds    = 8.0f;
constexpr float kDecaySkewCentre    = 2.2f;
constexpr float kDefaultDecaySeconds = 1.8f;

// What fraction of the knob becomes the loop's own RT60 target, fitted
// alongside the shelves above. It is not 1.0 because the shelves themselves
// eat into the mid band on the way past, so the loop has to be asked for
// slightly more than the knob says to land on it. Turn the shelves up or down
// and this has to be refitted with them.
constexpr float kDecayScale = 0.91f;

// Ceiling on the per-trip loop gain, whatever the decay knob asks for. The
// chirp chain is all-pass and the filters only ever lose energy, so the loop
// is stable for anything below 1 - this keeps a margin.
constexpr float kMaxLoopGain = 0.995f;

// ============================================================================
// LEVEL
// ============================================================================
// Output trim on the summed springs. Set by measurement, not by ear: with the
// mix knob at 100 % the reference tank's output sits 28.4 dB under full scale
// on the test material, and this is what puts ours in the same place.
constexpr float kWetTrim = 0.47f;

constexpr float kDefaultMixPercent = 35.0f;

// Make-up gain on the WET path as the mix knob comes up. A spring tank is a
// narrow band-pass - roughly 60 Hz to 6 kHz - so as the knob replaces a
// full-range dry signal with it, the level sags even though nothing has
// actually been turned down. This puts it back.
//
// It rides the same shaped mix value the wet gain does, so the compensation
// arrives exactly as the wet does: none at all at mix 0, and the full amount
// only at mix 100.
//
// The value is what the wet is multiplied by at the top of the knob, and it is
// measured rather than picked: without it the output sags 2.8 dB from dry to
// fully wet, and 1.38x is what flattens that sweep to within a few tenths.
//   1.00 = off - the wet is left exactly where the reference tank puts it
//   1.38 = level holds across the whole knob  <-- default
constexpr float kMixMakeupAtFullWet = 1.38f;

// Hard ceiling on the above, whatever it is set to. Make-up gain on a reverb
// is a foot-gun: it is applied blind, with no idea what the source is, so a
// setting that flatters one guitar can clip another. Doubling is as far as
// this is allowed to go.
constexpr float kMixMakeupMax = 2.0f;
static_assert (kMixMakeupAtFullWet <= kMixMakeupMax,
               "wet make-up must stay under the x2 ceiling");

} // namespace ee::dsp::spring
