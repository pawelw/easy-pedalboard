#pragma once

#include "ee/dsp/RateMap.h"

/** Peak Artifact's Filter Time knob: one LFO cycle from 30 ms (knob down) to
    3 s (knob up) - a filter wobble rather than a tremolo throb, the same travel
    Peak Wah's Time knob has.

    Like Peak Wah's, this knob runs the other way up from the shared
    ee::dsp::RateMap: down is the shortest period, up is the longest. Every entry
    point below flips the normalised position before handing it to the shared
    map, so the processor and the editor readout agree. */
namespace ee::peakartifact
{

inline constexpr ee::dsp::RateMap kMap { 30.0f, 3000.0f, 450.0f };

/** Flip the knob: 0 -> fastest, 1 -> slowest. */
inline float invert (float rate01) noexcept
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, rate01);
}

inline float syncedDivisionBeats (float rate01) noexcept
{
    return ee::dsp::RateMap::syncedDivisionBeats (invert (rate01));
}

inline float rateToPeriodSeconds (float rate01, bool synced, double bpm) noexcept
{
    return kMap.rateToPeriodSeconds (invert (rate01), synced, bpm);
}

inline juce::String rateToText (float rate01, bool synced)
{
    return kMap.rateToText (invert (rate01), synced);
}

} // namespace ee::peakartifact
