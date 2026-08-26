#pragma once

#include <algorithm>

namespace ee::dsp
{

/** One-pole absorbent filter for a reverb feedback path.

    Unity gain at DC scaled by `gain`; a separate gain at Nyquist set by the
    low/high ratio. That is what makes highs and lows decay at different rates.
*/
class OnePoleDamper
{
public:
    /** @param gain            loop gain at DC
        @param highToLowRatio  Nyquist gain divided by DC gain (>1 = brighter tail)
    */
    void set (float gain, float highToLowRatio) noexcept
    {
        g = gain;
        const float r = std::clamp (highToLowRatio, 0.02f, 4.0f);
        d = std::clamp ((1.0f - r) / (1.0f + r), -0.9f, 0.9f);
    }

    void reset() noexcept { z = 0.0f; }

    float process (float x) noexcept
    {
        z = (1.0f - d) * x + d * z;
        return g * z;
    }

private:
    float g = 0.0f;
    float d = 0.0f;
    float z = 0.0f;
};

} // namespace ee::dsp
