// Renders ee::dsp::SpaceReverb on its own - an impulse, or a file - with any
// SpaceVoicing field overridden from the command line, so the voicing can be
// fitted against a reference (see SpaceConfig.h) without a rebuild per guess.
//
//   ee_space_fit --decay 2 --damp 0.25 --out ir.wav [--in dry.wav] [--seconds 6]
//                [--predelay 0] [--lowcut 20] [--highcut 20000]
//                [field=value ...]
//
// Fields: any scalar in SpaceVoicing by name ("lateGain=0.3"), the arrays as
// comma lists ("lineMs=25,29,..."), dampLow / dampMid / dampHigh as 25 values
// row-major.
// Output is the wet only, 32-bit float.

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "ee/dsp/SpaceReverb.h"

namespace
{
bool setField (ee::dsp::SpaceVoicing& v, const juce::String& name, const juce::String& value)
{
    const auto list = juce::StringArray::fromTokens (value, ",", "");
    const auto fill = [&list] (float* dst, int n)
    {
        if (list.size() != n)
            return false;
        for (int i = 0; i < n; ++i)
            dst[i] = static_cast<float> (list[i].getDoubleValue());
        return true;
    };
    const float x = static_cast<float> (value.getDoubleValue());

    if (name == "earlySameMs")          { v.earlySameMs = x; return true; }
    if (name == "earlyLeftToRightMs")   { v.earlyLeftToRightMs = x; return true; }
    if (name == "earlyRightToLeftMs")   { v.earlyRightToLeftMs = x; return true; }
    if (name == "earlyGain")            { v.earlyGain = x; return true; }
    if (name == "earlyDampTripMs")      { v.earlyDampTripMs = x; return true; }
    if (name == "modDepthMs")           { v.modDepthMs = x; return true; }
    if (name == "lateGain")             { v.lateGain = x; return true; }
    if (name == "lateGainDecayExponent") { v.lateGainDecayExponent = x; return true; }
    if (name == "lateBandwidthHz")      { v.lateBandwidthHz = x; return true; }
    if (name == "decayScale")           return fill (v.decayScale.data(), ee::dsp::SpaceVoicing::kDecayPoints);
    if (name == "dampLowHz")            { v.dampLowHz = x; return true; }
    if (name == "dampHighHz")           { v.dampHighHz = x; return true; }
    if (name == "feedDiffusion")        { v.feedDiffusion = x; return true; }
    if (name == "feedDiffuserMidMs")    return fill (v.feedDiffuserMidMs.data(), ee::dsp::SpaceVoicing::kFeedDiffusers);
    if (name == "feedDiffuserSideMs")   return fill (v.feedDiffuserSideMs.data(), ee::dsp::SpaceVoicing::kFeedDiffusers);
    if (name == "lineMs")               return fill (v.lineMs.data(), ee::dsp::SpaceVoicing::kLines);
    if (name == "lfoHz")                return fill (v.lfoHz.data(), ee::dsp::SpaceVoicing::kLines);
    if (name == "dampLow" || name == "dampMid" || name == "dampHigh")
    {
        if (list.size() != 25)
            return false;
        auto& table = name == "dampLow" ? v.dampLow : name == "dampMid" ? v.dampMid : v.dampHigh;
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 5; ++c)
                table[static_cast<size_t> (r)][static_cast<size_t> (c)] =
                    static_cast<float> (list[r * 5 + c].getDoubleValue());
        return true;
    }
    return false;
}
} // namespace

int main (int argc, char* argv[])
{
    double sampleRate = 48000.0;
    float decay = 2.0f, damp = 0.25f, predelay = 0.0f, lowCut = 20.0f, highCut = 20000.0f;
    double seconds = 6.0;
    juce::String inPath, outPath;
    ee::dsp::SpaceVoicing voicing;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };

        if (arg == "--decay")          decay = static_cast<float> (next().getDoubleValue());
        else if (arg == "--damp")      damp = static_cast<float> (next().getDoubleValue());
        else if (arg == "--predelay")  predelay = static_cast<float> (next().getDoubleValue());
        else if (arg == "--lowcut")    lowCut = static_cast<float> (next().getDoubleValue());
        else if (arg == "--highcut")   highCut = static_cast<float> (next().getDoubleValue());
        else if (arg == "--seconds")   seconds = next().getDoubleValue();
        else if (arg == "--sr")        sampleRate = next().getDoubleValue();
        else if (arg == "--in")        inPath = next();
        else if (arg == "--out")       outPath = next();
        else if (arg.contains ("="))
        {
            if (! setField (voicing, arg.upToFirstOccurrenceOf ("=", false, false),
                            arg.fromFirstOccurrenceOf ("=", false, false)))
            {
                std::printf ("bad field: %s\n", arg.toRawUTF8());
                return 2;
            }
        }
    }

    if (outPath.isEmpty())
    {
        std::printf ("usage: %s --decay s --damp 0..1 --out ir.wav [--in dry.wav] [field=value ...]\n", argv[0]);
        return 2;
    }

    juce::AudioBuffer<float> buffer;
    if (inPath.isNotEmpty())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (
            formats.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (inPath)));
        if (reader == nullptr)
        {
            std::printf ("could not read %s\n", inPath.toRawUTF8());
            return 1;
        }
        const int n = static_cast<int> (reader->lengthInSamples);
        buffer.setSize (2, n + static_cast<int> (seconds * sampleRate));
        buffer.clear();
        reader->read (&buffer, 0, n, 0, true, true);
        sampleRate = reader->sampleRate;
    }
    else
    {
        buffer.setSize (2, static_cast<int> (seconds * sampleRate));
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);
    }

    ee::dsp::SpaceReverb reverb;
    reverb.prepare (sampleRate);
    reverb.setVoicing (voicing);
    reverb.setDecayTime (decay);
    reverb.setDamping (damp);
    reverb.setPredelay (predelay);
    reverb.setLowCut (lowCut);
    reverb.setHighCut (highCut);
    reverb.prepare (sampleRate); // snap every smoother to the settings above

    // Past the engine's after-reset input fade before the signal arrives, or an
    // impulse at sample 0 is scaled by the fade's first step.
    {
        juce::AudioBuffer<float> silence (2, static_cast<int> (0.02 * sampleRate));
        silence.clear();
        juce::AudioBuffer<float> sink (2, silence.getNumSamples());
        reverb.process (silence.getReadPointer (0), silence.getReadPointer (1), sink.getWritePointer (0),
                        sink.getWritePointer (1), silence.getNumSamples());
    }

    juce::AudioBuffer<float> out (2, buffer.getNumSamples());
    constexpr int block = 512;
    for (int pos = 0; pos < buffer.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, buffer.getNumSamples() - pos);
        reverb.process (buffer.getReadPointer (0, pos), buffer.getReadPointer (1, pos),
                        out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
    }

    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (outPath));
    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
        return 1;
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    return 0;
}
