#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <memory>

#include "PluginProcessor.h"

/** Offline renderer used to A/B Peak Spring against a reference recording.

    Runs a dry file through the real PeakSpringProcessor, so the mix law, the
    wet trim and the parameter smoothing are all exercised rather than
    reimplemented in the harness.

        ee_spring_match in.wav out.wav <decaySeconds> <mixPercent> [stereo 0|1]

    Pass a one-sample impulse as in.wav to capture the tank's own response.
*/
int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf ("usage: %s in.wav out.wav decaySeconds mixPercent [stereo]\n", argv[0]);
        return 2;
    }

    const juce::File inFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    const auto decay = static_cast<float> (juce::String (argv[3]).getDoubleValue());
    const auto mix = static_cast<float> (juce::String (argv[4]).getDoubleValue());
    const bool stereo = argc > 5 ? juce::String (argv[5]).getIntValue() != 0 : true;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("could not read %s\n", inFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto sampleRate = reader->sampleRate;
    const auto numSamples = static_cast<int> (reader->lengthInSamples);
    constexpr int blockSize = 512;

    // The reference files are exactly as long as the dry, tail included, so
    // render to the same length rather than past the end of it - anything else
    // would not line up sample for sample.
    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();
    reader->read (&buffer, 0, numSamples, 0, true, true);

    PeakSpringProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    auto set = [&processor] (const char* id, float value)
    {
        if (auto* p = processor.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    set ("decay", decay);
    set ("mix", mix);
    set ("stereo", stereo ? 1.0f : 0.0f);
    set ("on", 1.0f);

    juce::MidiBuffer midi;
    for (int pos = 0; pos < numSamples; pos += blockSize)
    {
        const int chunk = juce::jmin (blockSize, numSamples - pos);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, pos, chunk);
        processor.processBlock (slice, midi);
    }

    processor.releaseResources();

    outFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (outFile.createOutputStream());
    if (stream == nullptr)
    {
        std::printf ("could not write %s\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), sampleRate, 2, 24, {}, 0));

    if (writer == nullptr)
    {
        std::printf ("could not create writer\n");
        return 1;
    }

    writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    writer.reset();

    std::printf ("rendered %s -> %s  (decay %.2f s, mix %.0f %%, %s)\n", inFile.getFileName().toRawUTF8(),
                 outFile.getFileName().toRawUTF8(), decay, mix, stereo ? "stereo" : "mono");
    return 0;
}
