#pragma once

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace ee::dsp
{

/** A 4-pole transistor-ladder lowpass: four cascaded leaky integrators, each
    driven toward a tanh-saturated target, inside a global negative-feedback
    loop that raises resonance and, near the top of its travel, self-
    oscillates - the textbook topology every Moog-style ladder clone descends
    from (Huovilainen 2004; D'Angelo & Valimaki), not one particular pedal's
    tuning.

    Every stage's target is bounded to (-1, 1) by tanh and each stage is a
    convex blend of that target and its own previous state, so the state can
    never leave (-1, 1) either, whatever the resonance or the input - this
    filter cannot latch a non-finite value or blow up, by construction, so it
    carries none of the feedback-loop safety machinery Grainer's own
    recirculation needs.

    Runs its integrator at kOversample times the caller's sample rate (input
    held across the sub-steps, not interpolated) so the tanh stages'
    harmonics - loudest right where resonance is loudest, near self-
    oscillation - alias far above the audible range instead of into it. */
class LadderFilter
{
public:
    void reset() noexcept { s1 = s2 = s3 = s4 = 0.0f; }

    /** cutoffHz clamped to a safe range under the caller's own Nyquist;
        resonance01 0 (flat, no feedback) to 1 (feedback just past the
        classic 4-pole unity-loop-gain point, so the top of the knob's travel
        genuinely self-oscillates at the cutoff). Cheap to call every block -
        recomputes the coefficients unconditionally, but does no allocation
        or transcendental work beyond one exp(). */
    void setCutoffAndResonance (float cutoffHz, float resonance01, double sampleRate) noexcept
    {
        const float sr = static_cast<float> (sampleRate > 0.0 ? sampleRate : 44100.0);
        const float hz = std::clamp (cutoffHz, 20.0f, sr * 0.45f);
        const float oversampledSr = sr * static_cast<float> (kOversample);

        g = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / oversampledSr);
        k = std::clamp (resonance01, 0.0f, 1.0f) * kMaxFeedback;

        // A ladder loses gain across its whole passband as the feedback rises
        // - exactly 1/(1+k) at DC - so without this, turning resonance up
        // sinks everything below the corner instead of raising a peak above
        // it. Only what leaves the loop is scaled; the loop itself, and so the
        // point it starts oscillating, is untouched.
        passbandComp = 1.0f + k;
    }

    float process (float x) noexcept
    {
        for (int i = 0; i < kOversample; ++i)
        {
            const float in1 = std::tanh (x - k * s4);
            s1 += g * (in1 - s1);
            s2 += g * (std::tanh (s1) - s2);
            s3 += g * (std::tanh (s2) - s3);
            s4 += g * (std::tanh (s3) - s4);
        }

        // Without this the tail lands in the denormals and effectively latches
        // there - measured 5.6e-45 still on the output six seconds after the
        // input stopped, because a denormal multiplied by a coefficient just
        // under 1 rounds back to itself. Same threshold and the same reason as
        // ee::dsp::Grainer's own highpass squelch.
        squelch (s1);
        squelch (s2);
        squelch (s3);
        squelch (s4);

        return s4 * passbandComp;
    }

private:
    static void squelch (float& s) noexcept
    {
        if (std::abs (s) < 1.0e-20f)
            s = 0.0f;
    }

    static constexpr int kOversample = 4;

    // The ideal continuous-time 4-pole feedback loop hits unity gain at the
    // cutoff when k = 4; a hair past it so the knob's very top reliably
    // starts and sustains oscillation rather than only approaching it.
    static constexpr float kMaxFeedback = 4.2f;

    float g = 0.0f;
    float k = 0.0f;
    float passbandComp = 1.0f;
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
};

} // namespace ee::dsp
