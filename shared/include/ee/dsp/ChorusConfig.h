#pragma once

/**
 * Voicing for ee::dsp::Chorus.
 *
 * These defaults are a "sounds good out of the box" wide stereo chorus. The
 * four knobs (Rate, Depth, Phase, Mix) only ever scale what is set up here;
 * retuning the character means editing this file and nothing else.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::dsp::config
{

// ============================================================================
// VOICES
// ============================================================================
// Modulated delay taps per channel. Each voice has its own fixed base delay
// and rides the LFO at a spread phase, so the combined sweep is full instead of
// one-note. Two is a lush classic chorus; three thickens it toward an ensemble.
// kMaxChorusVoices only sizes the tables below - raise it (and add base-delay
// entries) if you want to go higher.
constexpr int kMaxChorusVoices  = 3;
constexpr int kVoicesPerChannel = 2;

// How far apart the voices of one channel sit on the LFO, in cycles, spread
// evenly across the voice count. MUST stay clear of 0.5 for a two-voice setup:
// two exactly-antiphase voices make the pitch modulation cancel twice per LFO
// cycle, which is heard as the chorus briefly "dropping out" and back.
constexpr float kVoicePhaseSpreadCycles = 0.7f;

// Base (unmodulated) delay of each voice, in milliseconds. The left/right
// asymmetry alone opens the stereo image before the LFOs do anything - it is
// the static width floor that keeps the image open even at the instants the
// modulation is momentarily still.
//   ~7-12 ms  = shimmery, "sharper" chorus
//   ~12-20 ms = deeper, more vibrato-leaning                <-- default range
constexpr float kBaseDelayMsLeft[kMaxChorusVoices]  = { 9.0f, 14.0f, 20.0f };
constexpr float kBaseDelayMsRight[kMaxChorusVoices] = { 12.5f, 18.0f, 24.0f };

// ============================================================================
// DEPTH
// ============================================================================
// Peak LFO excursion, in milliseconds, at Depth = 100 %. Added on top of the
// base delay, so it never drives the read negative.
//   2 ms  = subtle
//   5 ms  = rich, still musical                             <-- default
//   9 ms+ = seasick / detuned
constexpr float kDepthMaxMs = 5.0f;

// ============================================================================
// RATE
// ============================================================================
// LFO frequency range of the Rate knob, in Hz. The knob is skewed so the slow,
// useful end gets most of the travel.
constexpr float kRateMinHz       = 0.05f;
constexpr float kRateMaxHz       = 8.0f;
constexpr float kRateSkewCentreHz = 0.8f;

// ============================================================================
// WET SHAPING
// ============================================================================
// One-pole filters on each wet channel, before the mix. The high-pass keeps
// the low end tight and centred; the low-pass keeps the moving reads from
// adding fizz on top.
constexpr float kWetHighPassHz = 100.0f;
constexpr float kWetLowPassHz  = 9000.0f;

// ============================================================================
// PHASE
// ============================================================================
// The Phase knob is the width control: it offsets the right channel's LFOs
// from the left's. The knob reads 0..kMaxPhaseDeg, but the actual LFO offset it
// drives is capped at kPhaseSpanCycles - kept below 0.5 (antiphase) on purpose.
// At exactly antiphase the right channel becomes a mirror of the left and the
// image collapses to mono every time the LFO crosses zero; ~0.33 cycles (~120
// deg) is the widest offset that stays decorrelated right through the cycle.
constexpr float kMaxPhaseDeg    = 180.0f;
constexpr float kPhaseSpanCycles = 0.33f;

// ============================================================================
// DEFAULTS (knob positions the pedal opens on)
// ============================================================================
constexpr float kDefaultRateHz   = 0.6f;
constexpr float kDefaultDepthPct = 45.0f;
constexpr float kDefaultPhaseDeg = 110.0f;
constexpr float kDefaultMixPct   = 50.0f;

} // namespace ee::dsp::config
