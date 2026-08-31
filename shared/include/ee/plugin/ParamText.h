#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/** Parameter value formatters shared by more than one pedal.

    Only the ones that were genuinely byte-identical across processors live here.
    Several formatters that share a name do not share a behaviour and have
    deliberately been left where they are:

    - `hzToText` rounds to a whole number in Peak Wah but keeps a decimal place in
      Peak Chorus and Peak Phase. The decimal form is the one below; Wah keeps its
      own.
    - `decibelsToText` prints "3.0 dB" in Peak Overdrive and a bare signed integer
      in Peak EQ, whose fader row is too narrow for a unit.
    - Peak Reverb's `hertzToText`, Peak EQ's cut readouts and Peak Wah's
      `filterTypeToText` all have pedal-specific special cases.

    Do not fold those together without deciding what the readout should say. */
namespace ee::plugin
{

/** "45 %" - a whole-number percentage. */
inline juce::String percentToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " %";
}

/** "0.75 Hz" below 1 Hz, "3.2 Hz" above it - an LFO rate slow enough that the
    decimal matters. Peak Wah's rates are fast enough that it rounds instead. */
inline juce::String hzToText (float value, int)
{
    return juce::String (value, value < 1.0f ? 2 : 1) + " Hz";
}

} // namespace ee::plugin
