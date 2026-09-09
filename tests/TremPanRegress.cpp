// Renders a fixed battery of settings through the whole Peak Trem & Pan
// processor and prints a checksum per pass, so the pedal's output can be
// compared byte for byte across a change that is supposed to change nothing.
//
// Written for the extraction of ee::dsp::Tremolo out of this processor. See
// RegressHarness.h for what a *_regress tool is and how it differs from the
// *_match voicing renderers.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;

constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 3;
constexpr int kLength = static_cast<int> (kSampleRate) * kSeconds;

struct Pass
{
    const char* name;
    float amount; // %
    float rate01; // the Rate knob's own position
    float shape;  // %
    float bias;   // %
    bool panning;
    bool synced;
    bool engaged;
};

// Every branch the processor has: each of the five LFO shape anchors, the bias
// stage in and out, the panning law, the bypass crossfade, and a depth of zero.
const Pass kPasses[] = {
    { "trem shape 0 (exp decay)", 70.0f, 0.50f, 0.0f, 0.0f, false, false, true },
    { "trem shape 25 (ramp)", 70.0f, 0.50f, 25.0f, 0.0f, false, false, true },
    { "trem shape 50 (triangle)", 70.0f, 0.50f, 50.0f, 0.0f, false, false, true },
    { "trem shape 75", 70.0f, 0.50f, 75.0f, 0.0f, false, false, true },
    { "trem shape 100 (square)", 70.0f, 0.50f, 100.0f, 0.0f, false, false, true },
    { "trem bias 50", 40.0f, 0.35f, 50.0f, 50.0f, false, false, true },
    { "trem bias 100, full depth", 100.0f, 0.62f, 50.0f, 100.0f, false, false, true },
    { "panning", 80.0f, 0.45f, 50.0f, 0.0f, true, false, true },
    { "panning, bias ignored", 80.0f, 0.45f, 50.0f, 80.0f, true, false, true },
    { "synced, transport jump", 75.0f, 0.55f, 40.0f, 20.0f, false, true, true },
    { "synced panning", 75.0f, 0.30f, 60.0f, 0.0f, true, true, true },
    { "bypassed", 70.0f, 0.50f, 50.0f, 50.0f, false, false, false },
    { "amount 0", 0.0f, 0.50f, 50.0f, 50.0f, false, false, true },
};

bool runPass (const Pass& pass, const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    PeakTremPanProcessor processor;

    setPercent (processor.apvts, "amount", pass.amount);
    setNormalised (processor.apvts, "rate", pass.rate01);
    setPercent (processor.apvts, "shape", pass.shape);
    setPercent (processor.apvts, "bias", pass.bias);
    setFlag (processor.apvts, "mode", pass.panning);
    setFlag (processor.apvts, "sync", pass.synced);
    setFlag (processor.apvts, "on", pass.engaged);

    FakePlayHead playHead { 128.0, kSampleRate };
    processor.setPlayHead (&playHead);

    processor.setPlayConfigDetails (2, 2, kSampleRate, 1024);
    processor.prepareToPlay (kSampleRate, 1024);

    output.makeCopyOf (input);

    juce::MidiBuffer midi;
    const int jumpAt = kLength / 3;
    bool jumped = false;
    int blockIndex = 0;

    for (int offset = 0; offset < kLength;)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), kLength - offset);

        if (! jumped && offset >= jumpAt)
        {
            // Relocate: the processor should snap its phase rather than crawl
            // towards the new position over several seconds.
            playHead.jump (64.0);
            jumped = true;
        }

        juce::AudioBuffer<float> slice (output.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);

        playHead.advance (chunk);
        offset += chunk;
    }

    processor.setPlayHead (nullptr);
    return allFinite (output);
}
} // namespace

int main (int argc, char* argv[])
{
    // Optional: a folder to drop one wav per pass into, for listening to a
    // difference the checksums have already found.
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    juce::AudioBuffer<float> input (2, kLength);
    fillTestSignal (input, kSampleRate);

    juce::AudioBuffer<float> output (2, kLength);
    juce::WavAudioFormat wav;
    bool ok = true;

    std::printf ("Peak Trem & Pan - %d passes at %.0f Hz, %d s each, ragged blocks\n\n",
                 static_cast<int> (sizeof (kPasses) / sizeof (kPasses[0])), kSampleRate, kSeconds);

    for (const auto& pass : kPasses)
    {
        if (! runPass (pass, input, output))
        {
            std::printf ("%-30s FAILED\n", pass.name);
            ok = false;
            continue;
        }

        std::printf ("%-30s %s  peak %.6f  rms %.6f\n", pass.name, checksum (output).toRawUTF8(),
                     output.getMagnitude (0, kLength), output.getRMSLevel (0, 0, kLength));

        if (outDir != juce::File())
        {
            outDir.createDirectory();
            const auto file = outDir.getChildFile (juce::String (pass.name).replaceCharacter (' ', '-') + ".wav");
            file.deleteFile();

            if (auto stream = file.createOutputStream())
                if (std::unique_ptr<juce::AudioFormatWriter> writer {
                        wav.createWriterFor (stream.release(), kSampleRate, 2, 24, {}, 0) })
                    writer->writeFromAudioSampleBuffer (output, 0, kLength);
        }
    }

    std::printf ("\n%s\n", ok ? "all passes finite" : "FAILURES ABOVE");
    return ok ? 0 : 1;
}
