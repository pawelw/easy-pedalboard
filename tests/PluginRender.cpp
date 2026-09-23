// Loads an installed Audio Unit, sets its parameters by their displayed value,
// and renders a file (or a unit impulse) through it - so a third-party
// reference can be measured at exactly the settings we are matching, rather
// than deconvolved out of whatever someone happened to bounce.
//
//   ee_plugin_render <identifier> --list
//   ee_plugin_render <identifier> [--program N|Name] [--set "Name=Value"]...
//                    (--in dry.wav | --impulse) --out out.wav [--tail 12] [--sr 48000] [--block 512]
//
// A --set value is matched against the parameter's own text: "Decay=2.5"
// bisects the parameter until its display reads 2.5 (so units and skew are the
// plugin's business, not ours). Give the target a unit ("Decay=8s", "20ms",
// "2kHz") when the readout changes unit along its travel ("480 ms" .. "2.00 s").
// "Decay=n:0.4" sets the normalised value, and a non-numeric value
// ("Mode=Hall") picks the step whose text matches.
//
// Addressed by identifier, like ee_au_host, because a full AU scan loads every
// third-party component into the process.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

namespace
{
juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName (256).equalsIgnoreCase (name))
            return p;
    return nullptr;
}

bool isNumeric (const juce::String& s)
{
    return s.trim().containsOnly ("0123456789.-+") && s.trim().isNotEmpty();
}

/** Scale from a unit suffix to its base unit (s, Hz), or 0 if there is none we
    know. A readout that switches units part-way ("480 ms", then "2.00 s")
    is only monotonic once both are in the same unit. */
double unitScale (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t.endsWith ("khz")) return 1000.0;
    if (t.endsWith ("hz"))  return 1.0;
    if (t.endsWith ("ms"))  return 0.001;
    if (t.endsWith ("s"))   return 1.0;
    return 0.0;
}

double textNumber (juce::AudioProcessorParameter& p, float normalised, bool inBaseUnits)
{
    const auto text = p.getText (normalised, 256);
    const double v = text.retainCharacters ("0123456789.-").getDoubleValue();
    const double scale = inBaseUnits ? unitScale (text) : 0.0;
    return scale > 0.0 ? v * scale : v;
}

