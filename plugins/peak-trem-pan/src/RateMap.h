#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "ee/dsp/TempoDivision.h"

#include <cmath>

/** The Rate knob is one normalised 0..1 parameter whose meaning is set by the
    Sync switch. Both readings - the note division when synced and the period in
    milliseconds when free - are derived here so the processor, the editor
    readout and the snapshot test all agree.

    In both modes turning the knob up speeds the LFO up, so the preview (more
    cycles as the knob rises) reads correctly either way. */
namespace ee::trempan
{

// Free-rate sweep: one LFO cycle from 2 s (knob down) to 10 ms (knob up), packed
// towards the faster end where small moves matter more.
inline juce::NormalisableRange<float> freePeriodMsRange()
{
    juce::NormalisableRange<float> r (10.0f, 2000.0f);
    r.setSkewForCentre (300.0f);
    return r;
}

/** Milliseconds for one LFO cycle in free mode. Knob up (rate01 -> 1) = fast. */
inline float freePeriodMs (float rate01) noexcept
{
    return freePeriodMsRange().convertFrom0to1 (1.0f - juce::jlimit (0.0f, 1.0f, rate01));
}

/** The knob position that gives a wanted free-mode period. */
inline float rate01ForFreePeriodMs (float ms) noexcept
{
    return 1.0f - freePeriodMsRange().convertTo0to1 (juce::jlimit (10.0f, 2000.0f, ms));
}

inline int syncedDivisionIndex (float rate01) noexcept
{
    const int last = ee::dsp::kNumTempoDivisions - 1;
    // Faster rate (higher knob) = shorter division = lower table index.
    return juce::jlimit (0, last,
                         juce::roundToInt ((1.0f - juce::jlimit (0.0f, 1.0f, rate01)) * static_cast<float> (last)));
}

/** Length of the current synced division, in quarter notes. */
inline float syncedDivisionBeats (float rate01) noexcept
{
    return ee::dsp::kTempoDivisions[syncedDivisionIndex (rate01)].beats;
}

/** Seconds for one LFO cycle. */
inline float rateToPeriodSeconds (float rate01, bool synced, double bpm) noexcept
{
    if (synced)
        return syncedDivisionBeats (rate01) * static_cast<float> (60.0 / juce::jmax (1.0, bpm));

    return freePeriodMs (rate01) * 0.001f;
}

inline juce::String rateToText (float rate01, bool synced)
{
    if (synced)
        return ee::dsp::kTempoDivisions[syncedDivisionIndex (rate01)].label;

    const float ms = freePeriodMs (rate01);
    return ms >= 100.0f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms, 1) + " ms";
}

} // namespace ee::trempan
