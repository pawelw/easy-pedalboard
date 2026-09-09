// Renders a fixed battery of settings through the whole Peak Spring processor
// and prints a checksum per pass.
//
// Written for adding Tension and Low Cut to ee::dsp::SpringReverb. Those are
// additive - Peak Spring never sets either, so the tank must come back
// bit-identical with them in place. See RegressHarness.h for the family.
//
// A reverb needs a different signal from a delay or a tremolo: what matters is
// the tail, so the input stops early and the render carries on into silence.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "ee/dsp/SpringReverb.h"

#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;

constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 6;
constexpr int kLength = static_cast<int> (kSampleRate) * kSeconds;

// The input stops here and the rest is silence, so most of the render is tail.
// A change that alters the loop gain by a hair is invisible in the first
// hundred milliseconds and obvious four seconds in.
constexpr int kInputSeconds = 2;

struct Pass
{
    const char* name;
    float decay; // %
    float mix;   // %
    bool stereo;
    bool engaged;
};

const Pass kPasses[] = {
    { "decay 0 (shortest)", 0.0f, 100.0f, true, true },
    { "decay 25", 25.0f, 100.0f, true, true },
    { "decay 50, default mix", 50.0f, 35.0f, true, true },
    { "decay 75", 75.0f, 100.0f, true, true },
    { "decay 100 (longest)", 100.0f, 100.0f, true, true },
    { "mono tank", 50.0f, 100.0f, false, true },
    { "mix 0", 50.0f, 0.0f, true, true },
    { "bypassed", 50.0f, 100.0f, true, false },
};

