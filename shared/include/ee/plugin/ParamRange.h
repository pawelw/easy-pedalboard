#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ee::plugin
{
/** Nudges `value` onto a point the range can reproduce exactly.

    A skewed `NormalisableRange` converts through `pow`/`exp`, so a round trip
    is not the identity for every float. `auval -strict` sets each parameter to
    the default it was told about and reads it back, and reports
    "Parameter did not retain default value when set" when the two differ -
    which is what a default sitting off a representable point looks like.

    Iterates rather than converting once: the round trip walks towards a fixed
    point instead of landing on one. Spring's 1.8 s decay takes three passes.
*/
inline float snapToRange (const juce::NormalisableRange<float>& range, float value) noexcept
{
    constexpr int maxPasses = 8; // it converges in three; the cap is for safety

    for (int pass = 0; pass < maxPasses; ++pass)
    {
        const auto snapped = range.convertFrom0to1 (range.convertTo0to1 (value));

        if (juce::exactlyEqual (snapped, value))
            break;

        value = snapped;
    }

    return value;
}
} // namespace ee::plugin
