#pragma once

#include <algorithm>
#include <cmath>

namespace ee::dsp
{

/** A safety-net brickwall limiter: feed-forward, no lookahead, stereo-linked
    (the gain reduction is computed once from whichever channel is louder and
    applied to both, so limiting never shifts the stereo image).

    This is not a voicing tool a pedal exposes on its face - it exists to
    catch a peak the pedal's own gain staging did not anticipate (two
    overlapping events summing past 0 dBFS) rather than to be dialed in by
    ear. Because it has no lookahead, a peak that rises faster than the
    attack time can still poke a little over the ceiling before the gain
    catches it; that is the tradeoff for adding no latency. */
class PeakLimiter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
        gain = 1.0f;
    }

    void reset() noexcept { gain = 1.0f; }

    /** Linear gain floor the envelope is not allowed to pull below - keeps a
        big enough transient from being crushed to silence rather than just
        capped. Defaults to no floor. */
    void setCeilingDb (float db) noexcept { ceilingLinear = std::pow (10.0f, db * 0.05f); }

    void setAttackMs (float ms) noexcept
    {
        attackMs = std::max (0.01f, ms);
        updateCoefficients();
    }

    void setReleaseMs (float ms) noexcept
    {
        releaseMs = std::max (1.0f, ms);
        updateCoefficients();
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // The gain a sample this loud would need right now, with no
            // ballistics - smoothing this (rather than smoothing the level
            // first and deriving gain from that) is what lets the leading
            // edge of a fast transient start getting pulled down from its
            // very first sample instead of waiting for a lagging envelope to
            // notice it arrived.
            const float peak = std::max (std::abs (left[i]), std::abs (right[i]));
            const float targetGain = peak > ceilingLinear ? ceilingLinear / peak : 1.0f;

            const float coeff = targetGain < gain ? attackCoeff : releaseCoeff;
            gain += (targetGain - gain) * coeff;

            left[i] *= gain;
            right[i] *= gain;
        }
    }

private:
    void updateCoefficients() noexcept
    {
        attackCoeff = 1.0f - std::exp (-1.0f / (0.001f * attackMs * static_cast<float> (sr)));
        releaseCoeff = 1.0f - std::exp (-1.0f / (0.001f * releaseMs * static_cast<float> (sr)));
    }

    double sr = 48000.0;
    float ceilingLinear = 1.0f;
    float attackMs = 1.0f;
    float releaseMs = 60.0f;
    float attackCoeff = 1.0f;
    float releaseCoeff = 1.0f;
    float gain = 1.0f;
};

} // namespace ee::dsp
