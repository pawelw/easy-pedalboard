// Drives the real Peak Alpine processor the way a host does, and asserts the
// things that would make it broken rather than merely different: that it makes
// sound at all, that every module's power toggle reaches the audio, that a
// bypassed plugin is unity, and that nothing anywhere goes non-finite.
//
// It also prints a checksum per case, so this doubles as the baseline for the
// next refactor of it - the same trick the *_regress tools use. There is no
// baseline to diff against yet, because the processor is new.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "Params.h"
#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;

constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 4;
constexpr int kLength = static_cast<int> (kSampleRate) * kSeconds;

int failures = 0;

void check (bool condition, const char* what)
{
    std::printf ("  %s  %s\n", condition ? "ok  " : "FAIL", what);
    if (! condition)
        ++failures;
}

/** Renders the test signal through a fresh processor with `configure` applied,
    over ragged blocks and a running transport. */
template <typename Fn>
void render (juce::AudioBuffer<float>& out, Fn&& configure)
{
    PeakAlpineProcessor processor;
    configure (processor.apvts);

    FakePlayHead playHead { 120.0, kSampleRate };
    processor.setPlayHead (&playHead);

    processor.setPlayConfigDetails (2, 2, kSampleRate, 1024);
    processor.prepareToPlay (kSampleRate, 1024);

    juce::MidiBuffer midi;
    int blockIndex = 0;

    for (int offset = 0; offset < kLength;)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), kLength - offset);

        juce::AudioBuffer<float> slice (out.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);

        playHead.advance (chunk);
        offset += chunk;
    }

    processor.setPlayHead (nullptr);
}

float difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float worst = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));

    return worst;
}

struct Case
{
    const char* name;
    void (*configure) (juce::AudioProcessorValueTreeState&);
};

void defaults (juce::AudioProcessorValueTreeState&) {}

void bypassed (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::on, false); }
void modOff (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::modOn, false); }
void delayOff (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::dlyOn, false); }
void reverbOff (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::revOn, false); }

void everything (juce::AudioProcessorValueTreeState& s)
{
    using namespace ee::alpine::id;
    setPercent (s, modMix, 70.0f);
    setPercent (s, dlyMix, 55.0f);
    setPercent (s, dlyFeedback, 65.0f);
    setPercent (s, dlyWear, 60.0f);
    setPercent (s, dlyFlutter, 50.0f);
    setPercent (s, dlyDrift, 40.0f);
    setPercent (s, dlyPhaser, 40.0f);
    setPercent (s, revMix, 60.0f);
    setPercent (s, revSpaceShimmer, 40.0f);
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    juce::AudioBuffer<float> input (2, kLength);
    fillTestSignal (input, kSampleRate);

    std::printf ("=== Peak Alpine host ===\n\n");

    juce::AudioBuffer<float> plain (2, kLength);
    plain.makeCopyOf (input);
    render (plain, defaults);

    check (allFinite (plain), "the default patch is finite");
    check (plain.getMagnitude (0, kLength) > 0.01f, "...and makes sound");
    check (difference (plain, input) > 0.001f, "...and is not just the input back");

    // Each module's own power toggle has to reach the audio. A module wired to
    // the wrong parameter, or to none, would pass every other check here.
    for (const auto& c : { Case { "Modulation off", modOff }, Case { "Delay off", delayOff },
                           Case { "Reverb off", reverbOff } })
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out, c.configure);

        check (allFinite (out) && difference (out, plain) > 0.001f, c.name);
    }

    // Bypassed is the input, and must be so whatever the trims say - they sit
    // inside the crossfade. The first 100 ms is the engage ramp.
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out, bypassed);

        const int settled = static_cast<int> (kSampleRate * 0.1);
        float worst = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = settled; i < kLength; ++i)
                worst = juce::jmax (worst, std::abs (out.getSample (ch, i) - input.getSample (ch, i)));

        check (worst == 0.0f, "bypassed is the input, bit for bit");
    }

    // Every engine of both switchable modules, at a real mix.
    std::printf ("\nEngines:\n");
    juce::WavAudioFormat wav;

    for (int mod = 0; mod < 4; ++mod)
        for (int rev = 0; rev < 2; ++rev)
        {
            juce::AudioBuffer<float> out (2, kLength);
            out.makeCopyOf (input);

            render (out,
                    [mod, rev] (juce::AudioProcessorValueTreeState& s)
                    {
                        using namespace ee::alpine::id;
                        setChoice (s, modEngine, mod);
                        setChoice (s, revEngine, rev);
                        setPercent (s, modMix, 60.0f);
                        setPercent (s, revMix, 50.0f);
                    });

            const bool ok = allFinite (out) && out.getMagnitude (0, kLength) > 0.01f;

            std::printf ("  %s  mod %d / rev %d  %s  peak %.6f  rms %.6f\n", ok ? "ok  " : "FAIL", mod, rev,
                         checksum (out).toRawUTF8(), out.getMagnitude (0, kLength),
                         out.getRMSLevel (0, 0, kLength));
            if (! ok)
                ++failures;

            if (outDir != juce::File())
            {
                outDir.createDirectory();
                const auto file = outDir.getChildFile ("mod" + juce::String (mod) + "-rev" + juce::String (rev) + ".wav");
                file.deleteFile();

                if (auto stream = file.createOutputStream())
                    if (std::unique_ptr<juce::AudioFormatWriter> writer {
                            wav.createWriterFor (stream.release(), kSampleRate, 2, 24, {}, 0) })
                        writer->writeFromAudioSampleBuffer (out, 0, kLength);
            }
        }

    std::printf ("\nEverything up:\n");
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out, everything);

        std::printf ("  %s  peak %.6f  rms %.6f\n", checksum (out).toRawUTF8(),
                     out.getMagnitude (0, kLength), out.getRMSLevel (0, 0, kLength));
        check (allFinite (out), "every module at once is finite");
        check (out.getMagnitude (0, kLength) < 4.0f, "...and nothing ran away");
    }

    std::printf ("\n%s\n", failures == 0 ? "OK - Peak Alpine works" : "PEAK ALPINE CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
