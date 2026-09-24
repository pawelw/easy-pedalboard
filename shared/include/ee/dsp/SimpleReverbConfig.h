#pragma once

#include <cmath>

/**
 * Voicing for BitBit Reverb's Simple engine (ee::fx::ReverbModule::Simple).
 *
 * Simple is the Studio engine (ee::dsp::SpaceReverb) behind one knob, Amount.
 * Turning it up makes the room longer, and as the tail grows it also darkens a
 * little and loses its bottom, so a big Amount stays a wash rather than a
 * boom: Decay, Damping and Low Cut all ride the one knob together. Size,
 * Pre-delay and Hi Cut stay at SpaceReverb's resting values.
 *
 * Decay and Low Cut move exponentially (equal knob travel is an equal ratio,
 * which is how both are heard); Damping moves linearly.
 *
 * pedal-ui's ReverbScope (its `simple` model) draws the tail from the same
 * numbers - change them there too.
 */

namespace ee::dsp::simple
{

// Amount at 0 and at 100 %.
constexpr float kMinDecaySeconds = 0.8f;
constexpr float kMaxDecaySeconds = 5.0f;

// "A bit of damping": from a shade under the Studio engine's own 25 % default
// to a little under half.
constexpr float kMinDamping01    = 0.15f;
constexpr float kMaxDamping01    = 0.45f;

// Off at the bottom (SpaceReverb::kMinLowCutHz), a gentle clean-up at the top.
constexpr float kMinLowCutHz     = 20.0f;
constexpr float kMaxLowCutHz     = 220.0f;

// Where a fresh instance sits, in per cent.
constexpr float kDefaultAmountPct = 40.0f;

struct Voicing
{
    float decaySeconds;
    float damping01;
    float lowCutHz;
};

inline Voicing voicingFor (float amount01) noexcept
{
    const float a = amount01 < 0.0f ? 0.0f : (amount01 > 1.0f ? 1.0f : amount01);

    return { kMinDecaySeconds * std::pow (kMaxDecaySeconds / kMinDecaySeconds, a),
             kMinDamping01 + (kMaxDamping01 - kMinDamping01) * a,
             kMinLowCutHz * std::pow (kMaxLowCutHz / kMinLowCutHz, a) };
}

} // namespace ee::dsp::simple
