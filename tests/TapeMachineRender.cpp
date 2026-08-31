// Runs a wav through the whole tape machine, so a voicing can be scored against
// a reference recording of the same take. Every knob is on the command line, so
// one part of the machine can be measured with the rest of it out of the way.
#include <juce_audio_formats/juce_audio_formats.h>

#include "ee/dsp/TapeMachine.h"

#include <vector>

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::printf ("usage: ee_tape_render <in.wav> <out.wav>"
                     " [sat 0-100] [wear 0-100] [flutter 0-100]"
                     " [tone -100..100] [stereo 0/1] [noise 0-100] [floor.wav]\n");
        return 1;
    }

    const juce::File inFile { juce::String (argv[1]) };
    const juce::File outFile { juce::String (argv[2]) };

    const auto arg = [argc, argv] (int index, float fallback)
    {
        return index < argc ? juce::String (argv[index]).getFloatValue() : fallback;
    };

    const float sat     = juce::jlimit (0.0f, 1.0f, arg (3, 0.0f) * 0.01f);
    const float wear    = juce::jlimit (0.0f, 1.0f, arg (4, 0.0f) * 0.01f);
    const float flutter = juce::jlimit (0.0f, 1.0f, arg (5, 0.0f) * 0.01f);
    const float tone    = juce::jlimit (-1.0f, 1.0f, arg (6, 0.0f) * 0.01f);
    const float stereo  = arg (7, 0.0f) > 0.5f ? 1.0f : 0.0f;
    const float noise   = juce::jlimit (0.0f, 1.0f, arg (8, 0.0f) * 0.01f);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // Optional: the tape floor recording the pedal embeds. Without it the engine
    // falls back to synthesised hiss, which is fine for voicing everything else.
    juce::AudioBuffer<float> floorSample;
    std::vector<const float*> floorChannels;
    double floorRate = 44100.0;

    if (argc > 9)
    {
        const juce::File floorFile { juce::String (argv[9]) };
        std::unique_ptr<juce::AudioFormatReader> floorReader (formats.createReaderFor (floorFile));

        if (floorReader == nullptr)
        {
            std::printf ("could not read %s\n", floorFile.getFullPathName().toRawUTF8());
            return 1;
        }

        const int floorLength = static_cast<int> (floorReader->lengthInSamples);
        const int floorNumChannels = juce::jlimit (1, 2, static_cast<int> (floorReader->numChannels));

        floorSample.setSize (floorNumChannels, floorLength);
        floorReader->read (&floorSample, 0, floorLength, 0, true, floorNumChannels > 1);
        floorRate = floorReader->sampleRate;

        floorChannels.resize (static_cast<size_t> (floorNumChannels));
        for (int ch = 0; ch < floorNumChannels; ++ch)
            floorChannels[static_cast<size_t> (ch)] = floorSample.getReadPointer (ch);
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("could not read %s\n", inFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const int numSamples = static_cast<int> (reader->lengthInSamples);
    const int numChannels = static_cast<int> (reader->numChannels);

    juce::AudioBuffer<float> buffer (juce::jmax (2, numChannels), numSamples);
    buffer.clear();
    reader->read (&buffer, 0, numSamples, 0, true, true);

    if (numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    ee::dsp::TapeMachine machine;
    machine.prepare (reader->sampleRate);
    machine.reset();
    machine.setSaturation01 (sat);
    machine.setWear01 (wear);
    machine.setFlutter01 (flutter);
    machine.setTone (tone);
    machine.setStereo01 (stereo);
    machine.setNoise01 (noise);

    if (! floorChannels.empty())
        machine.setNoiseSample (floorChannels.data(), static_cast<int> (floorChannels.size()),
                                floorSample.getNumSamples(), floorRate);

    // In blocks, the way a host would call it.
    constexpr int kBlock = 512;
    for (int offset = 0; offset < numSamples; offset += kBlock)
    {
        const int chunk = juce::jmin (kBlock, numSamples - offset);
        machine.process (buffer.getWritePointer (0, offset),
                         buffer.getWritePointer (1, offset), chunk);
    }

    outFile.deleteFile();

    juce::WavAudioFormat wav;
    if (auto stream = outFile.createOutputStream())
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), reader->sampleRate, 2, 24, {}, 0));

        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    }

    std::printf ("wrote %s (%d samples, latency %d samples)\n"
                 "  sat %.0f %%  wear %.0f %%  flutter %.0f %%  tone %+.0f %%  stereo %s  noise %.0f %%\n",
                 outFile.getFullPathName().toRawUTF8(), numSamples, machine.getLatencySamples(),
                 sat * 100.0f, wear * 100.0f, flutter * 100.0f, tone * 100.0f,
                 stereo > 0.5f ? "on" : "off", noise * 100.0f);
    return 0;
}
