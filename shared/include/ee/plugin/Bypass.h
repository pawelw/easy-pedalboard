#pragma once

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace ee::plugin
{

/** How long a bypass or level change takes to ramp. Long enough not to click,
    short enough to feel immediate. */
inline constexpr float kRampSeconds = 0.02f;

/** Crossfades the processed buffer back towards an untouched dry copy, so the
    host's on/off never clicks, and guarantees a finite result.

    `wetGain`, if given, is applied to the wet side only - an output level knob
    that should not also turn the bypassed signal up.

    The non-finite clamp is not optional. A NaN handed downstream gets latched
    into the tail of any feedback effect after this one and roars; that is what
    the trem-into-reverb bug was. Every pedal gets the guard here whether or not
    its own engine can produce one. */
inline void crossfadeToDry (juce::AudioBuffer<float>& buffer,
                            const juce::AudioBuffer<float>& dryBuffer,
                            juce::SmoothedValue<float>& wetMix,
                            int numChannels,
                            int numSamples,
                            juce::SmoothedValue<float>* wetGain = nullptr) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = wetGain != nullptr ? wetGain->getNextValue() : 1.0f;
        const float wet = wetMix.getNextValue();
        const float dry = 1.0f - wet;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out = buffer.getWritePointer (ch, i);
            *out = (*out * g) * wet + dryBuffer.getSample (ch, i) * dry;

            if (! std::isfinite (*out))
                *out = 0.0f;
        }
    }
}

} // namespace ee::plugin
