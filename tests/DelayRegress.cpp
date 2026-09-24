// Renders a fixed battery of settings through the whole BitBit Delay processor
// and prints a checksum per pass, so the pedal's output can be compared byte
// for byte across a change that is supposed to change nothing.
//
// Written for the extraction of ee::fx::DelayModule out of this processor. See
// RegressHarness.h for what a *_regress tool is; ee_delay_match is the other
// thing entirely - a voicing renderer that takes a real file.
//
// The battery is wider than Trem & Pan's because the chain is: three routings,
// tape on the repeats, the filter pair either end of its travel, the in-loop
// phaser, and the Sync switch, which is the only reason the playhead matters.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

#include "PluginProcessor.h"
#include "RegressHarness.h"
#include "ee/fx/DelayTimeMap.h"

namespace
{
using namespace ee::regress;

constexpr double kSampleRate = 48000.0;
// Long enough for a 500 ms delay at high feedback to be well into its tail
// before the render stops - a refactor that gets the feedback path subtly
// wrong shows up in the tail, not in the first repeat.
constexpr int kSeconds = 4;
constexpr int kLength = static_cast<int> (kSampleRate) * kSeconds;

struct Pass
{
    const char* name;
    float mix;      // %
    float feedback; // %
    float ltime01;  // Time knobs are note divisions or ms - their own 0..1
    float rtime01;
    float wear;    // %  ("tape")
    float flutter; // %
    float drift;   // %  ("mod") - in the feedback loop
    float phaser;  // %  - on the repeats
    float locut;   // %
    float hicut;   // %
    int routing;   // dtype: 0 Normal, 1 Wide, 2 Ping Pong
    bool synced; // "timeunit" is inverted - see below
    bool engaged;
};

const Pass kPasses[] = {
    // The plain pedal, and the two routings that rewire it.
    { "normal, clean", 35.0f, 35.0f, 0.50f, 0.50f, 0, 0, 0, 0, 0, 100, 0, true, true },
    { "wide", 35.0f, 35.0f, 0.50f, 0.42f, 0, 0, 0, 0, 0, 100, 1, true, true },
    { "ping pong", 35.0f, 45.0f, 0.50f, 0.50f, 0, 0, 0, 0, 0, 100, 2, true, true },
    // Uneven sides, which the L/R mirror must not have collapsed.
    { "unlinked times", 40.0f, 50.0f, 0.30f, 0.70f, 0, 0, 0, 0, 0, 100, 0, true, true },
    // The tape section, on the repeats.
    { "tape", 40.0f, 40.0f, 0.50f, 0.50f, 70, 60, 0, 0, 0, 100, 0, true, true },
    // Drift compounds inside the loop; the phaser is a single pass on the wet.
    { "drift", 45.0f, 60.0f, 0.50f, 0.50f, 0, 0, 80, 0, 0, 100, 0, true, true },
    { "phaser", 45.0f, 40.0f, 0.50f, 0.50f, 0, 0, 0, 80, 0, 100, 0, true, true },
    // Both cuts off their resting positions, where runFilter stops bypassing.
    { "filter both ends", 45.0f, 40.0f, 0.50f, 0.50f, 0, 0, 0, 0, 40, 45, 0, true, true },
    // Free-running: the Time knobs stop being divisions and the tempo is not
    // consulted at all.
    { "free (ms), not synced", 35.0f, 35.0f, 0.55f, 0.55f, 0, 0, 0, 0, 0, 100, 0, false, true },
    // Everything at once, at a feedback that really rings.
    { "everything, high feedback", 60.0f, 80.0f, 0.35f, 0.55f, 55, 45, 40, 35, 25, 60, 1, true, true },
    // The bypass crossfade, and the two ends of the Mix law.
    { "bypassed (trails)", 50.0f, 60.0f, 0.50f, 0.50f, 40, 40, 0, 0, 0, 100, 0, true, false },
    { "mix 0", 0.0f, 50.0f, 0.50f, 0.50f, 40, 40, 0, 0, 0, 100, 0, true, true },
    { "mix 100", 100.0f, 50.0f, 0.50f, 0.50f, 0, 0, 0, 0, 0, 100, 0, true, true },
};

bool runPass (const Pass& pass, const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    BitBitDelayProcessor processor;

    setPercent (processor.apvts, "mix", pass.mix);
    setPercent (processor.apvts, "fb", pass.feedback);
    setNormalised (processor.apvts, "ltime", pass.ltime01);
    setNormalised (processor.apvts, "rtime", pass.rtime01);
    setPercent (processor.apvts, "tape", pass.wear);
    setPercent (processor.apvts, "flutter", pass.flutter);
    setPercent (processor.apvts, "mod", pass.drift);
    setPercent (processor.apvts, "phaser", pass.phaser);
    setPercent (processor.apvts, "locut", pass.locut);
    setPercent (processor.apvts, "hicut", pass.hicut);
    setChoice (processor.apvts, "dtype", pass.routing);
    // "timeunit" reads "is this in ms mode", so synced is its inverse - the
    // same inversion the face's Sync pill draws.
    setFlag (processor.apvts, "timeunit", ! pass.synced);
    setFlag (processor.apvts, "on", pass.engaged);

    // "sync" is the L/R link, not the tempo switch. Off throughout, so the
    // uneven-times pass above stays uneven.
    setFlag (processor.apvts, "sync", false);

    FakePlayHead playHead { 132.0, kSampleRate };
    processor.setPlayHead (&playHead);

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

        playHead.advance (chunk);
        offset += chunk;
    }

