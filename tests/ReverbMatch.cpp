#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <memory>

#include "PluginProcessor.h"

/** Offline renderer used to A/B the plugin against a reference recording.

    Runs a dry file through the real EasyReverbProcessor, so the mix law, wet
    trim and parameter smoothing are all exercised rather than reimplemented.

        ee_reverb_match in.wav out.wav <decaySeconds> <mixPercent> <highCutHz>
*/
int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::printf ("usage: %s in.wav out.wav decaySeconds mixPercent highCutHz\n", argv[0]);
        return 2;
    }

    const juce::File inFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    const auto decay = static_cast<float> (juce::String (argv[3]).getDoubleValue());
    const auto mix = static_cast<float> (juce::String (argv[4]).getDoubleValue());
    const auto highCut = static_cast<float> (juce::String (argv[5]).getDoubleValue());

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

    // Render past the end of the file so the tail is captured rather than cut.
    const int tailSamples = static_cast<int> (sampleRate * 12.0);
    const int total = numSamples + tailSamples;

    juce::AudioBuffer<float> buffer (2, total);
    buffer.clear();
    reader->read (&buffer, 0, numSamples, 0, true, true);

    EasyReverbProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    auto set = [&processor] (const char* id, float value)
    {
        if (auto* p = processor.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    set ("decay", decay);
    set ("mix", mix);
    set ("hicut", highCut);
    set ("on", 1.0f);

    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int chunk = juce::jmin (blockSize, total - pos);
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
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), sampleRate, 2, 24, {}, 0));

    if (writer == nullptr)
    {
        std::printf ("could not create writer\n");
        return 1;
    }

    writer->writeFromAudioSampleBuffer (buffer, 0, total);
    writer.reset();

    std::printf ("rendered %s -> %s  (decay %.2f s, mix %.0f %%, high cut %.0f Hz)\n",
                 inFile.getFileName().toRawUTF8(),
                 outFile.getFileName().toRawUTF8(),
                 decay, mix, highCut);
    return 0;
}
