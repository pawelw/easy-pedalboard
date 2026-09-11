// Renders a fixed battery of settings through the whole Peak Sympathy processor
// and prints an FNV-1a checksum per pass. New code, so nothing to diff against
// yet - this is the A/B for the next change that means to leave the pedal
// alone. See RegressHarness.h for why *_regress and *_match are two families.
//
// Covers every tuning mode, Coupling at both ends, Spread and Bloom at the top,
// Freeze in and out, Key-Learn, and the bypass crossfade. A resonator pedal
// wants a tail-heavy signal: the input stops early and the render carries on
// into silence, where a loop-gain change is obvious.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;

constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 7;
constexpr int kLength = static_cast<int> (kSampleRate) * kSeconds;
constexpr int kInputSeconds = 2;

struct Pass
{
    const char* name;
    int   mode;      // 0..5
    int   octave;    // -2..2
    float decay;     // %
    float damping;   // %
    float coupling;  // %
    float spread;    // %
    float bloom;     // %
    bool  freeze;    // static Freeze state (when freezeAtSec < 0)
    float freezeAtSec;    // >= 0: start Live, engage Freeze here (energy already in the bank)
    float freezeOffAtSec; // >= 0: release Freeze here, so the tail then decays
    bool  learn;
    bool  engaged;
};

const Pass kPasses[] = {
    { "octaves", 0, 0, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "fifths", 1, 0, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "harmonic", 2, -1, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "major JI", 3, 0, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "minor JI", 4, 0, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "chroma", 5, 0, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "coupling 0", 3, 0, 55.0f, 55.0f, 0.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "coupling 100", 3, 0, 55.0f, 55.0f, 100.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "spread 100", 3, 0, 55.0f, 55.0f, 25.0f, 100.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "bloom 100", 3, 0, 55.0f, 30.0f, 25.0f, 25.0f, 100.0f, false, -1.0f, -1.0f, false, true },
    { "freeze in", 3, 0, 60.0f, 60.0f, 30.0f, 30.0f, 0.0f, false, 1.5f, -1.0f, false, true },
    { "freeze in then out", 3, 0, 60.0f, 60.0f, 30.0f, 30.0f, 0.0f, false, 1.5f, 4.0f, false, true },
    { "freeze from cold", 3, 0, 60.0f, 60.0f, 30.0f, 30.0f, 0.0f, true, -1.0f, -1.0f, false, true },
    { "key learn", 3, 0, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, true, true },
    { "octave -2 / +2 mix", 0, 2, 50.0f, 70.0f, 40.0f, 20.0f, 0.0f, false, -1.0f, -1.0f, false, true },
    { "bypassed", 3, 0, 45.0f, 55.0f, 25.0f, 25.0f, 0.0f, false, -1.0f, -1.0f, false, false },
};

bool runPass (const Pass& pass, const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    PeakSympathyProcessor processor;

    setChoice (processor.apvts, "tune.mode", pass.mode);
    setChoice (processor.apvts, "tune.key", 9); // A
    setChoice (processor.apvts, "octave", pass.octave);
    setPercent (processor.apvts, "decay", pass.decay);
    setPercent (processor.apvts, "damping", pass.damping);
    setPercent (processor.apvts, "coupling", pass.coupling);
    setPercent (processor.apvts, "spread", pass.spread);
    setPercent (processor.apvts, "bloom", pass.bloom);
    setPercent (processor.apvts, "sensitivity", 55.0f);
    setPercent (processor.apvts, "mix", 60.0f);
    setFlag (processor.apvts, "freeze", pass.freeze && pass.freezeAtSec < 0.0f);
    setFlag (processor.apvts, "learn", pass.learn);
    setFlag (processor.apvts, "on", pass.engaged);

    processor.setPlayConfigDetails (2, 2, kSampleRate, 1024);
    processor.prepareToPlay (kSampleRate, 1024);

    output.makeCopyOf (input);

    const int freezeOn = pass.freezeAtSec >= 0.0f ? static_cast<int> (pass.freezeAtSec * kSampleRate) : -1;
    const int freezeOff = pass.freezeOffAtSec >= 0.0f ? static_cast<int> (pass.freezeOffAtSec * kSampleRate) : -1;

    juce::MidiBuffer midi;
    int blockIndex = 0;

    for (int offset = 0; offset < kLength;)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), kLength - offset);

        if (freezeOn >= 0 && offset <= freezeOn && offset + chunk > freezeOn)
            setFlag (processor.apvts, "freeze", true);
        if (freezeOff >= 0 && offset <= freezeOff && offset + chunk > freezeOff)
            setFlag (processor.apvts, "freeze", false);

        juce::AudioBuffer<float> slice (output.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);
        offset += chunk;
    }

    return allFinite (output);
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    juce::AudioBuffer<float> input (2, kLength);
    input.clear();

    juce::AudioBuffer<float> excitation (2, static_cast<int> (kSampleRate) * kInputSeconds);
    fillTestSignal (excitation, kSampleRate);
    for (int ch = 0; ch < 2; ++ch)
        input.copyFrom (ch, 0, excitation, ch, 0, excitation.getNumSamples());

    juce::AudioBuffer<float> output (2, kLength);
    juce::WavAudioFormat wav;
    bool ok = true;

    std::printf ("Peak Sympathy - %d passes at %.0f Hz, %d s each (%d s input, then tail)\n\n",
                 static_cast<int> (sizeof (kPasses) / sizeof (kPasses[0])), kSampleRate, kSeconds, kInputSeconds);

    for (const auto& pass : kPasses)
    {
        if (! runPass (pass, input, output))
        {
            std::printf ("%-22s FAILED (non-finite)\n", pass.name);
            ok = false;
            continue;
        }

        const int tailStart = static_cast<int> (kSampleRate) * (kInputSeconds + 2);
        std::printf ("%-22s %s  peak %.6f  rms %.6f  tail %.6f\n", pass.name, checksum (output).toRawUTF8(),
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

    std::printf ("\n%s\n", ok ? "all passes finite" : "FAILURES ABOVE");
    return ok ? 0 : 1;
}