    processor.setPlayHead (nullptr);
    return allFinite (output);
}
/** Timing, which the checksums above would happily freeze in the wrong place: the
    dry note goes straight through with no latency, and the first repeat lands
    where the Time knob says - including at the very shortest time the knob can
    make, which is where the tape's own delay (taken off the delay line's time)
    is closest to the delay line's 1 ms floor.

    The shortest is 1/32 note at the fastest tempo the plugin will use (300 bpm),
    25 ms - about seven times the tape's 3.35 ms, so the floor is never reached
    and the trim is always exact. That is asserted here rather than assumed.

    Wear and Flutter are at 0 so the tape stage is a whole-sample pass-through
    and the peak is one sample, not a smear. */
bool checkTiming()
{
    constexpr int kImpulseAt = 200;
    std::printf ("Timing (impulse, Mix 50 %%, no feedback):\n");

    struct Case
    {
        const char* name;
        float time01;
        bool synced;
        double bpm;
    };

    const Case cases[] = {
        { "free, shortest", 0.0f, false, 120.0 },   { "free, mid", 0.5f, false, 120.0 },
        { "free, long", 0.7f, false, 120.0 },       { "1/32 at 300 bpm (the shortest)", 0.0f, true, 300.0 },
        { "1/16 at 300 bpm", 2.0f / 14.0f, true, 300.0 },
    };

    bool ok = true;

    for (const auto& c : cases)
    {
        BitBitDelayProcessor p;
        setPercent (p.apvts, "mix", 50.0f);
        setPercent (p.apvts, "fb", 0.0f);
        setPercent (p.apvts, "tape", 0.0f);
        setPercent (p.apvts, "flutter", 0.0f);
        setNormalised (p.apvts, "ltime", c.time01);
        setNormalised (p.apvts, "rtime", c.time01);
        setFlag (p.apvts, "timeunit", ! c.synced); // "timeunit" is true for ms
        setFlag (p.apvts, "sync", false);

        FakePlayHead playHead { c.bpm, kSampleRate };
        p.setPlayHead (&playHead);
        p.setPlayConfigDetails (2, 2, kSampleRate, 1024);
        p.prepareToPlay (kSampleRate, 1024);

        const int expectedRepeat =
            kImpulseAt
            + static_cast<int> (std::lround (ee::bitbitdelay::timeSeconds (c.time01, c.synced, c.bpm) * kSampleRate));

        juce::AudioBuffer<float> buffer (2, expectedRepeat + 4096);
        buffer.clear();
        buffer.setSample (0, kImpulseAt, 1.0f);
        buffer.setSample (1, kImpulseAt, 1.0f);

        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        p.setPlayHead (nullptr);

        const auto peakIn = [&] (int from, int to)
        {
            int at = from;
            for (int i = from; i < to; ++i)
                if (std::abs (buffer.getSample (0, i)) > std::abs (buffer.getSample (0, at)))
                    at = i;
            return at;
        };

        const int dryAt = peakIn (0, kImpulseAt + 64);
        const int repeatAt = peakIn (kImpulseAt + 64, buffer.getNumSamples());

        std::printf ("  %-32s %6.1f ms: latency %d, dry at %+d, first repeat %+d from the knob\n", c.name,
                     1000.0 * (expectedRepeat - kImpulseAt) / kSampleRate, p.getLatencySamples(), dryAt - kImpulseAt,
                     repeatAt - expectedRepeat);

        ok = ok && p.getLatencySamples() == 0 && dryAt == kImpulseAt && std::abs (repeatAt - expectedRepeat) <= 1;
    }

    std::printf ("  %s  the dry note is undelayed and the first repeat lands on the Time knob, shortest included\n\n",
                 ok ? "ok  " : "FAIL");
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
    bool ok = checkTiming();

    std::printf ("BitBit Delay - %d passes at %.0f Hz, %d s each, ragged blocks\n\n",
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
