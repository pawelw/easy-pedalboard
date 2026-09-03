#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

#include "ee/dsp/TempoDivision.h"

namespace ee::dsp
{

/** A knob whose meaning a Sync switch flips: a free value in the knob's own
    unit, or a tempo-locked note division. One normalised 0..1 parameter backs
    both readings, so the switch only reinterprets it - the same trick RateMap
    plays for an LFO, generalised to the two flavours Peak Grain needs:

      - a duration  (grain Size, delay Time): knob up = longer, free unit is
        milliseconds, the synced value is the division's length
      - a rate      (grain Density):          knob up = faster, free unit is
        events per second, the synced value is 1 / the division's length

    `freeRange` is the skewed map from 0..1 to the free unit; `durationLike`
    picks which way round the divisions run and whether the synced value is read
    back as a time or a rate. RateMap is period-only and fixes knob-up = faster,
    so it could not be reused for the Size knob directly.
*/
struct GrainSyncMap
{
    juce::NormalisableRange<float> freeRange;   ///< 0..1 -> free unit (ms, or /s)
    bool                           durationLike = true;

    float freeValue (float v01) const noexcept
    {
        return freeRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, v01));
    }

    /** The knob position that gives a wanted free value - for seeding a default
        or restoring a remembered position. */
    float v01ForFree (float value) const noexcept
    {
        return freeRange.convertTo0to1 (juce::jlimit (freeRange.start, freeRange.end, value));
    }

    /** Which division the knob position selects. A duration knob puts the
        longest division at the top of the travel; a rate knob puts the fastest
        (shortest) there, so turning either knob up always speeds the effect. */
    int divisionIndex (float v01) const noexcept
    {
        const int   last = kNumTempoDivisions - 1;
        const float t    = juce::jlimit (0.0f, 1.0f, v01);
        const float pick = durationLike ? t : 1.0f - t;
        return juce::jlimit (0, last, juce::roundToInt (pick * static_cast<float> (last)));
    }

    /** Length of the selected division at this tempo, in seconds. */
    float divisionSeconds (float v01, double bpm) const noexcept
    {
        return kTempoDivisions[divisionIndex (v01)].beats * static_cast<float> (60.0 / juce::jmax (1.0, bpm));
    }

    /** The value the DSP should use. A duration map returns milliseconds (scale
        by 0.001 for seconds); a rate map returns events per second. */
    float value (float v01, bool synced, double bpm) const noexcept
    {
        if (! synced)
            return freeValue (v01);

        const float secs = divisionSeconds (v01, bpm);
        return durationLike ? secs * 1000.0f : 1.0f / juce::jmax (1.0e-4f, secs);
    }

    juce::String toText (float v01, bool synced, double bpm) const
    {
        if (synced)
            return kTempoDivisions[divisionIndex (v01)].label;

        const float f = freeValue (v01);

        if (! durationLike)
            return juce::String (f, f < 10.0f ? 1 : 0) + " /s";

        if (f >= 1000.0f)
            return juce::String (f * 0.001f, 2) + " s";

        return juce::String (juce::roundToInt (f)) + " ms";
    }
};

} // namespace ee::dsp
