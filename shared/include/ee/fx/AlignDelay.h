#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ee::fx
{

/** A whole-sample delay applied in place, used only to line things up: a
    module's engines with each other and with its own dry path, and a bypass
    reference with the latency the plugin reports, so switching off does not pull
    the signal ahead of what the host compensated. Not a DSP delay line:
    there is no interpolation and no feedback, because nothing here ever moves -
    an engine's latency is fixed by its own construction. */
class AlignDelay
{
public:
    void prepare (int numChannels, int lengthSamples)
    {
        length = juce::jmax (0, lengthSamples);

        if (length == 0)
            return;

        line.setSize (numChannels, length, false, true, true);
        line.clear();
        writeIndex = 0;
    }

    void reset() noexcept
    {
        if (length > 0)
            line.clear();

        writeIndex = 0;
    }

    void process (juce::AudioBuffer<float>& io, int numChannels, int numSamples) noexcept
    {
        if (length == 0)
            return;

        const int numCh = juce::jmin (numChannels, line.getNumChannels());
        int w = writeIndex;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = io.getWritePointer (ch);
                float* stored = line.getWritePointer (ch);

                const float out = stored[w];
                stored[w] = d[i];
                d[i] = out;
            }

            if (++w >= length)
                w = 0;
        }

        writeIndex = w;
    }

    int getLength() const noexcept { return length; }

private:
    juce::AudioBuffer<float> line;
    int length = 0;
    int writeIndex = 0;
};

} // namespace ee::fx
