// Drives the real BitBit Modulation processor the way a host does, and asserts
// the things that would make it broken rather than merely different: that every
// engine makes sound and stays finite, that the power toggle reaches the audio,
// that the dry path arrives exactly where the reported latency says it will, and
// that the Init factory preset names every parameter.
//
// It also prints a checksum per case, so it doubles as the baseline for the next
// change that means to leave the pedal alone - see RegressHarness.h.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <tuple>
#include <utility>

#include "Params.h"
#include "PluginProcessor.h"
#include "RegressHarness.h"

namespace
{
using namespace ee::regress;
namespace id = ee::modulation::id;

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
    BitBitModulationProcessor processor;
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

void printCase (const char* name, const juce::AudioBuffer<float>& out, bool ok)
{
    std::printf ("  %s  %-18s %s  peak %.6f  rms %.6f\n", ok ? "ok  " : "FAIL", name, checksum (out).toRawUTF8(),
                 out.getMagnitude (0, kLength), out.getRMSLevel (0, 0, kLength));
    if (! ok)
        ++failures;
}

/** Every parameter is pushed to the far end of its range first, so one Init.xml
    leaves out stays visibly wrong instead of already sitting on its default -
    APVTS::replaceState fills a missing parameter from whatever it held before,
    which is how a partial preset silently inherits from the last one loaded. */
void checkInitPreset()
{
    std::printf ("Init preset:\n");

    BitBitModulationProcessor p;
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

/** Where an impulse comes out with Mix at 0, against what the plugin tells the
    host to compensate. Only Tape has a delay line; the module pads every other
    engine and its dry path out to match, so the figure must not move with the
    engine. Tape itself runs fully wet (no Mix), so it is not the dry path and is
    left out. */
void checkLatency()
{
    std::printf ("Latency:\n");

    const auto arrival = [] (int engine)
    {
        BitBitModulationProcessor p;
        setChoice (p.apvts, id::engine, engine);
        setPercent (p.apvts, id::mix, 0.0f);

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

        return std::pair<int, int> { at - kImpulseAt, p.getLatencySamples() };
    };

    bool honest = true;
    for (int engine = ee::fx::ModulationModule::Tremolo; engine < ee::fx::ModulationModule::NumEngines; ++engine)
    {
        const auto [at, reported] = arrival (engine);
        std::printf ("        engine %d   %4d samples   (reported %d)\n", engine, at, reported);
        honest = honest && at == reported && reported > 0;
    }

    check (honest, "the dry path arrives where the reported latency says, whichever engine mixes");
}
/** Everything neutral, through the real parameters, is the input delayed - bit for
    bit. The Tone is set the way a host or the face sets it, as a normalised 0.5,
    which JUCE's interval rounding turns into 1.49e-6 rather than 0; that used to
    switch the tilt on and run the whole engine +1.7 dB hot. Tape has no Mix, so
    this is the only way to null-test it. */
void checkTapeAtRestIsTransparent()
{
    std::printf ("Tape at rest:\n");

    BitBitModulationProcessor p;
    setChoice (p.apvts, id::engine, ee::fx::ModulationModule::Tape);
    setPercent (p.apvts, id::tapeSat, 0.0f);
    setPercent (p.apvts, id::tapeFlutter, 0.0f);
    setPercent (p.apvts, id::tapeWear, 0.0f);
    setPercent (p.apvts, id::tapeNoise, 0.0f);
    setFlag (p.apvts, id::tapeStereo, false);
    p.apvts.getParameter (id::tapeTone)->setValueNotifyingHost (0.5f);

    p.setPlayConfigDetails (2, 2, kSampleRate, 512);
    p.prepareToPlay (kSampleRate, 512);

    const int total = static_cast<int> (kSampleRate) * 2;
    juce::AudioBuffer<float> input (2, total), output (2, total);
    fillTestSignal (input, kSampleRate);
    output.makeCopyOf (input);

    juce::MidiBuffer midi;
    for (int offset = 0; offset < total; offset += 512)
    {
        juce::AudioBuffer<float> slice (output.getArrayOfWritePointers(), 2, offset, juce::jmin (512, total - offset));
        p.processBlock (slice, midi);
    }

    const int latency = p.getLatencySamples();
    float worst = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = static_cast<int> (kSampleRate * 0.5); i < total; ++i)
            worst = juce::jmax (worst, std::abs (output.getSample (ch, i) - input.getSample (ch, i - latency)));

    std::printf ("        latency %d, worst difference %.2e\n", latency, worst);
    check (worst == 0.0f, "Tape with everything neutral is the input delayed by the reported latency, bit for bit");
}
} // namespace

