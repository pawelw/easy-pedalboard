#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ee::dsp
{

/** Mid/side Haas widener: the side channel gets a short-delayed copy of the mid.

        M = (L + R) / 2,   S = (L - R) / 2
        S' = S + width * M(t - delay)
        L' = M + S',  R' = M - S'

    So the left channel hears the mid plus its echo and the right hears the mid
    minus it - complementary comb filters that read as a wide image, where a
    plain delay on one channel only shifts it. The echo cancels exactly in the
    mono sum (L' + R' = L + R), so a widened signal folds down to mono unchanged.

    This is the structure measured on the reference grainer BitBit Grain's
    Mono/Stereo switch was modelled on: 6.83 ms, side gain ~0.9 - see
    config::kHaasDelayMs.

    Off means off: once the width ramp has settled at 0 the samples pass through
    untouched (not recomputed as M + S, which is not bit-exact in float), so a
    signal that never engages the widener is bit-identical to one without it.
    The delay line keeps filling either way, so switching on has history rather
    than a silent first few milliseconds. */
class HaasWidener
{
public:
    void prepare (double sampleRate, float delayMs, float rampMs)
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        delaySamples = std::max (1, static_cast<int> (std::lround (delayMs * 0.001 * fs)));
        line.assign (static_cast<size_t> (delaySamples), 0.0f);
        rampStep = 1.0f / std::max (1.0f, rampMs * 0.001f * static_cast<float> (fs));
        reset();
    }

    void reset()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        index = 0;
        current = target;
    }

    /** 0 = off, 1 = full width. Ramped, so switching does not click. */
    void setWidth (float width01) noexcept { target = std::clamp (width01, 0.0f, 1.0f); }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (line.empty() || left == nullptr || right == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float mid = 0.5f * (left[i] + right[i]);
            const float echo = line[static_cast<size_t> (index)];
            line[static_cast<size_t> (index)] = std::isfinite (mid) ? mid : 0.0f;
            if (++index >= delaySamples)
                index = 0;

            if (current != target)
                current = current < target ? std::min (target, current + rampStep)
                                           : std::max (target, current - rampStep);

            if (current <= 0.0f)
                continue;

            const float side = 0.5f * (left[i] - right[i]) + current * echo;
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }

private:
    std::vector<float> line;
    int delaySamples = 1;
    int index = 0;
    float target = 0.0f;
    float current = 0.0f;
    float rampStep = 1.0f;
};

} // namespace ee::dsp
