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
void artifactOff (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::artOn, false); }
void modOff (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::modOn, false); }
void delayOff (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::dlyOn, false); }
void reverbOff (juce::AudioProcessorValueTreeState& s) { setFlag (s, ee::alpine::id::revOn, false); }

/** Every module doing something at once. Note the Shimmer: this case is
    deliberately *not* a reproducible baseline, and its checksum is printed with
    a warning rather than kept.

    DaisySP's PitchShifter - which the Space reverb's shimmer is built on - draws
    its modulation slew coefficients from `daisysp::myrand()`, and that is one
    function-local `static uint32_t seed` shared by every instance in the
    process and advanced per sample from inside `Process()`. So two shimmer
    renders in one process start at different points of the sequence and do not
    agree, and two plugin instances on different audio threads race on it. The
    variation is inaudible - a slightly different approach rate on a modulation
    whose depth is zero unless `SetFun` is called, which nothing here does - but
    it means no shimmered render can be checksummed against another. Everything
    else in this file is bit-reproducible; ee_alpine_host's own diagnostic
    confirmed shimmer is the only stage that is not. */
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
    setPercent (s, revSpaceShimmer, 40.0f);
}

/** Where an impulse actually comes out, against what the plugin tells the host
    to compensate.

    Every Mix is set to 0, so what this measures is the *dry* path - what a host
    lines the rest of the session up against - rather than any engine's own
    delayed output. Two things are worth knowing from it and only one of them is
    asserted.

    The assertion is that the figure does not depend on which Modulation engine
    is selected. Only the Tape engine has a delay line in it; the module pads
    the other three and its own dry path out to match, and if that ever stopped
    happening the Tape engine would comb against the dry at any partial Mix -
    the "Flutter sounds like a chorus" bug, which is what this guards.

    Tape (engine 0) is the exception now: it has no Mix and runs fully wet (see
    ModulationModule::engineUsesMix), so at Mix 0 this measures the Tape
    engine's own output - padded to the same latency, but its peak smeared a
    few samples by the live saturation and wear - rather than the dry path. It
    is printed, not asserted; the dry path under Tape is checked instead with
    the module bypassed, where the output is that padded dry.

    The other is printed and not asserted: switching the **Delay** module off
    takes its 288 samples out of the real path while the plugin goes on
    reporting them, because `ee::fx::DelayModule` crossfades back to the
    caller's untouched buffer rather than to a copy delayed to match. So a host
    compensating the reported figure pulls the signal 6 ms early the moment that
    module is bypassed. It is a known gap, not an accident - see
    docs/peak-alpine-plan.md §7. */
