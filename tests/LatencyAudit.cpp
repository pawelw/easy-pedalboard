// Does the dry path arrive where the plugin tells the host it will?
//
// release-plan.md G5.3. A host compensates the figure getLatencySamples()
// reports, so the whole promise is: at Mix 0 the plugin's output is its input,
// delayed by exactly that many samples, whatever else has been done to it. This
// is the offline half of the null test the plan asks for in a real host (same
// audio on two tracks, one through the plugin at Mix 0, polarity inverted) - it
// runs through the real processor, at every sample rate a host runs at, over
// ragged block sizes, and it does not need a DAW.
//
//   ee_latency_audit_<Target> [--rate HZ] [--verbose]
//
// One binary per product, like ee_param_golden and ee_soak: every pedal's
// PluginProcessor.cpp defines createPluginFilter().
//
// The processor is not told what its parameters mean. They are found by the
// naming the whole tree already follows:
//   - an id ending "mix" is a wet/dry control and is put to 0;
//   - BitBit Grains has a mixer rather than a Mix: `grains` goes to 0, and Dry
//     is left where it is (its default is unity), so what reaches the output
//     is the played note alone;
//   - every other boolean and choice parameter is then flipped through each of
//     its values, *live*, after prepareToPlay - the way a player or an
//     automation lane does it - and the dry path is checked again.
// A choice called "Tape" is skipped: BitBit Modulation's Tape engine has no Mix
// and runs fully wet (ModulationModule::engineUsesMix), so at Mix 0 it measures
// that engine's own output, not the dry path. The module bypassed covers its
// dry path instead, and the audit does cover that (its `on` is a boolean).
//
// The host's own bypass is one more scenario: it goes through
// processBlockBypassed, which JUCE defaults to a pass-through with no delay.
//
// What is asserted, per scenario and per sample rate:
//   - getLatencySamples() has not moved from what prepareToPlay reported.
//     A host reads it once per prepare, so a figure that changes underneath it
//     is a stale-PDC bug by construction.
//   - the output, after the smoothers have settled, is the input delayed by
//     that figure to within kNullFloor.
// A failure prints the lag that *would* have nulled it, which is the size of
// the mistake.
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "PluginProcessor.h"

#ifndef EE_LAT_PROCESSOR
#error "EE_LAT_PROCESSOR must name the processor class"
#endif

#ifndef EE_LAT_NAME
#define EE_LAT_NAME "plugin"
#endif

namespace
{
constexpr int kChannels = 2;

/** Residual, relative to the input's RMS, below which the dry path counts as
    the input delayed. At Mix 0 the path is multiplies by 1 and adds of 0, so
    this is loose by design - a real misalignment is around 0 dB. */
constexpr double kNullFloor = 1.0e-4;

/** The seconds of noise before measuring: enough for every 20 ms smoother and
    the bypass crossfade to have landed. */
constexpr double kSettleSeconds = 0.4;
constexpr double kMeasureSeconds = 0.25;

struct Scenario
{
    juce::String name;
    /** Drive the block through processBlockBypassed - what a host calls when
        its own bypass button is down - instead of processBlock. */
    bool hostBypass = false;
    /** Applied live, after prepareToPlay. */
    std::vector<std::pair<int, float>> moves; // parameter index, normalised value
};

struct Outcome
{
    bool ok = true;
    int reportedBefore = 0;
    int reportedAfter = 0;
    double residual = 0.0;
    int bestLag = 0;
};

std::unique_ptr<juce::AudioProcessor> make()
{
    return std::unique_ptr<juce::AudioProcessor> (new EE_LAT_PROCESSOR());
}

bool endsWith (const juce::String& s, const char* suffix)
{
    return s.endsWith (suffix);
}

juce::String idOf (const juce::AudioProcessorParameter* p)
{
    if (auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*> (p))
        return withId->paramID;

    return {};
}

/** Normalised value of the nth step of a discrete parameter. */
float normalisedStep (juce::RangedAudioParameter& p, int step)
{
    const auto range = p.getNormalisableRange();
    return p.convertTo0to1 (range.start + static_cast<float> (step) * (range.interval > 0.0f ? range.interval : 1.0f));
}

int stepCount (juce::RangedAudioParameter& p)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p))
        return c->choices.size();

    if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
        return 2;

    return 0;
}

juce::String stepText (juce::RangedAudioParameter& p, int step)
{
    return p.getText (normalisedStep (p, step), 32);
}

/** Runs one scenario at one rate. Returns the outcome; `noise` is the shared
    input. */