bool runPass (const Pass& pass, const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    PeakSpringProcessor processor;

    setPercent (processor.apvts, "decay", pass.decay);
    setPercent (processor.apvts, "mix", pass.mix);
    setFlag (processor.apvts, "stereo", pass.stereo);
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
/** The two controls Peak Spring does not have, driven on the engine directly.
    Three things are being asked here, and the pedal battery above can answer
    none of them because that pedal never touches either control:

      1. that the defaults really are inert - an engine told
         setTension01(kDefaultTension01) and setLowCut(kOutputLowCutHz) must
         checksum identically to one told nothing at all, or "Peak Spring is
         untouched" is only true by accident of it not calling them;
      2. that they actually do something - every other row must differ;
      3. that nothing at either extreme rings away or goes non-finite.

    Engine-level rather than through a processor, because there is no processor
    that exposes them yet. */
bool engineSweep()
{
    constexpr int kSweepLength = static_cast<int> (kSampleRate) * 4;
    constexpr int kExciteLength = static_cast<int> (kSampleRate) / 2;

    juce::AudioBuffer<float> mono (1, kSweepLength);
    mono.clear();
    juce::AudioBuffer<float> excitation (1, kExciteLength);
    fillTestSignal (excitation, kSampleRate);
    mono.copyFrom (0, 0, excitation, 0, 0, kExciteLength);

    struct Row
    {
        const char* name;
        float decaySeconds;
        float tension01; // < 0 means "never set it"
        float lowCutHz;  // < 0 means "never set it"
    };

    const Row rows[] = {
        { "engine: untouched", 2.0f, -1.0f, -1.0f },
        { "engine: defaults set", 2.0f, ee::dsp::spring::kDefaultTension01, ee::dsp::spring::kOutputLowCutHz },
        { "engine: tension 0 (slack)", 2.0f, 0.0f, -1.0f },
        { "engine: tension 1 (taut)", 2.0f, 1.0f, -1.0f },
        { "engine: low cut min", 2.0f, -1.0f, ee::dsp::spring::kMinLowCutHz },
        { "engine: low cut max", 2.0f, -1.0f, ee::dsp::spring::kMaxLowCutHz },
        { "engine: both extreme, long decay", ee::dsp::SpringReverb::kMaxDecay, 1.0f,
          ee::dsp::spring::kMaxLowCutHz },
    };

    juce::AudioBuffer<float> out (2, kSweepLength);
    juce::String untouched, defaultsSet;
    bool ok = true;

    std::printf ("\nengine sweep - the two controls Peak Spring does not have\n\n");

    for (const auto& row : rows)
    {
        ee::dsp::SpringReverb tank;
        tank.prepare (kSampleRate);
        tank.setDecayTime (row.decaySeconds);

        // Set *before* prepare would also work; after it is the harder case,
        // because the coefficient has to reach chains that are already built.
        if (row.tension01 >= 0.0f)
            tank.setTension01 (row.tension01);
        if (row.lowCutHz >= 0.0f)
            tank.setLowCut (row.lowCutHz);

        out.clear();

        int blockIndex = 0;
        for (int offset = 0; offset < kSweepLength;)
        {
            const int chunk = juce::jmin (nextBlockSize (blockIndex++), kSweepLength - offset);
            tank.process (mono.getReadPointer (0, offset), out.getWritePointer (0, offset),
                          out.getWritePointer (1, offset), chunk);
            offset += chunk;
        }

        if (! allFinite (out))
        {
            std::printf ("%-34s FAILED\n", row.name);
            ok = false;
            continue;
        }

        const auto sum = checksum (out);
        const int tailStart = static_cast<int> (kSampleRate) * 3;

        std::printf ("%-34s %s  peak %.6f  tail %.6f\n", row.name, sum.toRawUTF8(),
                     out.getMagnitude (0, kSweepLength),
                     out.getRMSLevel (0, tailStart, kSweepLength - tailStart));

        if (juce::String (row.name) == "engine: untouched")
            untouched = sum;
        if (juce::String (row.name) == "engine: defaults set")
            defaultsSet = sum;
    }

    if (untouched.isNotEmpty() && untouched == defaultsSet)
        std::printf ("\n  ok    setting both controls to their defaults changes nothing\n");
    else
    {
        std::printf ("\n  FAIL  the defaults are not inert (%s vs %s)\n", untouched.toRawUTF8(),
                     defaultsSet.toRawUTF8());
        ok = false;
    }

    return ok;
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    juce::AudioBuffer<float> input (2, kLength);
    input.clear();

    // Signal for the first stretch only; silence after, so the tank is ringing
    // out on its own for two thirds of every pass.
    juce::AudioBuffer<float> excitation (2, static_cast<int> (kSampleRate) * kInputSeconds);
    fillTestSignal (excitation, kSampleRate);
    for (int ch = 0; ch < 2; ++ch)
        input.copyFrom (ch, 0, excitation, ch, 0, excitation.getNumSamples());

    juce::AudioBuffer<float> output (2, kLength);
    juce::WavAudioFormat wav;
    bool ok = true;

    std::printf ("Peak Spring - %d passes at %.0f Hz, %d s each (%d s of input, then tail)\n\n",
                 static_cast<int> (sizeof (kPasses) / sizeof (kPasses[0])), kSampleRate, kSeconds, kInputSeconds);

    for (const auto& pass : kPasses)
    {
        if (! runPass (pass, input, output))
        {
            std::printf ("%-24s FAILED\n", pass.name);
            ok = false;
            continue;
        }

        // The tail's own level, measured well after the input stopped - the
        // number a decay change moves first.
        const int tailStart = static_cast<int> (kSampleRate) * (kInputSeconds + 1);

        std::printf ("%-24s %s  peak %.6f  rms %.6f  tail %.6f\n", pass.name, checksum (output).toRawUTF8(),
                     output.getMagnitude (0, kLength), output.getRMSLevel (0, 0, kLength),
                     output.getRMSLevel (0, tailStart, kLength - tailStart));

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

    ok = engineSweep() && ok;

    std::printf ("\n%s\n", ok ? "all passes finite" : "FAILURES ABOVE");
    return ok ? 0 : 1;
}
