#pragma once

#include <algorithm>
#include <cmath>

namespace ee::dsp
{

/** Feedback-loop absorber with independent low, mid and high decay rates.

    Both bands are shelves rather than rolloffs. A one-pole lowpass would keep
    eating into the top octave every trip round the loop, which collapses the
    air band to a fraction of the nominal decay and leaves a dull thud. A shelf
    holds a fixed ratio across the whole band instead, which is what a plate
    actually does.

    Neither shelf can exceed unity, so loop gain stays bounded by `midGain`.
*/
class LoopDamper
{
public:
    void prepare (double sampleRate, float lowCornerHz, float highCornerHz) noexcept
    {
        lowCoeff = onePoleCoeff (lowCornerHz, sampleRate);
        highCoeff = onePoleCoeff (highCornerHz, sampleRate);
    }

    /** @param midGain    loop gain through the middle of the spectrum
        @param lowRatio   gain below the low corner, relative to midGain
        @param highRatio  gain above the high corner, relative to midGain
    */
    void set (float midGain, float lowRatio, float highRatio) noexcept
    {
        g = midGain;
        lowGain = std::clamp (lowRatio, 0.05f, 1.0f);
        highGain = std::clamp (highRatio, 0.05f, 1.0f);
    }

    void reset() noexcept
    {
        lowState = 0.0f;
        highState = 0.0f;
    }

    float process (float x) noexcept
    {
        // High shelf: unity below the corner, highGain above it.
        highState += highCoeff * (x - highState);
        float y = highGain * x + (1.0f - highGain) * highState;

        // Low shelf: unity above the corner, lowGain below it.
        lowState += lowCoeff * (y - lowState);
        y -= (1.0f - lowGain) * lowState;

        return g * y;
    }

private:
    static float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = 2.0f * 3.14159265f * cornerHz / static_cast<float> (sampleRate);
        return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
    }

    float g = 0.0f;
    float lowGain = 1.0f;
    float highGain = 1.0f;
    float lowCoeff = 0.05f;
    float highCoeff = 0.30f;
    float lowState = 0.0f;
    float highState = 0.0f;
};

} // namespace ee::dsp
