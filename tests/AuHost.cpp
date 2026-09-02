// Loads an installed Audio Unit through JUCE's AU plugin host and runs signal
// through it, reporting the output level second by second.
//
// This is the path a host actually takes - component scan, bus negotiation, the
// AU wrapper's own buffer handling - none of which a test that instantiates the
// processor class directly exercises.
//
//   ee_au_host "AudioUnit:Effects/aufx,Pgrn,Peak" [--sr 48000] [--block 512] [--level -20] [--seconds 30]

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <random>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String wanted = argc > 1 ? juce::String (argv[1]) : juce::String ("AudioUnit:Effects/aufx,Pgrn,Peak");
    double sampleRate = 48000.0;
    int block = 512;
    float inputDb = -20.0f;
    double seconds = 30.0;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };

        if (arg == "--sr")           sampleRate = next().getDoubleValue();
        else if (arg == "--block")   block = next().getIntValue();
        else if (arg == "--level")   inputDb = static_cast<float> (next().getDoubleValue());
        else if (arg == "--seconds") seconds = next().getDoubleValue();
    }

    // The headless audio_processors module deletes addDefaultFormats, so the
    // one format this needs is registered by hand.
    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::AudioUnitPluginFormat());

    juce::AudioUnitPluginFormat au;
    juce::OwnedArray<juce::PluginDescription> found;

    // Addressed by identifier rather than by scanning every installed Audio
    // Unit: a full scan loads third-party components into this process, and at
    // least one of the ones on this machine takes it down with a SIGBUS.
    au.findAllTypesForFile (found, wanted);

    if (found.isEmpty())
    {
        std::printf ("no Audio Unit at identifier \"%s\"\n", wanted.toRawUTF8());
        return 1;
    }

    const auto& description = *found.getFirst();
    std::printf ("%s - %s %s (%s)\n", description.name.toRawUTF8(), description.manufacturerName.toRawUTF8(),
                 description.version.toRawUTF8(), description.pluginFormatName.toRawUTF8());

    juce::String error;
    auto plugin = formats.createPluginInstance (description, sampleRate, block, error);

    if (plugin == nullptr)
    {
        std::printf ("could not instantiate: %s\n", error.toRawUTF8());
        return 1;
    }

    plugin->setPlayConfigDetails (2, 2, sampleRate, block);
    plugin->prepareToPlay (sampleRate, block);

    std::printf ("%d parameters, tail %.2f s, latency %d\n", plugin->getParameters().size(),
                 plugin->getTailLengthSeconds(), plugin->getLatencySamples());

    for (auto* parameter : plugin->getParameters())
        std::printf ("    %-24s %s\n", parameter->getName (24).toRawUTF8(),
                     parameter->getCurrentValueAsText().toRawUTF8());

    std::printf ("\n%.0f Hz, block %d, %g dBFS noise, %.0f s\n\n", sampleRate, block, inputDb, seconds);
    std::printf ("  %5s  %10s  %10s\n", "sec", "peak", "rms");

    const float amplitude = juce::Decibels::decibelsToGain (inputDb);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-amplitude, amplitude);

    const int blocksPerSecond = juce::jmax (1, static_cast<int> (sampleRate / block));
    const int totalBlocks = static_cast<int> (seconds * sampleRate / block);

    float secondPeak = 0.0f;
    double secondSquares = 0.0;
    long long secondCount = 0;

    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* p = buffer.getWritePointer (ch);
            for (int i = 0; i < block; ++i)
                p[i] = noise (rng);
        }

        plugin->processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            for (int i = 0; i < block; ++i)
            {
                secondPeak = juce::jmax (secondPeak, std::abs (p[i]));
                secondSquares += static_cast<double> (p[i]) * p[i];
                ++secondCount;
            }
        }

        // A console app dispatches no messages of its own, so anything the
        // plugin does on the message thread - a meter, a timer, a diagnostic
        // log - would never run. Hosts pump it constantly; give it a slice.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

        if ((b + 1) % blocksPerSecond == 0)
        {
            const float rms = static_cast<float> (std::sqrt (secondSquares / static_cast<double> (secondCount)));
            std::printf ("  %5d  %10.4f  %10.4f%s\n", (b + 1) / blocksPerSecond, secondPeak, rms,
                         secondPeak > 2.0f ? "   <-- LOUD" : "");

            secondPeak = 0.0f;
            secondSquares = 0.0;
            secondCount = 0;
        }
    }

    plugin->releaseResources();
    return 0;
}