Outcome run (const Scenario& scenario, double rate, const std::vector<float>& noise)
{
    Outcome out;

    auto proc = make();
    proc->setPlayConfigDetails (kChannels, kChannels, rate, 512);
    proc->prepareToPlay (rate, 512);
    out.reportedBefore = proc->getLatencySamples();

    for (auto [index, value] : scenario.moves)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc->getParameters()[index]))
            p->setValueNotifyingHost (value);

    const int total = static_cast<int> (noise.size());
    juce::AudioBuffer<float> in (kChannels, total);
    juce::AudioBuffer<float> result (kChannels, total);

    for (int ch = 0; ch < kChannels; ++ch)
        for (int i = 0; i < total; ++i)
            in.setSample (ch, i, noise[static_cast<size_t> (i)] * (ch == 0 ? 1.0f : -0.7f));

    result.makeCopyOf (in);

    static const int kBlocks[] = { 512, 64, 997, 33 };
    int at = 0, b = 0;
    juce::MidiBuffer midi;

    while (at < total)
    {
        const int n = juce::jmin (kBlocks[b++ % 4], total - at);
        juce::AudioBuffer<float> view (result.getArrayOfWritePointers(), kChannels, at, n);
        if (scenario.hostBypass)
            proc->processBlockBypassed (view, midi);
        else
            proc->processBlock (view, midi);

        at += n;
    }

    out.reportedAfter = proc->getLatencySamples();
    proc->releaseResources();

    const int from = static_cast<int> (kSettleSeconds * rate);
    const int to = juce::jmin (total, from + static_cast<int> (kMeasureSeconds * rate));

    auto residualAt = [&] (int lag)
    {
        double num = 0.0, den = 0.0;

        for (int ch = 0; ch < kChannels; ++ch)
            for (int i = from; i < to; ++i)
            {
                const double want = in.getSample (ch, i - lag);
                const double d = result.getSample (ch, i) - want;
                num += d * d;
                den += want * want;
            }

        return den > 0.0 ? std::sqrt (num / den) : 1.0;
    };

    out.residual = residualAt (out.reportedBefore);
    out.bestLag = out.reportedBefore;
    out.ok = out.reportedBefore == out.reportedAfter && out.residual < kNullFloor;

    if (! out.ok)
    {
        double best = out.residual;

        for (int lag = 0; lag <= out.reportedBefore * 3 + 256 && lag < from; ++lag)
        {
            const double r = residualAt (lag);

            if (r < best)
            {
                best = r;
                out.bestLag = lag;
            }
        }
    }

    return out;
}
} // namespace

int main (int argc, char* argv[])
{
    double onlyRate = 0.0;
    bool verbose = false;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--rate") == 0 && i + 1 < argc)
            onlyRate = std::atof (argv[++i]);
        else if (std::strcmp (argv[i], "--verbose") == 0)
            verbose = true;
    }

    std::printf ("=== %s latency audit ===\n\n", EE_LAT_NAME);

    // Which parameters, and which scenarios, from the tree itself.
    auto probe = make();
    auto& params = probe->getParameters();

    std::vector<std::pair<int, float>> mixesOff;
    for (int i = 0; i < params.size(); ++i)
    {
        const auto id = idOf (params[i]);

        if (endsWith (id, "mix") || id == "grains")
            mixesOff.push_back ({ i, 0.0f });
    }

    std::vector<Scenario> scenarios;
    scenarios.push_back ({ "every Mix at 0", false, mixesOff });

    // The host's own bypass button. JUCE's default is a zero-latency
    // pass-through, which is the signal jumping ahead of a figure the host has
    // just compensated for; it is asserted here, on every product, so a new one
    // cannot forget. Mixes are at 0 like everywhere else, for the same reason:
    // bypass lets the repeats and the tail ring out (trails), and those are the
    // effect, not the dry path this is measuring.
    scenarios.push_back ({ "host bypass", true, mixesOff });

    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (params[i]);

        const auto id = idOf (p);

        if (p == nullptr || endsWith (id, "mix") || id == "grains" || id == "mlink")
            continue; // mlink mirrors Dry onto Grains, which undoes the `grains` = 0 above

        const int steps = stepCount (*p);

        for (int s = 0; s < steps; ++s)
        {
            const juce::String text = stepText (*p, s);

            if (text.equalsIgnoreCase ("Tape") && endsWith (idOf (p), "engine"))
                continue;

            Scenario sc { idOf (p) + " = " + text, false, mixesOff };
            sc.moves.push_back ({ i, normalisedStep (*p, s) });
            scenarios.push_back (std::move (sc));
        }
    }

    std::vector<double> rates { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    if (onlyRate > 0.0)
        rates = { onlyRate };

    std::printf ("%d scenarios x %d rates\n\n", static_cast<int> (scenarios.size()), static_cast<int> (rates.size()));

    int failures = 0;

    for (const double rate : rates)
    {
        juce::Random rng (0x1a7e11c9);
        std::vector<float> noise (static_cast<size_t> ((kSettleSeconds + kMeasureSeconds + 0.05) * rate));
        for (auto& v : noise)
            v = (rng.nextFloat() * 2.0f - 1.0f) * 0.25f;

        int reported = -1;
        int bad = 0;

        for (const auto& sc : scenarios)
        {
            const auto o = run (sc, rate, noise);

            if (reported < 0)
                reported = o.reportedBefore;

            if (! o.ok || verbose)
            {
                std::printf ("  %s  %8.0f Hz  %-34s reported %4d -> %4d   residual %.2e",
                             o.ok ? "ok  " : "FAIL", rate, sc.name.toRawUTF8(), o.reportedBefore, o.reportedAfter,
                             o.residual);

                if (! o.ok)
                    std::printf ("   arrives at %d (%+d)", o.bestLag, o.bestLag - o.reportedBefore);

                std::printf ("\n");
            }

            if (! o.ok)
                ++bad;
        }

        std::printf ("%8.0f Hz   reported %5d samples (%.2f ms)   %d of %d scenarios %s\n", rate, reported,
                     1000.0 * reported / rate, bad, static_cast<int> (scenarios.size()),
                     bad == 0 ? "aligned" : "MISALIGNED");
        failures += bad;
    }

    std::printf ("\n%s\n", failures == 0 ? "OK - the dry path lands where the host is told" : "LATENCY AUDIT FAILED");
    return failures == 0 ? 0 : 1;
}
