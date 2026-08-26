#pragma once

#include "ModDelayLine.h"

namespace ee::dsp
{

/** Schroeder allpass, used here to smear the input before it reaches the FDN. */
class Allpass
{
public:
    void prepare (double sampleRate, float maxDelaySeconds)
    {
        line.prepare (sampleRate, maxDelaySeconds);
    }

    void reset() { line.reset(); }

    void setDelaySamples (float samples) noexcept { delaySamples = samples; }
    void setCoefficient (float k) noexcept { coeff = k; }

    float process (float x) noexcept
    {
        const float delayed = line.read (delaySamples);
        const float v = x + coeff * delayed;
        line.write (v);
        line.advance();
        return delayed - coeff * v;
    }

private:
    ModDelayLine line;
    float delaySamples = 1.0f;
    float coeff = 0.62f;
};

} // namespace ee::dsp
