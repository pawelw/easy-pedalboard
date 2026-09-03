// Drives the real PeakGrainProcessor the way a host does - prepareToPlay, then
// processBlock over and over - and reports the output level second by second.
//
// The engine-level stress app (ee_grain_stress) drives the DSP directly and so
// cannot see anything wrong with the wiring around it. This one can.
//
//   ee_grain_host [--sr 44100] [--block 128] [--in noise|dc|burst|silence]
//                 [--level -20] [--seconds 30] [--ragged] [--mono]
//                 [--size 0.5] [--density 0.5] [--ssync 0] [--dsync 0]
//                 [--time 300] [--feedback 30] [--stretch 0] [--freeze 0]
//                 [--shape 55] [--scatter 25] [--reverse 25] [--stereo 85]
//                 [--detune 6] [--low 0] [--unison 100] [--high 0]
//                 [--dtime 0.36] [--dtsync 1] [--dfb 30] [--dmix 30]
//                 [--decay 2.5] [--rmix 30] [--mix 50]
//
// Size, Density and the delay Time (--size/--density/--dtime) are normalised
// 0..1 knobs now - their Sync switch decides what that maps to.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "PluginProcessor.h"

namespace
{
enum class Input
{
    noise,
    dc,
    burst,
    silence
};

Input inputFromName (const juce::String& name)
{
    if (name == "dc")
        return Input::dc;
    if (name == "burst")
        return Input::burst;
    if (name == "silence")
        return Input::silence;
    return Input::noise;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    double sampleRate = 48000.0;
    int block = 512;
    float inputDb = -20.0f;
    double seconds = 30.0;
    Input input = Input::noise;
    bool ragged = false;
    bool mono = false;
    bool withEditor = false;
    bool reprepare = false;
    bool sweep = false;
    juce::File snapshot;

    // Knob overrides, applied through the parameter tree the way a host would.
    // Empty means "leave at the default".
    std::vector<std::pair<juce::String, float>> knobs;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };

        if (arg == "--sr")            sampleRate = next().getDoubleValue();
        else if (arg == "--block")    block = next().getIntValue();
        else if (arg == "--level")    inputDb = static_cast<float> (next().getDoubleValue());
        else if (arg == "--seconds")  seconds = next().getDoubleValue();
        else if (arg == "--in")       input = inputFromName (next());
        else if (arg == "--ragged")   ragged = true;
        else if (arg == "--mono")     mono = true;
        else if (arg == "--editor")   withEditor = true;
        else if (arg == "--reprepare") reprepare = true;
        else if (arg == "--sweep")    sweep = true;
        else if (arg == "--size")     knobs.emplace_back ("size", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--density")  knobs.emplace_back ("density", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--ssync")    knobs.emplace_back ("ssync", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dsync")    knobs.emplace_back ("dsync", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--time")     knobs.emplace_back ("time", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--feedback") knobs.emplace_back ("feedback", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--stretch")  knobs.emplace_back ("stretch", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--freeze")   knobs.emplace_back ("freeze", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--shape")    knobs.emplace_back ("shape", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--scatter")  knobs.emplace_back ("scatter", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--reverse")  knobs.emplace_back ("reverse", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--stereo")   knobs.emplace_back ("stereo", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--detune")   knobs.emplace_back ("detune", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--low")      knobs.emplace_back ("plow", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--unison")   knobs.emplace_back ("puni", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--high")     knobs.emplace_back ("phigh", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dtime")    knobs.emplace_back ("dtime", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dtsync")   knobs.emplace_back ("dtsync", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dfb")      knobs.emplace_back ("dfb", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dmix")     knobs.emplace_back ("dmix", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--decay")    knobs.emplace_back ("decay", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--rmix")     knobs.emplace_back ("rmix", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--mix")      knobs.emplace_back ("mix", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--snapshot")
        {
            // Renders the editor - side panel included, when the tuner build
            // flag is on - so the layout can be checked without a host.
            snapshot = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            withEditor = true;
        }
    }

    const float amplitude = juce::Decibels::decibelsToGain (inputDb);
    const int channels = mono ? 1 : 2;

    PeakGrainProcessor processor;

