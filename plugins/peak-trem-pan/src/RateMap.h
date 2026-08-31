#pragma once

#include "ee/dsp/RateMap.h"

/** Peak Trem & Pan's Rate knob: one LFO cycle from 2 s (knob down) to 10 ms (knob up). */
namespace ee::trempan
{

inline constexpr ee::dsp::RateMap kMap { 10.0f, 2000.0f, 300.0f };

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

} // namespace ee::trempan
