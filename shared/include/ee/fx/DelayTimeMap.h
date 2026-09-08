#pragma once

#include <cmath>

#include "ee/dsp/GrainSyncMap.h"
#include "ee/dsp/TempoDivision.h"

/** The Left/Right Time knobs of Peak Delay - and of Peak Alpine's Delay
    module, which is the same chain, so the same knob position has to mean the
    same time on both: one normalised 0..1 parameter whose
    meaning the Sync pill decides - a note division when synced, a continuous
    millisecond time when free. `ee::dsp::GrainSyncMap` already is exactly this
    control (its own comment names "delay Time" as the duration-like case), so
    all that lives here is the delay's own free-mode sweep.

    Both knobs used to be an AudioParameterChoice over the division table, which
    made the free reading quantised too: the pill swapped the *text* from "1/1."
    to "3000 ms" but the knob still had only fifteen stops, so a drag stepped
    3000 -> 2000 -> 1500 rather than gliding. Peak Trem & Pan never had that
    because its Rate knob has always been a normalised float behind a map.

    The free sweep is deliberately the span the divisions themselves cover at
    kReferenceBpm - 1/32 at the bottom of the travel, 1/1. at the top - skewed
    exponentially, which the division table roughly is. So a given knob
    position means about the same thing either side of the pill (within ~30 %
    over most of the travel, less well down at the bottom where the table's
    triplets and dots bunch up), and turning Sync off reads as un-quantising
    the time and cutting it loose from the host rather than as jumping it
    somewhere else. That is also why there is no remembered per-mode knob
    position here of the kind Peak Grain and Peak Trem & Pan carry: those two
    map their free and synced sweeps to quite different spans, so they have
    something to remember and this does not. */
namespace ee::peakdelay
{

/** The tempo a free-running time is measured against. Free time is supposed to
    stop tracking the host, so the divisions' span has to be taken at some fixed
    tempo; 120 is also what currentBpm() falls back to when a host reports none. */
inline constexpr double kReferenceBpm = 120.0;

inline constexpr float msForBeats (float beats) noexcept
{
    return beats * static_cast<float> (60000.0 / kReferenceBpm);
}

/** Built once on first use rather than being a constant: NormalisableRange's
    skew is computed by setSkewForCentre, which is not constexpr. */
inline const ee::dsp::GrainSyncMap& timeMap()
{
    static const ee::dsp::GrainSyncMap map = []
    {
        const float shortest = msForBeats (ee::dsp::kTempoDivisions[0].beats);
        const float longest = msForBeats (ee::dsp::kTempoDivisions[ee::dsp::kNumTempoDivisions - 1].beats);

        juce::NormalisableRange<float> range (shortest, longest);
        range.setSkewForCentre (std::sqrt (shortest * longest));

        return ee::dsp::GrainSyncMap { range, true };
    }();

    return map;
}

/** The knob position that selects a division index. Also what a saved state
    from the AudioParameterChoice era already holds - a choice parameter
    normalises its index exactly this way - so old presets carry over with the
    same division selected. */
inline constexpr float time01ForDivision (int index) noexcept
{
    return static_cast<float> (index) / static_cast<float> (ee::dsp::kNumTempoDivisions - 1);
}

/** Seconds of delay for a knob position, in whichever mode the pill is in. */
inline float timeSeconds (float time01, bool synced, double bpm) noexcept
{
    return timeMap().value (time01, synced, bpm) * 0.001f;
}

} // namespace ee::peakdelay
