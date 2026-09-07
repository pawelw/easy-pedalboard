// Renders a file through the whole Peak Delay processor, so the chain can be
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

    void setFlag (juce::AudioProcessorValueTreeState& state, const char* id, bool on)
    {
        if (auto* param = state.getParameter (id))
            param->setValueNotifyingHost (on ? 1.0f : 0.0f);
    }

    /** `--name value` anywhere after the positional arguments. The four
        controls the positional list does not reach - the pedal has eight now,
        and a ninth positional argument nobody can remember the order of is
        worse than a flag. */
    float flagValue (int argc, char* argv[], const char* name, float fallback)
    {
        for (int i = 5; i + 1 < argc; ++i)
            if (juce::String (argv[i]) == name)
                return juce::String (argv[i + 1]).getFloatValue();

        return fallback;
    }

    juce::String flagText (int argc, char* argv[], const char* name, const char* fallback)
    {
        for (int i = 5; i + 1 < argc; ++i)
            if (juce::String (argv[i]) == name)
                return juce::String (argv[i + 1]);

        return juce::String (fallback);
    }

    /** `--type normal|wide|pingpong`, matched against kTypeID's own choice
        names so the tool cannot drift out of step with the parameter. */
    int routingIndex (juce::AudioProcessorValueTreeState& state, const juce::String& wanted)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter ("dtype")))
            for (int i = 0; i < param->choices.size(); ++i)
                if (param->choices[i].removeCharacters (" ").equalsIgnoreCase (wanted.removeCharacters (" -_")))
                    return i;

        return -1;
    }

    bool hasFlag (int argc, char* argv[], const char* name)
    {
        for (int i = 5; i < argc; ++i)
            if (juce::String (argv[i]) == name)
                return true;

        return false;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 5)
    {
        std::printf ("usage: ee_delay_match <in.wav> <out.wav> <wear%%> <mix%%> [feedback%%] [drift%%]\n"
                     "                      [--flutter %%] [--phaser %%] [--tape-post]\n"
                     "                      [--type normal|wide|pingpong]\n");
        return 1;
    }

    const juce::File inFile { juce::String (argv[1]) };
    const juce::File outFile { juce::String (argv[2]) };

    const float wear = juce::String (argv[3]).getFloatValue();
    const float mix = juce::String (argv[4]).getFloatValue();
    // Guarded against the flags below, so `... 35 20 --flutter 60` reads the
    // 20 as Chorus rather than the "--flutter" as a number (which parses as 0).
    const auto positional = [argc, argv] (int index, float fallback)
    {
        return index < argc && ! juce::String (argv[index]).startsWith ("--")
                   ? juce::String (argv[index]).getFloatValue()
                   : fallback;
    };

    const float feedback = positional (5, 35.0f);
    const float drift = positional (6, 0.0f);

    const float flutter = flagValue (argc, argv, "--flutter", 0.0f);
    const float phaser = flagValue (argc, argv, "--phaser", 0.0f);
    const bool tapePost = hasFlag (argc, argv, "--tape-post");
    const juce::String type = flagText (argc, argv, "--type", "normal");

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

    PeakDelayProcessor processor;

    // "tape" and "mod" are the ids Wear and Drift kept when the single-knob
    // stages became two-knob sections - see plugins/peak-delay's processor.
    setParam (processor.apvts, "tape", wear);
    setParam (processor.apvts, "mix", mix);
    setParam (processor.apvts, "fb", feedback);
    setParam (processor.apvts, "mod", drift);
    setParam (processor.apvts, "flutter", flutter);
    setParam (processor.apvts, "phaser", phaser);
    setFlag (processor.apvts, "tapepre", ! tapePost);

    const int typeIndex = routingIndex (processor.apvts, type);

    if (typeIndex < 0)
    {
        std::printf ("unknown --type '%s'\n", type.toRawUTF8());
        return 1;
    }

    if (auto* typeParam = processor.apvts.getParameter ("dtype"))
        typeParam->setValueNotifyingHost (typeParam->convertTo0to1 (static_cast<float> (typeIndex)));

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

    std::printf ("wrote %s\n"
                 "  tape %s: wear %.0f %%, flutter %.0f %%\n"
                 "  mod: drift %.0f %% (in the loop), phaser %.0f %% (on the repeats)\n"
                 "  type %s, mix %.0f %%, feedback %.0f %%, latency %d\n",
                 outFile.getFullPathName().toRawUTF8(), tapePost ? "post" : "pre", wear, flutter,
                 drift, phaser,
                 dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("dtype"))
                     ->choices[typeIndex]
                     .toRawUTF8(),
                 mix, feedback, processor.getLatencySamples());
    return 0;
}
