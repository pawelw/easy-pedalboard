// Drives the whole Peak Grain chain - the grain cloud into the reverb it feeds
// - over its knob range against adverse input, hunting for a non-finite or a
// runaway output.
//
// The grainer used to be feed-forward and unable to latch a NaN; the Feedback
// knob changed that, so the range is swept here against DC and NaN input with
// the loop wound to its ceiling. The reverb behind it can latch one too, which
// is why they are tested joined up: whatever the cloud does at the extremes -
// feedback wound up, or a frozen buffer scanned by Stretch - has to stay
// something the network can survive being fed.

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/Grainer.h"
#include "ee/dsp/GrainerConfig.h"
#include "ee/dsp/GrainerTuning.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

int failures = 0;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::printf ("  FAIL  %s\n", what.toRawUTF8());
        ++failures;
    }
}

/** What kind of unpleasantness goes in. */
enum class Input
{
    noise,      // full scale, the loudest thing the pedal will ever see
    dc,         // a stuck offset, which a granular buffer will happily recirculate
    impulses,   // sparse full-scale spikes, the worst case for a window edge
    poison      // one NaN, to prove the guard on the way into the buffer holds
};

const char* nameOf (Input input)
{
    switch (input)
    {
        case Input::noise:    return "noise";
        case Input::dc:       return "DC";
        case Input::impulses: return "impulses";
        case Input::poison:   return "NaN burst";
    }
    return "?";
}

struct Result
{
    float peak = 0.0f;
    bool finite = true;
};

Result run (float sizeMs, float densityHz, float timeMs, float pitch, float feedback, bool freeze, float stretch,
            float verbDecaySeconds, float verb, Input input)
{
    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (sizeMs);
    grainer.setDensityHz (densityHz);
    grainer.setTimeMs (timeMs);
    grainer.setFeedback (feedback);
    grainer.setStretch (stretch);
    grainer.setPitchMix (juce::jmax (0.0f, -pitch), 1.0f - std::abs (pitch), juce::jmax (0.0f, pitch));

    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setDecayTime (verbDecaySeconds);
    reverb.setResonance (ee::dsp::GrainerTuning{}.verbResonance);
    reverb.setShimmer (ee::dsp::config::kVerbShimmer);
    reverb.setLowCut (ee::dsp::GrainerTuning{}.verbLowCutHz);

    const float grainGain = std::cos (verb * juce::MathConstants<float>::halfPi);
    const float verbGain = std::sin (verb * juce::MathConstants<float>::halfPi) * 1.1f;

    std::mt19937 rng (4242);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    std::vector<float> inL (kBlock), inR (kBlock), mono (kBlock), wetL (kBlock), wetR (kBlock);

    Result result;
    int n = 0;

    // A second of input, then a second and a half of silence so the tail is
    // measured too - that is where a latched non-finite would show itself, and
    // it shows up in the first moments of one rather than at the end.
    const int drivenBlocks = static_cast<int> (kSampleRate * 1.0 / kBlock);
    const int totalBlocks = drivenBlocks + static_cast<int> (kSampleRate * 1.5 / kBlock);

    for (int b = 0; b < totalBlocks; ++b)
    {
        const bool driven = b < drivenBlocks;

        // Freeze at the moment the input stops, so Stretch is left scanning the
        // captured buffer for the whole tail.
        if (freeze && b == drivenBlocks)
            grainer.setFreeze (true);

        for (int i = 0; i < kBlock; ++i, ++n)
        {
            float s = 0.0f;

            if (driven)
            {
                switch (input)
                {
                    case Input::noise:    s = noise (rng); break;
                    case Input::dc:       s = 1.0f; break;
                    case Input::impulses: s = (n % 4096) == 0 ? 1.0f : 0.0f; break;
                    case Input::poison:
                        s = (n % 8192) == 0 ? std::numeric_limits<float>::quiet_NaN() : noise (rng);
                        break;
                }
            }

            inL[static_cast<size_t> (i)] = s;
            inR[static_cast<size_t> (i)] = s;
        }

        grainer.process (inL.data(), inR.data(), inL.data(), inR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
            mono[static_cast<size_t> (i)] = 0.5f * (inL[static_cast<size_t> (i)] + inR[static_cast<size_t> (i)]);

        reverb.process (mono.data(), wetL.data(), wetR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            const float l = inL[static_cast<size_t> (i)] * grainGain + wetL[static_cast<size_t> (i)] * verbGain;
            const float r = inR[static_cast<size_t> (i)] * grainGain + wetR[static_cast<size_t> (i)] * verbGain;

            if (! std::isfinite (l) || ! std::isfinite (r))
                result.finite = false;

            result.peak = juce::jmax (result.peak, juce::jmax (std::abs (l), std::abs (r)));
        }
    }

    return result;
}
} // namespace

