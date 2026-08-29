#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace ee::dsp
{

/** The trem/pan LFO shape, as a function of phase.

    One `shape` knob sweeps the waveform through five anchors:

      0 %   exponential decay - a sharp attack falling away, the "plucked" feel
      25 %  falling ramp (sawtooth)
      50 %  triangle
      75 %  soft rounded square
      100 % rounded-corner rectangle - a hard chop, but the corners are eased so
            it never steps between two levels in a single sample and so never
            clicks

    In between it linearly crossfades one anchor into the next. The ramp-type
    anchors fly back over a short window rather than a vertical edge, for the
    same anti-click reason. Both the audio path and the on-screen preview call
    `lfoValue`, so the drawing is literally a picture of the wave that is heard.
*/
namespace lfo_detail
{
    // Fraction of a cycle the ramp shapes take to return to the top.
    inline constexpr float kFlyback = 0.06f;

    inline float easeUp (float t) noexcept   // 0..1 -> -1..1, flat ends
    {
        return -1.0f + (1.0f - std::cos (juce::jlimit (0.0f, 1.0f, t)
                                         * juce::MathConstants<float>::pi));
    }

    inline float expDecay (float p) noexcept
    {
        constexpr float k = 5.0f;
        constexpr float body = 1.0f - kFlyback;
        const float endValue = -1.0f + 2.0f * std::exp (-k);

        if (p < body)
            return -1.0f + 2.0f * std::exp (-k * (p / body));

        const float t = (p - body) / kFlyback;
        return endValue + (1.0f - endValue) * 0.5f * (1.0f - std::cos (t * juce::MathConstants<float>::pi));
    }

    inline float ramp (float p) noexcept
    {
        constexpr float body = 1.0f - kFlyback;
        if (p < body)
            return 1.0f - 2.0f * (p / body);

        return easeUp ((p - body) / kFlyback);
    }

    inline float triangle (float p) noexcept
    {
        return p < 0.5f ? 1.0f - 4.0f * p : 4.0f * p - 3.0f;
    }

    inline float roundedSquare (float p, float sharpness) noexcept
    {
        const float c = std::cos (p * juce::MathConstants<float>::twoPi);
        return std::tanh (sharpness * c) / std::tanh (sharpness);
    }

    inline float anchor (int index, float p) noexcept
    {
        switch (index)
        {
            case 0:  return expDecay (p);
            case 1:  return ramp (p);
            case 2:  return triangle (p);
            case 3:  return roundedSquare (p, 2.1f);
            default: return roundedSquare (p, 5.5f);
        }
    }
} // namespace lfo_detail

/** Shaped LFO value at a phase (any real; the integer part is discarded) and a
    shape in [0, 1]. Returns roughly [-1, 1], peaking at +1 at phase 0. */
inline float lfoValue (float phase, float shape) noexcept
{
    const float p = phase - std::floor (phase);
    const float s = juce::jlimit (0.0f, 1.0f, shape);

    const float seg = s * 4.0f;
    const int i = juce::jlimit (0, 3, static_cast<int> (seg));
    const float t = seg - static_cast<float> (i);

    const float lo = lfo_detail::anchor (i, p);
    const float hi = lfo_detail::anchor (i + 1, p);
    return lo + (hi - lo) * t;
}

} // namespace ee::dsp