    for (const auto& [id, value] : knobs)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        else
            std::printf ("  unknown parameter \"%s\"\n", id.toRawUTF8());
    }

    processor.setPlayConfigDetails (channels, channels, sampleRate, block);
    processor.prepareToPlay (sampleRate, block);

    for (auto* parameter : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            std::printf ("  %-10s %s\n", withId->paramID.toRawUTF8(),
                         parameter->getCurrentValueAsText().toRawUTF8());

    std::printf ("Peak Grain defaults: %.0f Hz, block %d%s, %d ch, %g dBFS %s, %.0f s\n\n",
                 sampleRate, block, ragged ? " (ragged)" : "", channels, inputDb,
                 input == Input::dc        ? "DC"
                 : input == Input::burst   ? "bursts"
                 : input == Input::silence ? "silence"
                                           : "noise",
                 seconds);
    std::printf ("  %5s  %10s  %10s\n", "sec", "peak", "rms");

    // A host has the editor open most of the time, and its parameter
    // attachments write to the tree as they are constructed.
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    if (withEditor)
    {
        editor.reset (processor.createEditor());

        if (editor != nullptr && snapshot != juce::File())
        {
            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            {
                juce::Graphics g (image);
                editor->paintEntireComponent (g, true);
            }

            juce::PNGImageFormat png;
            snapshot.deleteFile();
            if (auto stream = snapshot.createOutputStream())
                png.writeImageToStream (image, *stream);

            std::printf ("wrote %s (%d x %d)\n", snapshot.getFullPathName().toRawUTF8(), editor->getWidth(),
                         editor->getHeight());
        }
    }

    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;

    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-amplitude, amplitude);
    std::uniform_int_distribution<int> blockSize (1, block);

    float secondPeak = 0.0f;
    double secondSquares = 0.0;
    long long secondCount = 0;
    long long sinceReport = 0;
    long long n = 0;
    int reportedSecond = 0;
    bool reportedNonFinite = false;

    const long long totalSamples = static_cast<long long> (seconds * sampleRate);
    const long long samplesPerSecond = static_cast<long long> (sampleRate);

    while (n < totalSamples)
    {
        const int thisBlock = ragged ? blockSize (rng) : block;
        buffer.setSize (channels, thisBlock, false, false, true);

        for (int i = 0; i < thisBlock; ++i)
        {
            float s = 0.0f;

            switch (input)
            {
                case Input::noise:   s = noise (rng); break;
                case Input::dc:      s = amplitude; break;
                case Input::silence: s = 0.0f; break;
                case Input::burst:
                {
                    // A plucked note: a decaying 220 Hz tone every two seconds,
                    // silence in between. Closer to a guitar than steady noise,
                    // and it is the silences that a granular buffer can misread.
                    const long long into = n % (2 * samplesPerSecond);
                    const double t = static_cast<double> (into) / sampleRate;
                    s = t < 0.8 ? amplitude * static_cast<float> (std::exp (-3.0 * t)
                                                                 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                             * 220.0 * t))
                                : 0.0f;
                    break;
                }
            }

            for (int ch = 0; ch < channels; ++ch)
                buffer.getWritePointer (ch)[i] = s;

            ++n;
        }

        // Every knob moving at once, at prime-ish rates so they never line up.
        // Static settings are the easy case; what a player actually does is
        // turn things while it is running.
        if (sweep)
        {
            const double t = static_cast<double> (n) / sampleRate;
            const auto ramp = [t] (double period) { return 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * t / period); };

            const std::pair<const char*, double> moving[] = {
                { "size", 3.1 },     { "density", 4.7 },  { "time", 5.3 },
                { "feedback", 9.7 }, { "stretch", 2.7 },  { "freeze", 13.1 },
                { "shape", 3.7 },    { "scatter", 4.3 },
                { "reverse", 2.3 },  { "stereo", 3.7 },   { "detune", 4.1 },
                { "plow", 2.9 },     { "puni", 6.1 },     { "phigh", 3.3 },
                { "dtime", 5.9 },    { "dfb", 8.7 },      { "dmix", 6.7 },
                { "decay", 7.1 },    { "rmix", 4.9 },     { "mix", 8.3 },
                { "on", 11.3 }   // the host's device on/off, which leaves the tail ringing
            };

            for (const auto& [id, period] : moving)
                if (auto* parameter = processor.apvts.getParameter (id))
                    parameter->setValueNotifyingHost (static_cast<float> (ramp (period)));
        }

        processor.processBlock (buffer, midi);

        // Hosts re-prepare on a buffer-size or sample-rate change, and on
        // transport starts. Anything the engine leaves behind shows up here.
        if (reprepare && (n / samplesPerSecond) != ((n - thisBlock) / samplesPerSecond))
        {
            processor.prepareToPlay (sampleRate, block);
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            for (int i = 0; i < thisBlock; ++i)
            {
                if (! std::isfinite (p[i]) && ! reportedNonFinite)
                {
                    std::printf ("  *** non-finite output at %.2f s\n",
                                 static_cast<double> (n) / sampleRate);
                    reportedNonFinite = true;
                }

                secondPeak = juce::jmax (secondPeak, std::abs (p[i]));
                secondSquares += static_cast<double> (p[i]) * p[i];
                ++secondCount;
            }
        }

        sinceReport += thisBlock;

        if (sinceReport >= samplesPerSecond)
        {
            const float rms = static_cast<float> (std::sqrt (secondSquares / static_cast<double> (secondCount)));
            std::printf ("  %5d  %10.4f  %10.4f%s\n", ++reportedSecond, secondPeak, rms,
                         secondPeak > 2.0f ? "   <-- LOUD" : "");

            secondPeak = 0.0f;
            secondSquares = 0.0;
            secondCount = 0;
            sinceReport = 0;
        }
    }

    return 0;
}
