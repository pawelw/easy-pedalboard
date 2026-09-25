// What BitBit Alpine costs to run, as a share of one core, for the figures the
// release plan wants published (release-plan.md 1.7 and "Published CPU
// figures"). Drives the real processor like a host - fixed blocks, a rolling
// transport - over a handful of patches, and prints the time spent per second
// of audio.
//
// A timing tool, not a pass/fail suite, and not in regress-baselines.sh: what
// it prints moves with the machine and whatever else is running on it. Take
// the best of several runs, and compare numbers from the same machine only.
//
//   ee_alpine_bench [--seconds N] [--repeats N]
#include <chrono>
#include <cstdio>

#include "Params.h"
#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;

struct Patch
{
    const char* name;
    void (*configure) (juce::AudioProcessorValueTreeState&);
};

void defaults (juce::AudioProcessorValueTreeState&) {}

void allModulesOff (juce::AudioProcessorValueTreeState& s)
{
    using namespace ee::alpine::id;
    for (const char* id : { artOn, modOn, dlyOn, revOn })
        setFlag (s, id, false);
}

void onlyDelay (juce::AudioProcessorValueTreeState& s)
{
    using namespace ee::alpine::id;
    for (const char* id : { artOn, modOn, revOn })
        setFlag (s, id, false);
}

void onlyReverb (juce::AudioProcessorValueTreeState& s)
{
    using namespace ee::alpine::id;
    for (const char* id : { artOn, modOn, dlyOn })
        setFlag (s, id, false);
}

void pluginBypassed (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::on, false); }

/** Every module doing something, the Shimmer reverb and the Advanced pre-EQ
    included - the ceiling. */
void everything (juce::AudioProcessorValueTreeState& s)
{
    using namespace ee::alpine::id;
    setPercent (s, artMix, 60.0f);
    setPercent (s, modMix, 70.0f);
    setPercent (s, dlyMix, 55.0f);
    setPercent (s, dlyFeedback, 65.0f);
    setPercent (s, dlyWear, 60.0f);
    setPercent (s, dlyFlutter, 50.0f);
    setPercent (s, dlyDrift, 40.0f);
    setPercent (s, dlyPhaser, 40.0f);
    setPercent (s, revMix, 60.0f);
    setChoice (s, revEngine, ee::fx::ReverbModule::Shimmer);
    setFlag (s, eqOn, true);
    setChoice (s, eqMode, 1);
}

/** Seconds of CPU per second of audio, as a percentage - the best of `repeats`. */
double measure (const Patch& patch, double rate, int block, int seconds, int repeats)
{
    const int length = static_cast<int> (rate) * seconds;

    juce::AudioBuffer<float> input (2, length);
    fillTestSignal (input, rate);

    double best = 1.0e9;

    for (int r = 0; r < repeats; ++r)
    {
        BitBitAlpineProcessor processor;
        patch.configure (processor.apvts);

        FakePlayHead playHead { 120.0, rate };
        processor.setPlayHead (&playHead);
        processor.setPlayConfigDetails (2, 2, rate, block);
        processor.prepareToPlay (rate, block);

        juce::AudioBuffer<float> work (2, block);
        juce::MidiBuffer midi;

        const auto start = std::chrono::steady_clock::now();

        for (int offset = 0; offset + block <= length; offset += block)
        {
            for (int ch = 0; ch < 2; ++ch)
                work.copyFrom (ch, 0, input, ch, offset, block);

            processor.processBlock (work, midi);
            playHead.advance (block);
        }

        const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
        best = juce::jmin (best, 100.0 * spent.count() / seconds);

        processor.setPlayHead (nullptr);
    }

    return best;
}

} // namespace

int main (int argc, char* argv[])
{
    int seconds = 10;
    int repeats = 3;

    for (int i = 1; i + 1 < argc; i += 2)
    {
        const juce::String flag (argv[i]);
        if (flag == "--seconds")
            seconds = juce::jmax (1, juce::String (argv[i + 1]).getIntValue());
        else if (flag == "--repeats")
            repeats = juce::jmax (1, juce::String (argv[i + 1]).getIntValue());
    }

    const Patch patches[] = {
        { "defaults (all four on)", defaults },
        { "every module off", allModulesOff },
        { "Delay only", onlyDelay },
        { "Reverb only", onlyReverb },
        { "plugin bypassed", pluginBypassed },
        { "everything up", everything },
    };

    struct Setting
    {
        double rate;
        int block;
    };
    const Setting settings[] = { { 48000.0, 128 }, { 96000.0, 256 } };

    std::printf ("=== BitBit Alpine CPU (%% of one core, best of %d x %d s) ===\n\n", repeats, seconds);
    std::printf ("  %-26s", "");
    for (const auto& s : settings)
        std::printf ("  %5.0f k / %-4d", s.rate / 1000.0, s.block);
    std::printf ("\n");

    for (const auto& patch : patches)
    {
        std::printf ("  %-26s", patch.name);
        for (const auto& s : settings)
            std::printf ("  %11.2f %%", measure (patch, s.rate, s.block, seconds, repeats));
        std::printf ("\n");
        std::fflush (stdout);
    }

    return 0;
}