int main()
{
    namespace cfg = ee::dsp::config;

    // The grid is deliberately coarse. Every axis is swept to its ends, which is
    // where the buffer arithmetic and the network's stability actually break,
    // and the middle of each is left to ee_dsp_tests - which sweeps the engine
    // far more finely and does it without a reverb attached, so it costs a
    // fraction of the time. A stress app nobody waits for gets run by nobody.
    std::printf ("Peak Grain stress: grain cloud into the reverb it feeds\n\n");

    const float sizes[] = { cfg::kMinGrainMs, 120.0f, cfg::kMaxGrainMs };
    const float densities[] = { cfg::kMinDensityHz, cfg::kMaxDensityHz };
    const float grainTimes[] = { cfg::kMinTimeMs, 400.0f, cfg::kMaxTimeMs };
    const float feedbacks[] = { 0.0f, cfg::kMaxFeedback };
    const float pitches[] = { -1.0f, 0.0f, 1.0f };
    const float decays[] = { ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay };
    const float verbs[] = { 0.0f, 1.0f };
    const Input inputs[] = { Input::noise, Input::dc, Input::impulses, Input::poison };

    // Freeze is its own short pass: it does not interact with the reverb tail,
    // and pairing every stretch rate with the full grid would triple the run.
    struct FreezeCase
    {
        bool freeze;
        float stretch;
    };
    const FreezeCase freezeCases[] = { { false, 1.0f }, { true, 0.0f }, { true, 1.0f }, { true, -1.0f } };

    for (Input input : inputs)
    {
        float worstPeak = 0.0f;
        bool finite = true;

        juce::String worstAt;

        for (float size : sizes)
            for (float density : densities)
                for (float grainTime : grainTimes)
                    for (float feedback : feedbacks)
                        for (float pitch : pitches)
                            for (float decay : decays)
                                for (float verb : verbs)
                                {
                                    const auto result =
                                        run (size, density, grainTime, pitch, feedback, false, 1.0f, decay, verb, input);

                                    finite = finite && result.finite;

                                    if (result.peak > worstPeak)
                                    {
                                        worstPeak = result.peak;
                                        worstAt = "size " + juce::String (size, 0) + " ms, density "
                                                  + juce::String (density, 0) + " /s, time "
                                                  + juce::String (grainTime, 0) + " ms, fb " + juce::String (feedback, 2)
                                                  + ", pitch " + juce::String (pitch, 1) + ", verb decay "
                                                  + juce::String (decay, 1) + " s, verb " + juce::String (verb, 2);
                                    }
                                }

        for (const auto& fc : freezeCases)
            for (float size : sizes)
                for (float pitch : pitches)
                {
                    const auto result = run (size, cfg::kMaxDensityHz, 400.0f, pitch, cfg::kMaxFeedback, fc.freeze,
                                             fc.stretch, ee::dsp::FdnReverb::kMaxDecay, 1.0f, input);

                    finite = finite && result.finite;

                    if (result.peak > worstPeak)
                    {
                        worstPeak = result.peak;
                        worstAt = "freeze " + juce::String (fc.freeze ? 1 : 0) + ", stretch "
                                  + juce::String (fc.stretch, 1) + ", size " + juce::String (size, 0) + " ms, pitch "
                                  + juce::String (pitch, 1);
                    }
                }

        std::printf ("%-10s worst peak %.3g\n", nameOf (input), worstPeak);
        std::printf ("           at %s\n", worstAt.toRawUTF8());

        check (finite, juce::String (nameOf (input)) + " produced a non-finite sample");
        check (worstPeak < 8.0f,
               juce::String (nameOf (input)) + " ran away (peak " + juce::String (worstPeak, 2) + ")");
    }

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "GRAIN STRESS PASSED" : "GRAIN STRESS FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
