#pragma once

#include "ee/dsp/RateMap.h"

/** Peak Wah's Time knob: one LFO cycle from 3 s (knob down) to 30 ms (knob up) -
    a filter wobble rather than a tremolo throb, so slower at both ends than Peak
    Trem & Pan. */
namespace ee::peakwah
{

inline constexpr ee::dsp::RateMap kMap { 30.0f, 3000.0f, 450.0f };

inline juce::NormalisableRange<float> freePeriodMsRange()
{
    return kMap.freePeriodMsRange();
}
inline float freePeriodMs (float rate01) noexcept
{
    return kMap.freePeriodMs (rate01);
}
inline float rate01ForFreePeriodMs (float ms) noexcept
{
    return kMap.rate01ForFreePeriodMs (ms);
}
inline int syncedDivisionIndex (float rate01) noexcept
{
    return ee::dsp::RateMap::syncedDivisionIndex (rate01);
}
inline float syncedDivisionBeats (float rate01) noexcept
{
    return ee::dsp::RateMap::syncedDivisionBeats (rate01);
}

inline float rateToPeriodSeconds (float rate01, bool synced, double bpm) noexcept
{
    return kMap.rateToPeriodSeconds (rate01, synced, bpm);
}

inline juce::String rateToText (float rate01, bool synced)
{
    return kMap.rateToText (rate01, synced);
}

} // namespace ee::peakwah
