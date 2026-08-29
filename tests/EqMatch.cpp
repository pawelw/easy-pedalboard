// Renders audio through the whole Easy EQ processor so the band filters and the
// bypass crossfade can be checked end to end.
//
//   ee_eq_match synth out.wav <b100dB> <b200dB> ... <b6k4dB> [levelDB]
//   ee_eq_match in.wav  out.wav <b100dB> ...
//
// "synth" as the input renders two seconds of white noise instead of reading a
// file, so the tool needs nothing on disk to exercise the chain.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

#include "PluginProcessor.h"

namespace
{
    const char* kBandIDs[EasyEqProcessor::kNumBands] = {
        "b100", "b200", "b400", "b800", "b1k6", "b3k2", "b6k4"
    };

    void setDb (juce::AudioProcessorValueTreeState& state, const char* id, float db)
    {
        if (auto* param = state.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (juce::jlimit (-15.0f, 15.0f, db)));
    }

    double rms (const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                sum += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);

        return std::sqrt (sum / (b.getNumChannels() * juce::jmax (1, b.getNumSamples())));
    }
}

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::printf ("usage: ee_eq_match <in.wav|synth> <out.wav> [b100dB ... b6k4dB] [levelDB]\n");
        return 1;
    }

    const juce::String inArg { argv[1] };
    const juce::File outFile { juce::String (argv[2]) };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    double sampleRate = 48000.0;
    int numSamples = 0;
    juce::AudioBuffer<float> buffer;

    if (inArg == "synth")
    {
        numSamples = static_cast<int> (sampleRate * 2.0);
        buffer.setSize (2, numSamples);

        juce::Random rng (0x51342);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.25f);
    }
    else
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (inArg)));
        if (reader == nullptr)
        {
            std::printf ("could not read %s\n", inArg.toRawUTF8());
            return 1;
        }

        sampleRate = reader->sampleRate;
        numSamples = static_cast<int> (reader->lengthInSamples);
        buffer.setSize (2, numSamples);
        buffer.clear();
        reader->read (&buffer, 0, numSamples, 0, true, true);

        if (reader->numChannels == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    }

    const double inputRms = rms (buffer);

    EasyEqProcessor processor;

    for (int b = 0; b < EasyEqProcessor::kNumBands; ++b)
    {
        const int argIndex = 3 + b;
        const float db = argc > argIndex ? juce::String (argv[argIndex]).getFloatValue() : 0.0f;
        setDb (processor.apvts, kBandIDs[b], db);
    }

    const int levelIndex = 3 + EasyEqProcessor::kNumBands;
    if (argc > levelIndex)
        setDb (processor.apvts, "level", juce::String (argv[levelIndex]).getFloatValue());

    // Optional trailing: <loCutHz> <hiCutHz>
    auto setHz = [&processor] (const char* id, float hz)
    {
        if (auto* p = processor.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (hz));
    };
    if (argc > levelIndex + 1) setHz ("locut", juce::String (argv[levelIndex + 1]).getFloatValue());
    if (argc > levelIndex + 2) setHz ("hicut", juce::String (argv[levelIndex + 2]).getFloatValue());

    constexpr int block = 512;
    processor.setPlayConfigDetails (2, 2, sampleRate, block);
    processor.prepareToPlay (sampleRate, block);

    juce::MidiBuffer midi;
    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int chunk = juce::jmin (block, numSamples - offset);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);
    }

    const double outputRms = rms (buffer);

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    if (auto stream = outFile.createOutputStream())
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), sampleRate, 2, 24, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    }

    std::printf ("wrote %s  in RMS %.5f  out RMS %.5f  (%+.2f dB)\n",
                 outFile.getFullPathName().toRawUTF8(),
                 inputRms, outputRms,
                 juce::Decibels::gainToDecibels (outputRms / juce::jmax (1.0e-9, inputRms)));
    return 0;
}