void checkLatencyLedger()
{
    auto arrival = [] (auto&& configure)
    {
        PeakAlpineProcessor p;

        configure (p.apvts);

        p.setPlayConfigDetails (2, 2, kSampleRate, 1024);
        p.prepareToPlay (kSampleRate, 1024);

        constexpr int kImpulseAt = 64;
        juce::AudioBuffer<float> buffer (2, 8192);
        buffer.clear();
        buffer.setSample (0, kImpulseAt, 1.0f);
        buffer.setSample (1, kImpulseAt, 1.0f);

        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);

        int at = 0;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs (buffer.getSample (0, i)) > std::abs (buffer.getSample (0, at)))
                at = i;

        return at - kImpulseAt;
    };

    auto silent = [] (juce::AudioProcessorValueTreeState& s)
    {
        using namespace ee::alpine::id;
        setPercent (s, artMix, 0.0f);
        setPercent (s, modMix, 0.0f);
        setPercent (s, dlyMix, 0.0f);
        setPercent (s, revMix, 0.0f);
    };

    std::printf ("Latency ledger (dry path, every Mix at 0):\n");

    int reported = 0;
    {
        PeakAlpineProcessor p;
        p.setPlayConfigDetails (2, 2, kSampleRate, 1024);
        p.prepareToPlay (kSampleRate, 1024);
        reported = p.getLatencySamples();
    }

    const int whole = arrival (silent);
    std::printf ("  %-30s %4d samples   (reported %d)\n", "everything engaged", whole, reported);
    check (whole == reported, "the dry path arrives exactly where the host is told it will");

    // Which module owns which half - and, on the Delay row, what a bypassed
    // one does to a figure the host is still compensating.
    struct Off { const char* name; const char* id; };
    for (const auto& off : { Off { "Artifact bypassed", ee::alpine::id::artOn },
                             Off { "Modulation bypassed", ee::alpine::id::modOn },
                             Off { "Delay bypassed", ee::alpine::id::dlyOn },
                             Off { "Reverb bypassed", ee::alpine::id::revOn } })
        std::printf ("  %-30s %4d samples\n", off.name,
                     arrival ([&silent, &off] (juce::AudioProcessorValueTreeState& s)
                              { silent (s); setFlag (s, off.id, false); }));

    // The contract: one figure, whichever engine is selected. Engines 1-3 mix
    // their wet against the dry, so at Mix 0 the impulse returns down the dry
    // path and has to land on `reported`. Tape (0) runs fully wet, so its row
    // is its own output and is printed, not asserted.
    bool flat = true;
    for (int engine = 0; engine < 4; ++engine)
    {
        const int at = arrival ([&silent, engine] (juce::AudioProcessorValueTreeState& s)
                                { silent (s); setChoice (s, ee::alpine::id::modEngine, engine); });
        const bool asserted = engine != ee::fx::ModulationModule::Tape;
        std::printf ("  %-22s %-3d %4d samples%s\n", "Modulation engine", engine, at,
                     asserted ? "" : "   (Tape: fully wet, not the dry path)");
        if (asserted)
            flat = flat && at == reported;
    }

    check (flat, "...and does not move with the Modulation engine that mixes");

    // Tape's dry path still has to be padded to the module's latency even
    // though nothing normally hears it alone: with the module bypassed the
    // output is that dry, and it must land on `reported` like the rest.
    const int tapeBypassed = arrival (
        [&silent] (juce::AudioProcessorValueTreeState& s)
        {
            silent (s);
            setChoice (s, ee::alpine::id::modEngine, ee::fx::ModulationModule::Tape);
            setFlag (s, ee::alpine::id::modOn, false);
        });
    std::printf ("  %-26s %4d samples\n", "Tape, module bypassed", tapeBypassed);
    check (tapeBypassed == reported, "the Tape dry path is aligned to the reported latency");
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    juce::AudioBuffer<float> input (2, kLength);
    fillTestSignal (input, kSampleRate);

    std::printf ("=== Peak Alpine host ===\n\n");

    checkLatencyLedger();
    std::printf ("\n");

    // What the plugin tells a host to compensate, across the rates it runs at.
    // Two tape transports plus two tape stages, in samples, so it moves with
    // the sample rate - the ledger below checks the figure is honest at one of
    // them, and this shows it lands at the same 12 ms at all of them.
    std::printf ("Reported latency:\n");
    for (const double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        PeakAlpineProcessor probe;
        probe.setPlayConfigDetails (2, 2, rate, 512);
        probe.prepareToPlay (rate, 512);

        const int reported = probe.getLatencySamples();
        std::printf ("  %6.0f Hz   %5d samples   %5.2f ms\n", rate, reported, 1000.0 * reported / rate);
    }
    std::printf ("\n");

    juce::AudioBuffer<float> plain (2, kLength);
    plain.makeCopyOf (input);
    render (plain, defaults);

    check (allFinite (plain), "the default patch is finite");
    check (plain.getMagnitude (0, kLength) > 0.01f, "...and makes sound");
    check (difference (plain, input) > 0.001f, "...and is not just the input back");

    // Each module's own power toggle has to reach the audio. A module wired to
    // the wrong parameter, or to none, would pass every other check here.
    for (const auto& c : { Case { "Artifact off", artifactOff }, Case { "Modulation off", modOff },
                           Case { "Delay off", delayOff }, Case { "Reverb off", reverbOff } })
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

        std::printf ("  %s  peak %.6f  rms %.6f   (not a baseline - shimmer, see above)\n",
                     checksum (out).toRawUTF8(), out.getMagnitude (0, kLength),
                     out.getRMSLevel (0, 0, kLength));
        check (allFinite (out), "every module at once is finite");
        check (out.getMagnitude (0, kLength) < 4.0f, "...and nothing ran away");
    }

    std::printf ("\n%s\n", failures == 0 ? "OK - Peak Alpine works" : "PEAK ALPINE CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
