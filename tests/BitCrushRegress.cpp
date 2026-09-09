// Renders a fixed battery of settings through the whole Peak Artifact processor
// with the Bit Crush engine selected, and drives ee::dsp::BitCrusher directly,
// printing a checksum per pass.
//
// Written for voicing the Bit Crush engine (it was a pass-through placeholder).
// This is new code, so there is nothing to diff against yet - it is the
// baseline for the next change that is meant to leave the crusher alone. See
// RegressHarness.h for the family.
//
// The engine section also asserts the thing a checksum battery cannot show on
// its own: that Jitter, though it is driven by a random generator, renders
// identically on a second run - the generator is instance-owned and re-seeded
// in reset(), unlike the DaisySP shimmer seed (see CLAUDE.md).
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "ee/dsp/BitCrusher.h"
#include "ee/dsp/BitCrusherConfig.h"

#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;

constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 3;
constexpr int kLength = static_cast<int> (kSampleRate) * kSeconds;

// -------------------------------------------------------- through the processor

struct Pass
{
    const char* name;
    float bits;   // %
    float rate;   // %
    float lp;     // %
    float jitter; // %
    float mix;    // %
    bool engaged;
};

const Pass kPasses[] = {
    { "engine off-settings, default mix", 0.0f, 0.0f, 100.0f, 0.0f, 50.0f, true },
    { "defaults", ee::dsp::bitcrush::kDefaultBitsPct, ee::dsp::bitcrush::kDefaultRatePct,
      ee::dsp::bitcrush::kDefaultLpPct, ee::dsp::bitcrush::kDefaultJitterPct, 50.0f, true },
    { "bits low, rate mid", 80.0f, 45.0f, 100.0f, 0.0f, 100.0f, true },
    { "rate hard down", 30.0f, 100.0f, 100.0f, 0.0f, 100.0f, true },
    { "filter halfway", 55.0f, 55.0f, 45.0f, 0.0f, 100.0f, true },
    { "jitter 60", 55.0f, 60.0f, 100.0f, 60.0f, 100.0f, true },
    { "everything hot", 100.0f, 90.0f, 20.0f, 80.0f, 100.0f, true },
    { "mix 0", 60.0f, 60.0f, 60.0f, 0.0f, 0.0f, true },
    { "bypassed", 60.0f, 60.0f, 60.0f, 40.0f, 100.0f, false },
};

bool runPass (const Pass& pass, const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    PeakArtifactProcessor processor;

    setChoice (processor.apvts, "engine", 1); // Ring Mod / Bit Crush / Filter
    setPercent (processor.apvts, "crush.bits", pass.bits);
    setPercent (processor.apvts, "crush.rate", pass.rate);
    setPercent (processor.apvts, "crush.lp", pass.lp);
    setPercent (processor.apvts, "crush.jitter", pass.jitter);
    setPercent (processor.apvts, "mix", pass.mix);
    setFlag (processor.apvts, "on", pass.engaged);

    processor.setPlayConfigDetails (2, 2, kSampleRate, 1024);
    processor.prepareToPlay (kSampleRate, 1024);

    output.makeCopyOf (input);

    juce::MidiBuffer midi;
    int blockIndex = 0;

    for (int offset = 0; offset < kLength;)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), kLength - offset);

        juce::AudioBuffer<float> slice (output.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);

        offset += chunk;
    }

    return allFinite (output);
}

// ------------------------------------------------------- through the engine only

struct Row
{
    const char* name;
    float bits;
    int decimation; // 1 means "no decimation"
    float lpHz;     // >= kLpBypassHz means "bypassed"
    float jitter01;
};

const Row kRows[] = {
    { "clean (fast path)", 16.0f, 1, 20000.0f, 0.0f },
    { "12 bit, no decimation", 12.0f, 1, 20000.0f, 0.0f },
    { "10 bit, N=4", 10.0f, 4, 20000.0f, 0.0f },
    { "8 bit, N=8, lp 4 kHz", 8.0f, 8, 4000.0f, 0.0f },
    { "4 bit, N=16, lp 800 Hz", 4.0f, 16, 800.0f, 0.0f },
    { "6 bit, N=32 (past the knob), lp 2 kHz", 6.0f, 32, 2000.0f, 0.0f },
    { "9 bit, N=6, jitter 0.5", 9.0f, 6, 20000.0f, 0.5f },
    { "8 bit, N=12, lp 3 kHz, jitter 0.5", 8.0f, 12, 3000.0f, 0.5f },
};

juce::String renderRow (const Row& row, const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& out)
{
    ee::dsp::BitCrusher crusher;
    crusher.prepare (kSampleRate);
    crusher.setBits (row.bits);
    crusher.setDecimation (row.decimation);
    crusher.setLowpassHz (row.lpHz);
    crusher.setJitter01 (row.jitter01);

    out.makeCopyOf (input);

    int blockIndex = 0;
    for (int offset = 0; offset < kLength;)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), kLength - offset);
        crusher.process (out.getWritePointer (0, offset), out.getWritePointer (1, offset), chunk);
        offset += chunk;
    }

    return checksum (out);
}

bool engineSweep (const juce::AudioBuffer<float>& input)
{
    std::printf ("\nengine sweep - ee::dsp::BitCrusher directly\n\n");

    juce::AudioBuffer<float> out (2, kLength);
    juce::AudioBuffer<float> again (2, kLength);
    bool ok = true;

    for (const auto& row : kRows)
    {
        const auto first = renderRow (row, input, out);

        if (! allFinite (out))
        {
            std::printf ("%-40s FAILED (non-finite)\n", row.name);
            ok = false;
            continue;
        }

        // Same row a second time: a fresh engine, reset() re-seeds the jitter
        // generator, so even the jitter rows must land on the same checksum.
        const auto second = renderRow (row, input, again);

        std::printf ("%-40s %s  peak %.6f%s\n", row.name, first.toRawUTF8(),
                     out.getMagnitude (0, kLength), first == second ? "" : "  <-- NOT REPRODUCIBLE");

        if (first != second)
            ok = false;
    }

    // The first row is the transparent fast path: it must be the input, bit for
    // bit.
    if (renderRow (kRows[0], input, out) == checksum (input))
        std::printf ("\n  ok    clean settings are a bit-exact pass-through\n");
    else
    {
        std::printf ("\n  FAIL  clean settings altered the signal\n");
        ok = false;
    }

    return ok;
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    juce::AudioBuffer<float> input (2, kLength);
    fillTestSignal (input, kSampleRate);

    juce::AudioBuffer<float> output (2, kLength);
    juce::WavAudioFormat wav;
    bool ok = true;

    std::printf ("Peak Artifact / Bit Crush - %d passes at %.0f Hz, %d s each\n\n",
                 static_cast<int> (sizeof (kPasses) / sizeof (kPasses[0])), kSampleRate, kSeconds);

    for (const auto& pass : kPasses)
    {
        if (! runPass (pass, input, output))
        {
            std::printf ("%-34s FAILED\n", pass.name);
            ok = false;
            continue;
        }

        std::printf ("%-34s %s  peak %.6f  rms %.6f\n", pass.name, checksum (output).toRawUTF8(),
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

    ok = engineSweep (input) && ok;

    std::printf ("\n%s\n", ok ? "all passes finite and reproducible" : "FAILURES ABOVE");
    return ok ? 0 : 1;
}
