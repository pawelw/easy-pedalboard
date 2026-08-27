#pragma once

#include <juce_core/juce_core.h>

#include <iterator>

namespace ee::dsp
{

/** Note values a tempo-synced time control can be set to, shortest first.

    `beats` is measured in quarter notes, which is what a host BPM counts.
*/
struct TempoDivision
{
    const char* label;
    float beats;
};

inline constexpr TempoDivision kTempoDivisions[] = {
    { "1/32",  0.125f },
    { "1/16T", 1.0f / 6.0f },
    { "1/16",  0.25f },
    { "1/16.", 0.375f },
    { "1/8T",  1.0f / 3.0f },
    { "1/8",   0.5f },
    { "1/8.",  0.75f },
    { "1/4T",  2.0f / 3.0f },
    { "1/4",   1.0f },
    { "1/4.",  1.5f },
    { "1/2T",  4.0f / 3.0f },
    { "1/2",   2.0f },
    { "1/2.",  3.0f },
    { "1/1",   4.0f },
    { "1/1.",  6.0f },
};

inline constexpr int kNumTempoDivisions = static_cast<int> (std::size (kTempoDivisions));

inline juce::StringArray tempoDivisionLabels()
{
    juce::StringArray names;
    for (const auto& d : kTempoDivisions)
        names.add (d.label);
    return names;
}

} // namespace ee::dsp
