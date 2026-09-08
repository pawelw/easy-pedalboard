#pragma once

/**
 * Voicing for ee::fx::DelayModule - the settings that make Peak Delay's chain
 * sound like Peak Delay rather than like a delay line with things bolted on.
 *
 * Its own namespace (ee::fx::delaymodule) for the reason PhaserConfig.h gives:
 * several voicing headers land in the same test translation unit and would
 * collide on names this general.
 *
 * TO USE:
 *   1. Change a value.
 *   2. cmake --build build
 *   3. Rescan in the host.
 */

namespace ee::fx::delaymodule
{

// ============================================================================
// TAPE PLACEMENT
// ============================================================================
// How long the Tape router takes to move the section from one side of the delay
// to the other. Slow on purpose: what actually travels is Wear and Flutter, so
// this is a hand turning one knob down while another comes up, and a quarter of
// a second is about how fast a hand does that. Anything much shorter starts to
// read as a step in the drive rather than as a move.
constexpr double kPlacementSeconds = 0.25;

// How much of the tape stage's own Wear travel this module's knob reaches: a
// fully-turned Wear here drives the machine half as hard as a fully-turned Wear
// on Peak Tape.
//
// Not in TapeMachineConfig.h with the rest of the tape voicing, and not a change
// to the stage itself. The machine is shared, Peak Tape's knob still reaches all
// of it, and re-voicing the engine would move both pedals - which is the thing
// the shared stage exists to prevent. What is different here is only how far
// *this* face's knob turns it, which is this module's business: on Peak Tape the
// machine is the effect and its top end is the point, while here it is a colour
// on a delay and the top of that range swamped the repeats long before the knob
// ran out. Halving the reach spreads the useful part across the whole travel
// instead of the first half of it.
//
// It is deliberately a scale on the amount rather than a smaller parameter
// range: the knob still reads 0-100 %, because what it is a percentage of is
// "as worn as this module goes", not "as worn as the machine goes".
constexpr float kWearScale = 0.5f;

// ============================================================================
// SMOOTHING
// ============================================================================
// The dry/wet, engage and trim ramps. Equal to ee::plugin::kRampSeconds, and
// restated here for the same reason TremoloConfig.h restates it: nothing under
// shared/include/ee reaches up into ee/plugin for a constant, and the host that
// owns both static_asserts they agree.
constexpr float kGainRampSeconds = 0.02f;

// ============================================================================
// FILTER
// ============================================================================
// Where each cut stops being run at all. A cut resting at its open end is not a
// filter set to "off", it is a filter that is not in the signal path - so a
// Filter section nobody has touched cannot colour anything. The margins are
// slack against a parameter that lands a hair off its endpoint.
constexpr float kLoCutBypassMarginHz = 0.5f;
constexpr float kHiCutBypassMarginHz = 0.5f;

// The floor a delay time is held above once the post section's latency has been
// taken off it. At the shortest time the knob reaches this is nowhere near -
// 62 ms against 6 ms of trim.
constexpr float kMinDelaySeconds = 0.001f;

} // namespace ee::fx::delaymodule
