// Drives the real BitBitGrainProcessor the way a host does - prepareToPlay, then
// processBlock over and over - and reports the output level second by second.
//
// The engine-level stress app (ee_grain_stress) drives the DSP directly and so
// cannot see anything wrong with the wiring around it. This one can.
//
//   ee_grain_host [--sr 44100] [--block 128] [--in noise|dc|burst|silence]
//                 [--level -20] [--seconds 30] [--ragged] [--mono]
//                 [--bpm 120] [--onsets]
//                 [--size 0.5] [--density 0.5] [--ssync 0] [--dsync 0]
//                 [--time 300] [--feedback 30] [--stretch 0] [--freeze 0]
//                 [--shape 55] [--scatter 25] [--reverse 25] [--stereo 85]
//                 [--mod 0] [--bit 0]
//                 [--scale 0] [--root 0] [--low 0] [--unison 100] [--high 0] [--pmix 100]
//                 [--dtime 0.36] [--dtsync 1] [--dfb 30] [--dmix 30]
//                 [--decay 2.5] [--rmix 30]
//                 [--dry 100] [--grains 71] [--filter 100] [--drive 35]
//                 [--grainon 1] [--pitchon 1] [--randon 1] [--delon 1] [--revon 1]
//                 [--modroute]
//                 [--tuning <GrainerTuning field> <value>]
//
// --tuning reaches the voicing that is not on the face (GrainerTuning.h), by
// the same field names the -DEE_GRAIN_TUNER panel shows - e.g.
// `--tuning bandSplit 1`. Repeatable, like --param.
//
// --modroute assigns the Mod LFO to eight of its modulation targets at once
// (size/density/shape/scatter/stereo/filter/drive/bit, mixed depth signs -
// ModRouter::kMaxAssignments' own cap) and, unless --param
// lforate was also given, pushes the Rate knob to its fastest free-running
// period so a short render still covers several LFO cycles. For hunting
// non-finite/runaway output or clicks at the modulation-chunk boundaries
// PluginProcessor.cpp's own processBlock introduces when anything is
// assigned - see its kModChunk.
//
// Size, Density and the delay Time (--size/--density/--dtime) are normalised
// 0..1 knobs now - their Sync switch decides what that maps to.
//
// No --bpm means no playhead at all, so a Sync switch never actually engages
// (processBlock never sees a finite ppq) - pass one to test the synced path.
// --onsets measures grain spawn instants directly from the rendered audio (a
// Schmitt-triggered envelope follower), rather than trusting the label under
// the knob - use with --in dc, --dry 0 --grains 100 and a short unscattered Size so each
// grain is a clean, separated blip.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdint>
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
    silence,
    file
};

Input inputFromName (const juce::String& name)
{
    if (name == "dc")
        return Input::dc;
    if (name == "burst")
        return Input::burst;
    if (name == "silence")
        return Input::silence;
    if (name == "file")
        return Input::file;
    return Input::noise;
}

/** A transport that plays at a fixed tempo from bar 1, never relocated - see
    tests/RegressHarness.h's own FakePlayHead for the fuller version with a
    jump(). Without a playhead BitBitGrainProcessor::processBlock never sees a
    finite ppq, so densitySynced/windowSynced/sizeSynced/delaySynced never
    actually engage - this is the only thing standing between ee_grain_host
    and exercising Sync at all. */
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    FakePlayHead (double bpm, double sr) : tempo (bpm), sampleRate (sr) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (tempo);
        info.setIsPlaying (true);
        info.setPpqPosition (ppq);
        return info;
    }

    void advance (int numSamples) { ppq += (tempo / 60.0) * (static_cast<double> (numSamples) / sampleRate); }

