// Renders a file through the whole Easy Delay processor, so the chain can be
// checked end to end rather than a stage at a time.
#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.h"

namespace
{
    void setParam (juce::AudioProcessorValueTreeState& state, const char* id, float percent)
    {
        if (auto* param = state.getParameter (id))
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, percent * 0.01f));
    }
}

int main (int argc, char* argv[])
{
    if (argc < 5)
    {
        std::printf ("usage: ee_delay_match <in.wav> <out.wav> <tape%%> <mix%%> [feedback%%] [mod%%]\n");
        return 1;
    }

    const juce::File inFile { juce::String (argv[1]) };
    const juce::File outFile { juce::String (argv[2]) };

    const float tape = juce::String (argv[3]).getFloatValue();
    const float mix = juce::String (argv[4]).getFloatValue();
    const float feedback = argc > 5 ? juce::String (argv[5]).getFloatValue() : 35.0f;
    const float mod = argc > 6 ? juce::String (argv[6]).getFloatValue() : 0.0f;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("could not read %s\n", inFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const int numSamples = static_cast<int> (reader->lengthInSamples);

    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();
    reader->read (&buffer, 0, numSamples, 0, true, true);

    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    EasyDelayProcessor processor;

    setParam (processor.apvts, "tape", tape);
    setParam (processor.apvts, "mix", mix);
    setParam (processor.apvts, "fb", feedback);
    setParam (processor.apvts, "mod", mod);

    constexpr int block = 512;
    processor.setPlayConfigDetails (2, 2, reader->sampleRate, block);
    processor.prepareToPlay (reader->sampleRate, block);

    juce::MidiBuffer midi;

    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int chunk = juce::jmin (block, numSamples - offset);

        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);
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

    std::printf ("wrote %s  (tape %.0f %%, mix %.0f %%, feedback %.0f %%, mod %.0f %%, latency %d)\n",
                 outFile.getFullPathName().toRawUTF8(), tape, mix, feedback, mod,
                 processor.getLatencySamples());
    return 0;
}
