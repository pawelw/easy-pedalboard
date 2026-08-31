#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace ee::dsp
{

/** 4th-order Butterworth low-pass, as two RBJ biquad sections.

    The anti-imaging / anti-aliasing leg of a 2x oversampled nonlinearity: one
    instance on the way up, a second on the way down, both set up at the
    oversampled rate. Shared by Peak Overdrive's diode clipper and Peak Tape's
    record-head saturation - a soft-clipper folds its own harmonics back over
    the top octave without it, which is the one thing that reads as digital.
*/
struct Aa4
{
    struct Section { float b0, b1, b2, a1, a2, z1, z2; };
    std::array<Section, 2> s {};

    void setup (float cutoffHz, float fs) noexcept
    {
        // Butterworth 4th-order section quality factors.
        const float qs[2] = { 0.54119610f, 1.30656296f };
        const float w0 = juce::MathConstants<float>::twoPi
                         * juce::jlimit (10.0f, 0.49f * fs, cutoffHz) / fs;
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);

        for (int i = 0; i < 2; ++i)
        {
            const float alpha = sw / (2.0f * qs[i]);
            const float a0 = 1.0f + alpha;
            s[static_cast<size_t> (i)].b0 = (1.0f - cw) * 0.5f / a0;
            s[static_cast<size_t> (i)].b1 = (1.0f - cw) / a0;
            s[static_cast<size_t> (i)].b2 = (1.0f - cw) * 0.5f / a0;
            s[static_cast<size_t> (i)].a1 = (-2.0f * cw) / a0;
            s[static_cast<size_t> (i)].a2 = (1.0f - alpha) / a0;
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& sec : s) { sec.z1 = 0.0f; sec.z2 = 0.0f; }
    }

    inline float process (float x) noexcept
    {
        for (auto& sec : s)
        {
            const float y = sec.b0 * x + sec.z1;
            sec.z1 = sec.b1 * x - sec.a1 * y + sec.z2;
            sec.z2 = sec.b2 * x - sec.a2 * y;
            x = y;
        }
        return x;
    }
};

} // namespace ee::dsp
