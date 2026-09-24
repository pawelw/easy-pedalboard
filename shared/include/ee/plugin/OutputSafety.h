#pragma once

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace ee::plugin
{

/** ~+12 dBFS - a level nothing on a properly-behaving face reaches on purpose,
    so crossing it means a genuine runaway rather than a loud preset. The same
    number BitBitAlpineProcessor::kSafetyCeiling uses, and what
    ee_soak_<Target> checks every block against - see release-plan.md 1.5. */
inline constexpr float kOutputSafetyCeiling = 4.0f;

/** The last line of defence on a processor's *finished* output, unconditional
    and in every build - not a voicing control, and normal operation never
    reaches it. A NaN or Inf is zeroed; anything past +/-ceiling is clamped
    there rather than zeroed, so a runaway reads as "pinned loud" rather than
    as a dropout.

    Generalises BitBitAlpineProcessor::sanitizeOutput for a pedal that does not
    need Alpine's peak-tracking/watchdog machinery alongside it - Alpine keeps
    its own copy rather than switching to this one, since the watchdog reads
    the verdict this returns nothing about.

    A per-engine internal guard (`ee::fx::DelayModule::scrub`, the runaway
    check inside `ee::fx::MultiEngineModule::process`) already catches a
    genuinely exploded value - those run much looser, around +36 dBFS, because
    they are guarding a feedback path against latching a NaN, not shaping what
    a listener hears. This is the tighter, final check on the signal that
    actually leaves the plugin. */
inline void sanitizeOutput (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples,
                            float ceiling = kOutputSafetyCeiling) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* d = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = d[i];

            if (! std::isfinite (x))
                d[i] = 0.0f;
            else if (x > ceiling)
                d[i] = ceiling;
            else if (x < -ceiling)
                d[i] = -ceiling;
        }
    }
}

} // namespace ee::plugin
