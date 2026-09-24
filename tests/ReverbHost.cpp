// Drives the real BitBit Reverb processor the way a host does, and asserts the
// things that would make it broken rather than merely different: that both
// engines make sound and stay finite, that the power toggle reaches the audio,
// that switched off is the input, and that the Init factory preset names every
// parameter.
//
// It also prints a checksum per case, so it doubles as the baseline for the next
// change that means to leave the pedal alone - see RegressHarness.h. Shimmer is
// left at 0 throughout: a shimmered render is not reproducible (CLAUDE.md).
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "Params.h"
#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;
namespace id = ee::reverb::id;

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
    over ragged blocks. */
template <typename Fn>
void render (juce::AudioBuffer<float>& out, Fn&& configure)
{
    BitBitReverbProcessor processor;
    configure (processor.apvts);

    processor.setPlayConfigDetails (2, 2, kSampleRate, 1024);
    processor.prepareToPlay (kSampleRate, 1024);

    juce::MidiBuffer midi;
    int blockIndex = 0;

    for (int offset = 0; offset < kLength;)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), kLength - offset);

        juce::AudioBuffer<float> slice (out.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);

        offset += chunk;
    }
}

float difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float worst = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));

    return worst;
}

/** Every parameter is pushed to the far end of its range first, so one Init.xml
    leaves out stays visibly wrong instead of already sitting on its default. */
void checkInitPreset()
{
    std::printf ("Init preset:\n");

    BitBitReverbProcessor p;
    for (auto* param : p.getParameters())
        param->setValueNotifyingHost (param->getDefaultValue() < 0.5f ? 1.0f : 0.0f);

    check (p.presets.factoryNames().contains ("Init"), "the factory bank ships an Init preset");
    check (p.presets.load (ee::plugin::PresetStore::Kind::factory, "Init"), "...and it loads");

    int off = 0;
    for (auto* param : p.getParameters())
        if (std::abs (param->getValue() - param->getDefaultValue()) > 1.0e-4f)
        {
            std::printf ("        %s is %s, not its default\n", param->getName (64).toRawUTF8(),
                         param->getCurrentValueAsText().toRawUTF8());
            ++off;
        }

    check (off == 0, "...and puts every parameter back on its default");
}
} // namespace

int main()
{
    juce::AudioBuffer<float> input (2, kLength);
    fillTestSignal (input, kSampleRate);

    std::printf ("=== BitBit Reverb host ===\n\n");

    checkInitPreset();
    std::printf ("\n");

    {
        BitBitReverbProcessor probe;
        probe.setPlayConfigDetails (2, 2, kSampleRate, 1024);
        probe.prepareToPlay (kSampleRate, 1024);
        check (probe.getLatencySamples() == 0, "all three engines are latency-free, and it says so");
        check (probe.getTailLengthSeconds() > 1.0, "...and it reports a tail for the host to render");
    }

    juce::AudioBuffer<float> plain (2, kLength);
    plain.makeCopyOf (input);
    render (plain, [] (juce::AudioProcessorValueTreeState&) {});

    check (allFinite (plain), "the default patch is finite");
    check (plain.getMagnitude (0, kLength) > 0.01f, "...and makes sound");
    check (difference (plain, input) > 0.001f, "...and is not just the input back");

    // Off is the input, bit for bit. The first 100 ms is left for the ramp.
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out, [] (juce::AudioProcessorValueTreeState& s) { setFlag (s, id::on, false); });

        const int settled = static_cast<int> (kSampleRate * 0.1);
        float worst = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = settled; i < kLength; ++i)
                worst = juce::jmax (worst, std::abs (out.getSample (ch, i) - input.getSample (ch, i)));

        check (worst == 0.0f, "switched off is the input, bit for bit");
    }

    std::printf ("\nEngines:\n");
    static constexpr const char* names[] = { "Spring", "Shimmer", "Studio" };

    for (int engine = 0; engine < ee::fx::ReverbModule::NumEngines; ++engine)
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out,
                [engine] (juce::AudioProcessorValueTreeState& s)
                {
                    setChoice (s, id::engine, engine);
                    setPercent (s, id::mix, 50.0f);
                });

        const bool ok = allFinite (out) && out.getMagnitude (0, kLength) > 0.01f && difference (out, plain) > 0.0f
                        && difference (out, input) > 0.001f;

        std::printf ("  %s  %-8s %s  peak %.6f  rms %.6f\n", ok ? "ok  " : "FAIL", names[engine],
                     checksum (out).toRawUTF8(), out.getMagnitude (0, kLength), out.getRMSLevel (0, 0, kLength));
        if (! ok)
            ++failures;
    }

    std::printf ("\n%s\n", failures == 0 ? "OK - BitBit Reverb works" : "PEAK REVERB CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
