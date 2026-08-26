#pragma once

#include <algorithm>
#include <vector>
#include <cmath>

namespace ee::dsp
{

/** Circular delay line with fractional (linearly interpolated) reads.

    Read before write when used inside a feedback loop.
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
        const float maxDelay = static_cast<float> (size - 2);
        if (delaySamples < 1.0f)      delaySamples = 1.0f;
        if (delaySamples > maxDelay)  delaySamples = maxDelay;

        float readPos = static_cast<float> (writeIndex) - delaySamples;
        if (readPos < 0.0f)
            readPos += static_cast<float> (size);

        const int i0 = static_cast<int> (readPos);
        const float frac = readPos - static_cast<float> (i0);
        int i1 = i0 + 1;
        if (i1 >= size)
            i1 -= size;

        const float a = buffer[static_cast<size_t> (i0)];
        const float b = buffer[static_cast<size_t> (i1)];
        return a + frac * (b - a);
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