int main()
{
    juce::AudioBuffer<float> input (2, kLength);
    fillTestSignal (input, kSampleRate);

    std::printf ("=== BitBit Modulation host ===\n\n");

    checkInitPreset();
    std::printf ("\n");

    checkLatency();
    std::printf ("\n");

    checkTapeAtRestIsTransparent();
    std::printf ("\n");

    std::printf ("Defaults:\n");
    juce::AudioBuffer<float> plain (2, kLength);
    plain.makeCopyOf (input);
    render (plain, [] (juce::AudioProcessorValueTreeState&) {});

    check (allFinite (plain), "the default patch is finite");
    check (plain.getMagnitude (0, kLength) > 0.01f, "...and makes sound");
    check (difference (plain, input) > 0.001f, "...and is not just the input back");

    // Off is the input delayed by the module's latency - what the host is
    // compensating - bit for bit. The first 100 ms is left for the engage ramp.
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out, [] (juce::AudioProcessorValueTreeState& s) { setFlag (s, id::on, false); });

        BitBitModulationProcessor probe;
        probe.setPlayConfigDetails (2, 2, kSampleRate, 1024);
        probe.prepareToPlay (kSampleRate, 1024);
        const int latency = probe.getLatencySamples();

        const int settled = static_cast<int> (kSampleRate * 0.1);
        float worst = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = settled; i < kLength; ++i)
                worst = juce::jmax (worst, std::abs (out.getSample (ch, i) - input.getSample (ch, i - latency)));

        check (worst == 0.0f, "switched off is the input, delayed by the reported latency, bit for bit");
    }

    // Every engine, at a real mix.
    std::printf ("\nEngines:\n");
    static constexpr const char* names[] = { "Tape", "Tremolo", "Chorus", "Phaser", "Filter" };

    for (int engine = 0; engine < ee::fx::ModulationModule::NumEngines; ++engine)
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out,
                [engine] (juce::AudioProcessorValueTreeState& s)
                {
                    setChoice (s, id::engine, engine);
                    setPercent (s, id::mix, 60.0f);
                });

        printCase (names[engine], out, allFinite (out) && out.getMagnitude (0, kLength) > 0.01f
                                           && difference (out, input) > 0.001f);
    }

    // Both tempo-locked LFOs with their Sync switch on and the transport
    // rolling - the host-grid alignment the free-running cases never reach.
    std::printf ("\nSynced LFOs:\n");
    for (const auto& [name, engine, syncId] :
         { std::tuple { "Tremolo, synced", static_cast<int> (ee::fx::ModulationModule::Tremolo), id::tremSync },
           std::tuple { "Filter, synced", static_cast<int> (ee::fx::ModulationModule::Filter), id::filterSync } })
    {
        juce::AudioBuffer<float> out (2, kLength);
        out.makeCopyOf (input);
        render (out,
                [engine, syncId] (juce::AudioProcessorValueTreeState& s)
                {
                    setChoice (s, id::engine, engine);
                    setFlag (s, syncId, true);
                    setPercent (s, id::mix, 60.0f);
                });

        printCase (name, out, allFinite (out) && out.getMagnitude (0, kLength) > 0.01f);
    }

    std::printf ("\n%s\n", failures == 0 ? "OK - BitBit Modulation works" : "PEAK MODULATION CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
