#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "ee/dsp/TempoDivision.h"

namespace ee::dsp
{

/** A Rate/Time knob that is one normalised 0..1 parameter whose meaning is set by
    a Sync switch: a free LFO period in milliseconds, or a tempo-synced note
    division. Both readings are derived here so the processor, the editor readout
    and the snapshot test all agree.

    In both modes turning the knob up speeds the LFO up, so a preview that draws
    more cycles as the knob rises reads correctly either way.

    Only the free-mode sweep differs between pedals - a filter wobble wants a
    slower floor than a tremolo throb - so that is what a pedal supplies. The
    synced side is the shared tempo-division table and is the same everywhere. */
struct RateMap
{
    float minPeriodMs;  ///< one cycle with the knob fully up (fastest)
    float maxPeriodMs;  ///< one cycle with the knob fully down (slowest)
    float skewCentreMs; ///< period at the middle of the knob's travel

    juce::NormalisableRange<float> freePeriodMsRange() const
    {
        juce::NormalisableRange<float> r (minPeriodMs, maxPeriodMs);
        r.setSkewForCentre (skewCentreMs);
        return r;
    }

    /** Milliseconds for one LFO cycle in free mode. Knob up (rate01 -> 1) = fast. */
    float freePeriodMs (float rate01) const noexcept
    {
        return freePeriodMsRange().convertFrom0to1 (1.0f - juce::jlimit (0.0f, 1.0f, rate01));
    }

    /** The knob position that gives a wanted free-mode period. */
    float rate01ForFreePeriodMs (float ms) const noexcept
    {
        return 1.0f - freePeriodMsRange().convertTo0to1 (juce::jlimit (minPeriodMs, maxPeriodMs, ms));
    }

    /** Faster rate (higher knob) = shorter division = lower table index. */
    static int syncedDivisionIndex (float rate01) noexcept
    {
        const int last = kNumTempoDivisions - 1;
        return juce::jlimit (0, last,
                             juce::roundToInt ((1.0f - juce::jlimit (0.0f, 1.0f, rate01)) * static_cast<float> (last)));
    }

    /** Length of the current synced division, in quarter notes. */
    static float syncedDivisionBeats (float rate01) noexcept
    {
        return kTempoDivisions[syncedDivisionIndex (rate01)].beats;
    }

    /** Seconds for one LFO cycle. */
    float rateToPeriodSeconds (float rate01, bool synced, double bpm) const noexcept
    {
        if (synced)
            return syncedDivisionBeats (rate01) * static_cast<float> (60.0 / juce::jmax (1.0, bpm));

        return freePeriodMs (rate01) * 0.001f;
    }

    juce::String rateToText (float rate01, bool synced) const
    {
        if (synced)
            return kTempoDivisions[syncedDivisionIndex (rate01)].label;

        const float ms = freePeriodMs (rate01);
        return ms >= 100.0f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms, 1) + " ms";
    }
};

} // namespace ee::dsp
