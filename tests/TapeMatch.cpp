// Runs a wav through the tape colour stage on its own, so its voicing can be
// scored against a reference recording of the same take.
#include <juce_audio_formats/juce_audio_formats.h>

#include "ee/dsp/TapeCharacter.h"

int main (int argc, char* argv[])
{
    if (argc < 4)
    {
        std::printf ("usage: ee_tape_match <in.wav> <out.wav> <amount 0-100>\n");
        return 1;
    }

    const juce::File inFile { juce::String (argv[1]) };
    const juce::File outFile { juce::String (argv[2]) };
    const float amount = juce::jlimit (0.0f, 1.0f, juce::String (argv[3]).getFloatValue() * 0.01f);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

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

    ee::dsp::TapeCharacter tape;
    tape.prepare (reader->sampleRate);
    tape.setAmount (amount);
    tape.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    outFile.deleteFile();

    juce::WavAudioFormat wav;
    if (auto stream = outFile.createOutputStream())
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), reader->sampleRate, 2, 24, {}, 0));

        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    }

    std::printf ("wrote %s (%d samples, tape %.0f %%, latency %d samples)\n",
                 outFile.getFullPathName().toRawUTF8(), numSamples, amount * 100.0f,
                 tape.getLatencySamples());
    return 0;
}
