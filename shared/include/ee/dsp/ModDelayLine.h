#pragma once

#include <algorithm>
#include <vector>
#include <cmath>

namespace ee::dsp
{

/** Circular delay line with fractional (cubic Hermite) reads.

    Read before write when used inside a feedback loop.

    The interpolator matters more than it looks: a linear one loses about 3 dB
    at a quarter of the sample rate on a half-sample offset, and inside a
    feedback loop that runs thirty times a second the whole top octave is gone
    before the tail is audible. Hermite keeps that loss under a decibel.
*/
class ModDelayLine
{
public:
    void prepare (double sampleRate, float maxDelaySeconds)
    {
        size = static_cast<int> (sampleRate * maxDelaySeconds) + 4;
        buffer.assign (static_cast<size_t> (size), 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    float read (float delaySamples) const noexcept
    {
        const float maxDelay = static_cast<float> (size - 3);
        if (delaySamples < 2.0f)      delaySamples = 2.0f;
        if (delaySamples > maxDelay)  delaySamples = maxDelay;

        float readPos = static_cast<float> (writeIndex) - delaySamples;
        if (readPos < 0.0f)
            readPos += static_cast<float> (size);

        const int i1 = static_cast<int> (readPos);
        const float frac = readPos - static_cast<float> (i1);

        const int i0 = i1 > 0 ? i1 - 1 : size - 1;
        int i2 = i1 + 1; if (i2 >= size) i2 -= size;
        int i3 = i2 + 1; if (i3 >= size) i3 -= size;

        const float y0 = buffer[static_cast<size_t> (i0)];
        const float y1 = buffer[static_cast<size_t> (i1)];
        const float y2 = buffer[static_cast<size_t> (i2)];
        const float y3 = buffer[static_cast<size_t> (i3)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    }

    void write (float x) noexcept
    {
        buffer[static_cast<size_t> (writeIndex)] = x;
    }

    void advance() noexcept
    {
        if (++writeIndex >= size)
            writeIndex = 0;
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
    int size = 0;
};

} // namespace ee::dsp