private:
    double tempo;
    double sampleRate;
    double ppq = 0.0;
};
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
    juce::File stateFile;
    juce::File inFile;
    juce::File outFile;
    double bpm = 0.0; // 0 means no playhead at all - the old behaviour
    bool onsets = false;
    bool modRoute = false;

    // Knob overrides, applied through the parameter tree the way a host would.
    // Empty means "leave at the default".
    std::vector<std::pair<juce::String, float>> knobs;

    // GrainerTuning fields by name - the voicing that is not on the face and
    // so has no parameter id to reach it through. Same idea as --param, for
    // the other half of the engine.
    std::vector<std::pair<juce::String, float>> tuningFields;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };

        if (arg == "--sr")
            sampleRate = next().getDoubleValue();
        else if (arg == "--block")
            block = next().getIntValue();
        else if (arg == "--level")
            inputDb = static_cast<float> (next().getDoubleValue());
        else if (arg == "--seconds")
            seconds = next().getDoubleValue();
        else if (arg == "--in")
            input = inputFromName (next());
        else if (arg == "--ragged")
            ragged = true;
        else if (arg == "--bpm")
            bpm = next().getDoubleValue();
        else if (arg == "--onsets")
            onsets = true;
        else if (arg == "--modroute")
            modRoute = true;
        else if (arg == "--mono")
            mono = true;
        else if (arg == "--editor")
            withEditor = true;
        else if (arg == "--reprepare")
            reprepare = true;
        else if (arg == "--sweep")
            sweep = true;
        else if (arg == "--size")
            knobs.emplace_back ("size", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--density")
            knobs.emplace_back ("density", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--ssync")
            knobs.emplace_back ("ssync", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dsync")
            knobs.emplace_back ("dsync", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--time")
            knobs.emplace_back ("time", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--feedback")
            knobs.emplace_back ("feedback", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--stretch")
            knobs.emplace_back ("stretch", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--freeze")
            knobs.emplace_back ("freeze", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--width")
            knobs.emplace_back ("width", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--shape")
            knobs.emplace_back ("shape", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--scatter")
            knobs.emplace_back ("scatter", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--reverse")
            knobs.emplace_back ("reverse", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--stereo")
            knobs.emplace_back ("stereo", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--mod")
            knobs.emplace_back ("mod", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--bit")
            knobs.emplace_back ("bit", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--scale")
            knobs.emplace_back ("scale", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--root")
            knobs.emplace_back ("root", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--low")
            knobs.emplace_back ("plow", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--unison")
            knobs.emplace_back ("puni", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--high")
            knobs.emplace_back ("phigh", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--pmix")
            knobs.emplace_back ("pmix", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dtime")
            knobs.emplace_back ("dtime", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dtsync")
            knobs.emplace_back ("dtsync", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dfb")
            knobs.emplace_back ("dfb", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dmix")
            knobs.emplace_back ("dmix", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--decay")
            knobs.emplace_back ("decay", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--rmix")
            knobs.emplace_back ("rmix", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--dry")
            knobs.emplace_back ("dry", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--grains")
            knobs.emplace_back ("grains", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--filter")
            knobs.emplace_back ("filter", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--drive")
            knobs.emplace_back ("drive", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--grainon")
            knobs.emplace_back ("grainon", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--pitchon")
            knobs.emplace_back ("pitchon", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--randon")
            knobs.emplace_back ("randon", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--delon")
            knobs.emplace_back ("delon", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--revon")
            knobs.emplace_back ("revon", static_cast<float> (next().getDoubleValue()));
        else if (arg == "--param")
        {
            // Any parameter by id, for the ones without a flag of their own.
            const auto id = next();
            knobs.emplace_back (id, static_cast<float> (next().getDoubleValue()));
        }
        else if (arg == "--tuning")
        {
            const auto name = next();
            tuningFields.emplace_back (name, static_cast<float> (next().getDoubleValue()));
        }
        else if (arg == "--in-file")
        {
            // A recording instead of a generated signal - summed to mono and
            // played once from the start, then silence, so --seconds past its
            // end renders the tail. For A/B against a reference.
            inFile = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            input = Input::file;
        }
        else if (arg == "--out")
        {
            outFile = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        }
        else if (arg == "--save-state")
        {
            // Writes the state after the knobs are applied, in exactly the form
            // ee::plugin::PresetStore saves - for authoring a preset file.
            stateFile = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        }
        else if (arg == "--snapshot")
        {
            // Renders the editor - side panel included, when the tuner build
            // flag is on - so the layout can be checked without a host.
            snapshot = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            withEditor = true;
        }
    }

    const float amplitude = juce::Decibels::decibelsToGain (inputDb);

    std::vector<float> fileSamples;
    if (input == Input::file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
        if (reader == nullptr)
        {
            std::printf ("could not read %s\n", inFile.getFullPathName().toRawUTF8());
            return 1;
        }

        const int length = static_cast<int> (reader->lengthInSamples);
        const int fileChannels = static_cast<int> (reader->numChannels);
        juce::AudioBuffer<float> loaded (fileChannels, length);
        reader->read (&loaded, 0, length, 0, true, true);

        fileSamples.assign (static_cast<size_t> (length), 0.0f);
        for (int ch = 0; ch < fileChannels; ++ch)
            for (int i = 0; i < length; ++i)
                fileSamples[static_cast<size_t> (i)] += loaded.getSample (ch, i) / static_cast<float> (fileChannels);

        if (std::abs (reader->sampleRate - sampleRate) > 0.5)
            std::printf ("  note: %s is %.0f Hz, rendering at %.0f Hz without resampling\n",
                         inFile.getFileName().toRawUTF8(), reader->sampleRate, sampleRate);
    }

    std::vector<float> renderedL, renderedR;
    const int channels = mono ? 1 : 2;

    BitBitGrainProcessor processor;

    for (const auto& [id, value] : knobs)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        else
            std::printf ("  unknown parameter \"%s\"\n", id.toRawUTF8());
    }

    if (! tuningFields.empty())
    {
        auto voicing = processor.tuning();

        for (const auto& [name, value] : tuningFields)
        {
            bool found = false;
            for (const auto& entry : ee::dsp::kGrainerTuningEntries)
                if (name == entry.name)
                {
                    voicing.*(entry.member) = juce::jlimit (entry.minimum, entry.maximum, value);
                    std::printf ("  tuning %s = %g\n", entry.name, voicing.*(entry.member));
                    found = true;
                    break;
                }

            if (! found)
                std::printf ("  unknown tuning field \"%s\"\n", name.toRawUTF8());
        }

        processor.setTuning (voicing);
    }

    if (modRoute)
    {
        bool rateWasOverridden = false;
        for (const auto& [id, value] : knobs)
            if (id == "lforate")
                rateWasOverridden = true;

        // Fastest free-running period (30 ms - see kLfoRateMap in
        // PluginProcessor.cpp) unless the caller already asked for a specific
        // Rate, so a short render still covers several LFO cycles.
        if (! rateWasOverridden)
            if (auto* rate = processor.apvts.getParameter ("lforate"))
                rate->setValueNotifyingHost (1.0f);

        // A representative spread across every modulation target this pedal
        // has - Grain/Pitch/Random plus the Mixer's Filter/Drive/Bit - mixed
        // depth signs, right up to ModRouter::kMaxAssignments, exercising
        // "several targets at once" and the 8-slot cap together.
        processor.setLfoRoutingFromJson (R"([
            {"paramId":"size","depth":0.6},
            {"paramId":"density","depth":-0.5},
            {"paramId":"shape","depth":-0.7},
            {"paramId":"scatter","depth":0.8},
            {"paramId":"stereo","depth":0.5},
            {"paramId":"filter","depth":-0.6},
            {"paramId":"drive","depth":0.4},
            {"paramId":"bit","depth":0.3}
        ])");
        std::printf ("  modulation routing: LFO -> size/density/shape/scatter/stereo/filter/drive/bit\n");
    }

    if (stateFile != juce::File())
    {
        const auto xml = processor.apvts.copyState().createXml();
        const bool ok = xml != nullptr && stateFile.getParentDirectory().createDirectory() && xml->writeTo (stateFile);
        std::printf ("%s %s\n", ok ? "wrote" : "FAILED to write", stateFile.getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }

    processor.setPlayConfigDetails (channels, channels, sampleRate, block);
    processor.prepareToPlay (sampleRate, block);

    std::unique_ptr<FakePlayHead> playHead;
    if (bpm > 0.0)
    {
        playHead = std::make_unique<FakePlayHead> (bpm, sampleRate);
        processor.setPlayHead (playHead.get());
        std::printf ("  playhead: %.1f bpm, always playing from bar 1\n", bpm);
    }

    for (auto* parameter : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            std::printf ("  %-10s %s\n", withId->paramID.toRawUTF8(), parameter->getCurrentValueAsText().toRawUTF8());

    std::printf ("BitBit Grain defaults: %.0f Hz, block %d%s, %d ch, %g dBFS %s, %.0f s\n\n", sampleRate, block,
                 ragged ? " (ragged)" : "", channels, inputDb,
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

    // FNV-1a over the raw bits of every output sample, printed at the end, so
    // a change meant to alter nothing can be checked sample-exact against a
    // run from before it (see tests/RegressHarness.h for the same idea).
    std::uint64_t checksum = 1469598103934665603ull;

    // --onsets: a Schmitt-triggered envelope follower on the wet output, so a
    // grain's own spawn instant can be measured directly from rendered audio
    // rather than trusted from the knob label - see the note on kSpawnJumpPpq
    // and the report this is chasing (grains landing off the Density grid).
    float onsetEnv = 0.0f;
    bool onsetAbove = false;
    long long lastOnsetSample = -1000000000;
    std::vector<long long> onsetTimes;

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
            case Input::noise:
                s = noise (rng);
                break;
            case Input::dc:
                s = amplitude;
                break;
            case Input::silence:
                s = 0.0f;
                break;
            case Input::file:
                s = n < static_cast<long long> (fileSamples.size()) ? fileSamples[static_cast<size_t> (n)] : 0.0f;
                break;
            case Input::burst:
            {
                // A plucked note: a decaying 220 Hz tone every two seconds,
                // silence in between. Closer to a guitar than steady noise,
                // and it is the silences that a granular buffer can misread.
                const long long into = n % (2 * samplesPerSecond);
                const double t = static_cast<double> (into) / sampleRate;
                s = t < 0.8
                        ? amplitude * static_cast<float> (std::exp (-3.0 * t) *
                                                          std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t))
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
            const auto ramp = [t] (double period)
            { return 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * t / period); };

            const std::pair<const char*, double> moving[] = {
                { "size", 3.1 },    { "density", 4.7 }, { "time", 5.3 },    { "feedback", 9.7 }, { "stretch", 2.7 },
                { "freeze", 13.1 }, { "width", 10.3 }, { "shape", 3.7 },   { "scatter", 4.3 }, { "reverse", 2.3 },  { "stereo", 3.7 },
                { "scale", 4.1 },   { "root", 5.1 },    { "plow", 2.9 },    { "puni", 6.1 },     { "phigh", 3.3 },
                { "dtime", 5.9 },   { "dfb", 8.7 },     { "dmix", 6.7 },    { "decay", 7.1 },    { "rmix", 4.9 },
                { "dry", 8.3 },     { "grains", 7.7 },  { "filter", 5.7 },   { "drive", 4.5 },
                { "on", 11.3 } // the host's device on/off, which leaves the tail ringing
            };

            for (const auto& [id, period] : moving)
                if (auto* parameter = processor.apvts.getParameter (id))
                    parameter->setValueNotifyingHost (static_cast<float> (ramp (period)));
        }

        processor.processBlock (buffer, midi);

        if (outFile != juce::File())
        {
            const auto* pl = buffer.getReadPointer (0);
            const auto* pr = buffer.getReadPointer (channels > 1 ? 1 : 0);
            renderedL.insert (renderedL.end(), pl, pl + thisBlock);
            renderedR.insert (renderedR.end(), pr, pr + thisBlock);
        }

        if (playHead != nullptr)
            playHead->advance (thisBlock);

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
                    std::printf ("  *** non-finite output at %.2f s\n", static_cast<double> (n) / sampleRate);
                    reportedNonFinite = true;
                }

                std::uint32_t bits = 0;
                std::memcpy (&bits, &p[i], sizeof (bits));
                for (int byte = 0; byte < 4; ++byte)
                {
                    checksum ^= (bits >> (8 * byte)) & 0xffu;
                    checksum *= 1099511628211ull;
                }

                secondPeak = juce::jmax (secondPeak, std::abs (p[i]));
                secondSquares += static_cast<double> (p[i]) * p[i];
                ++secondCount;
            }
        }

        if (onsets)
        {
            const auto* pl = buffer.getReadPointer (0);
            const auto* pr = channels > 1 ? buffer.getReadPointer (1) : nullptr;
            const float releaseCoeff = std::exp (-1.0f / (0.003f * static_cast<float> (sampleRate)));
            const float onThreshold = 0.35f * amplitude;
            const float offThreshold = 0.15f * amplitude;
            const long long refractorySamples = static_cast<long long> (0.01 * sampleRate);

            for (int i = 0; i < thisBlock; ++i)
            {
                const float rectified = std::abs (pr != nullptr ? 0.5f * (pl[i] + pr[i]) : pl[i]);
                onsetEnv = std::max (rectified, onsetEnv * releaseCoeff);

                const long long globalSample = n - thisBlock + i;

                if (! onsetAbove && onsetEnv > onThreshold && (globalSample - lastOnsetSample) > refractorySamples)
                {
                    onsetAbove = true;
                    lastOnsetSample = globalSample;
                    if (onsetTimes.size() < 200)
                        onsetTimes.push_back (globalSample);
                }
                else if (onsetAbove && onsetEnv < offThreshold)
                {
                    onsetAbove = false;
                }
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

    std::printf ("\nchecksum %016llx\n", static_cast<unsigned long long> (checksum));

    if (outFile != juce::File())
    {
        outFile.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            stream != nullptr ? wav.createWriterFor (stream.release(), sampleRate, 2, 24, {}, 0) : nullptr);
        if (writer == nullptr)
        {
            std::printf ("could not write %s\n", outFile.getFullPathName().toRawUTF8());
            return 1;
        }
        const float* channelData[] = { renderedL.data(), renderedR.data() };
        writer->writeFromFloatArrays (channelData, 2, static_cast<int> (renderedL.size()));
        std::printf ("wrote %s\n", outFile.getFullPathName().toRawUTF8());
    }

    if (onsets)
    {
        std::printf ("\nonsets: %zu logged (capped at 200)\n", onsetTimes.size());
        for (size_t k = 1; k < onsetTimes.size(); ++k)
        {
            const double gapMs = static_cast<double> (onsetTimes[k] - onsetTimes[k - 1]) / sampleRate * 1000.0;
            std::printf ("  #%-3zu  t=%8.4f s   gap=%8.2f ms\n", k, static_cast<double> (onsetTimes[k]) / sampleRate,
                         gapMs);
        }
    }

    return 0;
}
