#pragma once

#include "ee/dsp/RateMap.h"

/** Peak Wah's Time knob: one LFO cycle from 30 ms (knob down) to 3 s (knob up) -
    a filter wobble rather than a tremolo throb, so slower at both ends than Peak
    Trem & Pan.

    This is the one place the knob runs the other way up from the shared
    ee::dsp::RateMap (and from Trem & Pan): down is the *shortest* period, up is
    the *longest*. Every entry point below flips the normalised position before
    handing it to the shared map, so the processor, the editor readout and the
    snapshot test all still agree. */
namespace ee::peakwah
{

inline constexpr ee::dsp::RateMap kMap { 30.0f, 3000.0f, 450.0f };

/** Flip the knob: 0 -> fastest, 1 -> slowest. */
inline float invert (float rate01) noexcept
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, rate01);
}

inline juce::NormalisableRange<float> freePeriodMsRange()
{
    return kMap.freePeriodMsRange();
}
inline float freePeriodMs (float rate01) noexcept
{
    return kMap.freePeriodMs (invert (rate01));
}
inline float rate01ForFreePeriodMs (float ms) noexcept
{
    return invert (kMap.rate01ForFreePeriodMs (ms));
}
inline int syncedDivisionIndex (float rate01) noexcept
{
    return ee::dsp::RateMap::syncedDivisionIndex (invert (rate01));
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

} // namespace ee::peakwah
