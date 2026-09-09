#pragma once

#include "ee/dsp/RateMap.h"
#include "ee/dsp/TremoloConfig.h"

/** Peak Trem & Pan's Rate knob: one LFO cycle from 2 s (knob down) to 10 ms
    (knob up). The three numbers live with the tremolo's voicing rather than
    here, because Peak Alpine's Modulation module carries the same knob and the
    same position has to mean the same rate on both faces. */
namespace ee::trempan
{

inline constexpr ee::dsp::RateMap kMap { ee::dsp::tremolo::kRateMinPeriodMs,
                                         ee::dsp::tremolo::kRateMaxPeriodMs,
                                         ee::dsp::tremolo::kRateSkewCentreMs };

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