bool setParameter (juce::AudioProcessorParameter& p, const juce::String& value)
{
    float normalised = 0.0f;

    if (value.startsWith ("n:"))
    {
        normalised = static_cast<float> (value.substring (2).getDoubleValue());
    }
    else if (isNumeric (value) || (isNumeric (value.trimCharactersAtEnd ("kKmMsShHzZ")) && unitScale (value) > 0.0))
    {
        // Bisect on the displayed number for both edges of the band that
        // displays the target, and take its middle - a rounded display ("2.0 s")
        // covers a range, and either edge of it is really a different value.
        // Assumes the display rises with the normalised value; a falling one is
        // caught by comparing two interior points - not the ends, which are
        // often "Off" and read as 0.
        // A target with a unit ("8s", "20ms", "2kHz") is compared in base units,
        // for a readout that changes unit along its travel.
        const bool inBase = unitScale (value) > 0.0;
        const double target = value.getDoubleValue() * (inBase ? unitScale (value) : 1.0);
        const bool rising = textNumber (p, 0.75f, inBase) >= textNumber (p, 0.25f, inBase);
        const auto edge = [&] (bool inclusive)
        {
            float lo = 0.0f, hi = 1.0f;
            for (int i = 0; i < 40; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                const double shown = textNumber (p, mid, inBase);
                const bool before = inclusive ? shown < target : shown <= target;
                if (before == rising)
                    lo = mid;
                else
                    hi = mid;
            }
            return 0.5f * (lo + hi);
        };
        normalised = 0.5f * (edge (true) + edge (false));
    }
    else
    {
        const int steps = juce::jmax (2, p.getNumSteps() < 100000 ? p.getNumSteps() : 1001);
        bool found = false;
        for (int i = 0; i < steps && ! found; ++i)
        {
            const float v = static_cast<float> (i) / static_cast<float> (steps - 1);
            if (p.getText (v, 256).trim().equalsIgnoreCase (value.trim()))
            {
                normalised = v;
                found = true;
            }
        }
        if (! found)
            return false;
    }

    p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::printf ("usage: %s <identifier> --list | [--program N] [--set Name=Value]... (--in f.wav | --impulse) --out o.wav\n",
                     argv[0]);
        return 2;
    }

    const juce::String wanted (argv[1]);
    double sampleRate = 48000.0;
    int block = 512;
    double tailSeconds = 12.0;
    bool list = false, impulse = false;
    juce::String inPath, outPath, program;
    juce::StringArray sets;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };

        if (arg == "--list")          list = true;
        else if (arg == "--impulse")  impulse = true;
        else if (arg == "--in")       inPath = next();
        else if (arg == "--out")      outPath = next();
        else if (arg == "--set")      sets.add (next());
        else if (arg == "--program")  program = next();
        else if (arg == "--tail")     tailSeconds = next().getDoubleValue();
        else if (arg == "--sr")       sampleRate = next().getDoubleValue();
        else if (arg == "--block")    block = next().getIntValue();
    }

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::AudioUnitPluginFormat());

    juce::AudioUnitPluginFormat au;
    juce::OwnedArray<juce::PluginDescription> found;
    au.findAllTypesForFile (found, wanted);

    if (found.isEmpty())
    {
        std::printf ("no Audio Unit at identifier \"%s\"\n", wanted.toRawUTF8());
        return 1;
    }

    juce::String error;
    auto plugin = formats.createPluginInstance (*found.getFirst(), sampleRate, block, error);
    if (plugin == nullptr)
    {
        std::printf ("could not instantiate: %s\n", error.toRawUTF8());
        return 1;
    }

    plugin->setPlayConfigDetails (2, 2, sampleRate, block);
    plugin->prepareToPlay (sampleRate, block);

    const auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (5); };

    if (program.isNotEmpty())
    {
        int index = program.containsOnly ("0123456789") ? program.getIntValue() : -1;
        for (int i = 0; i < plugin->getNumPrograms() && index < 0; ++i)
            if (plugin->getProgramName (i).equalsIgnoreCase (program))
                index = i;
        if (index < 0 || index >= plugin->getNumPrograms())
        {
            std::printf ("no program \"%s\"\n", program.toRawUTF8());
            return 1;
        }
        plugin->setCurrentProgram (index);
        pump();
    }

    // Twice: some plugins move one parameter when another is set (Raum resets
    // Mix when Decay changes), so a single pass leaves the result depending on
    // the order the --set flags were given in.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto& s : sets)
        {
            const auto name = s.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto value = s.fromFirstOccurrenceOf ("=", false, false).trim();
            auto* p = findParameter (*plugin, name);
            if (p == nullptr || ! setParameter (*p, value))
            {
                std::printf ("could not set \"%s\"\n", s.toRawUTF8());
                return 1;
            }
        }
        pump();
    }

    if (list)
    {
        std::printf ("%d programs (current %d)\n", plugin->getNumPrograms(), plugin->getCurrentProgram());
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
            std::printf ("  [%d] %s\n", i, plugin->getProgramName (i).toRawUTF8());
    }

    std::printf ("%d parameters, latency %d, tail %.2f s\n", plugin->getParameters().size(),
                 plugin->getLatencySamples(), plugin->getTailLengthSeconds());
    for (auto* p : plugin->getParameters())
        std::printf ("  %3d  %-28s %.4f  %s\n", p->getParameterIndex(), p->getName (28).toRawUTF8(), p->getValue(),
                     p->getCurrentValueAsText().toRawUTF8());

    if (list || outPath.isEmpty())
        return 0;

    juce::AudioBuffer<float> buffer;
    int inputSamples = 0;

    if (impulse)
    {
        inputSamples = 1;
        const int total = static_cast<int> (sampleRate * tailSeconds);
        buffer.setSize (2, total);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);
    }
    else
    {
        juce::AudioFormatManager audioFormats;
        audioFormats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (
            audioFormats.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (inPath)));
        if (reader == nullptr)
        {
            std::printf ("could not read %s\n", inPath.toRawUTF8());
            return 1;
        }
        inputSamples = static_cast<int> (reader->lengthInSamples);
        const int total = inputSamples + static_cast<int> (sampleRate * tailSeconds);
        buffer.setSize (2, total);
        buffer.clear();
        reader->read (&buffer, 0, inputSamples, 0, true, true);
    }

    // A few blocks of silence first, so parameter smoothing inside the plugin has
    // settled before the signal arrives. They are not written out.
    const int preroll = block * 32;
    {
        juce::AudioBuffer<float> silence (2, block);
        juce::MidiBuffer midi;
        for (int i = 0; i < preroll; i += block)
        {
            silence.clear();
            plugin->processBlock (silence, midi);
            pump();
        }
    }

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> work (2, block);
    for (int pos = 0; pos < buffer.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, buffer.getNumSamples() - pos);
        work.clear();
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, buffer, ch, pos, n);
        plugin->processBlock (work, midi);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, pos, work, ch, 0, n);
    }

    // Report latency so the caller can line the render up with its input.
    const int latency = plugin->getLatencySamples();
    plugin->releaseResources();

    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (outPath));
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
    {
        std::printf ("could not write %s\n", outPath.toRawUTF8());
        return 1;
    }
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();

    std::printf ("rendered %d samples (+%d input) -> %s, latency %d\n", buffer.getNumSamples(), inputSamples,
                 outPath.toRawUTF8(), latency);
    return 0;
}
