#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ee/dsp/FdnReverb.h"

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

struct ImpulseResult
{
    double rt60Seconds = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

/** Fires an impulse into the reverb and tracks the decay envelope. */
ImpulseResult measureImpulse (float decaySeconds, float modulation)
{
    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setDecayTime (decaySeconds);
    reverb.setModulation (modulation);

    // Let the smoothed delay lengths settle before the impulse goes in.
    std::vector<float> silence (kBlock, 0.0f);
    std::vector<float> l (kBlock), r (kBlock);
    for (int i = 0; i < static_cast<int> (kSampleRate / kBlock); ++i)
        reverb.process (silence.data(), l.data(), r.data(), kBlock);

    ImpulseResult result;

    const int totalBlocks = static_cast<int> (kSampleRate * 25.0 / kBlock);
    const float threshold = 0.001f; // -60 dB relative to peak
    double peakTime = 0.0;
    double belowTime = -1.0;

    std::vector<float> in (kBlock, 0.0f);
    in[0] = 1.0f;

    for (int b = 0; b < totalBlocks; ++b)
    {
        reverb.process (in.data(), l.data(), r.data(), kBlock);
        std::fill (in.begin(), in.end(), 0.0f);

        float blockPeak = 0.0f;
        for (int i = 0; i < kBlock; ++i)
        {
            const float m = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            if (! std::isfinite (m))
                result.finite = false;
            blockPeak = juce::jmax (blockPeak, m);
        }

        const double t = static_cast<double> (b * kBlock) / kSampleRate;

        if (blockPeak > result.peak)
        {
            result.peak = blockPeak;
            peakTime = t;
            belowTime = -1.0;
        }

        if (belowTime < 0.0 && result.peak > 0.0f && blockPeak < result.peak * threshold)
            belowTime = t;
    }

    result.rt60Seconds = belowTime < 0.0 ? -1.0 : belowTime - peakTime;
    return result;
}

void testDecayAccuracy()
{
    std::printf ("Decay accuracy (target -> measured RT60):\n");

    for (const float target : { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f })
    {
        const auto res = measureImpulse (target, 0.25f);

        check (res.finite, "output contained NaN or Inf at decay " + juce::String (target));
        check (res.rt60Seconds > 0.0, "tail never decayed to -60 dB at decay " + juce::String (target));

        if (res.rt60Seconds > 0.0)
        {
            const double ratio = res.rt60Seconds / static_cast<double> (target);
            std::printf ("  %5.1f s -> %5.2f s  (%.2fx)  peak %.3f\n",
                         target, res.rt60Seconds, ratio, res.peak);

            // The fixed voicing lets lows ring past the nominal time, so a
            // broadband measurement should sit a little above 1.0x, not wildly off.
            check (ratio > 0.7 && ratio < 1.8,
                   "RT60 out of range at decay " + juce::String (target)
                       + " (ratio " + juce::String (ratio, 2) + ")");
        }
    }
}

void testStabilityUnderLoad()
{
    std::printf ("Stability under sustained full-scale noise:\n");

    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setDecayTime (ee::dsp::FdnReverb::kMaxDecay);
    reverb.setModulation (1.0f);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> in (kBlock), l (kBlock), r (kBlock);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < static_cast<int> (kSampleRate * 30.0 / kBlock); ++b)
    {
        for (auto& s : in)
            s = dist (rng);

        reverb.process (in.data(), l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            const float m = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            if (! std::isfinite (m))
                finite = false;
            peak = juce::jmax (peak, m);
        }
    }

    std::printf ("  peak after 30 s of full-scale noise: %.3f\n", peak);
    check (finite, "sustained noise produced NaN or Inf");
    check (peak < 8.0f, "network is not energy-stable (peak " + juce::String (peak, 2) + ")");
}

void testDecaySweepIsQuiet()
{
    std::printf ("Continuous decay sweep (click / discontinuity check):\n");

    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setModulation (0.5f);

    std::mt19937 rng (99);
    std::uniform_real_distribution<float> dist (-0.25f, 0.25f);

    std::vector<float> in (kBlock), l (kBlock), r (kBlock);
    float maxJump = 0.0f;
    float previous = 0.0f;
    bool finite = true;

    const int blocks = static_cast<int> (kSampleRate * 10.0 / kBlock);

    for (int b = 0; b < blocks; ++b)
    {
        // Sweep the knob end to end and back while audio is running.
        const float phase = static_cast<float> (b) / static_cast<float> (blocks);
        const float t = 1.0f - std::abs (2.0f * phase - 1.0f);
        reverb.setDecayTime (ee::dsp::FdnReverb::kMinDecay
                             + t * (ee::dsp::FdnReverb::kMaxDecay - ee::dsp::FdnReverb::kMinDecay));

        for (auto& s : in)
            s = dist (rng);

        reverb.process (in.data(), l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]))
                finite = false;
            maxJump = juce::jmax (maxJump, std::abs (l[i] - previous));
            previous = l[i];
        }
    }

    std::printf ("  largest sample-to-sample jump: %.4f\n", maxJump);
    check (finite, "decay sweep produced NaN or Inf");
    check (maxJump < 0.5f, "decay sweep produced a discontinuity (" + juce::String (maxJump, 3) + ")");
}

void testWetLevelConsistency()
{
    std::printf ("Wet RMS gain vs decay (should stay roughly flat):\n");

    double minGain = 1.0e9, maxGain = 0.0;

    for (const float decay : { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f })
    {
        ee::dsp::FdnReverb reverb;
        reverb.prepare (kSampleRate);
        reverb.reset();
        reverb.setDecayTime (decay);
        reverb.setModulation (0.25f);

        std::mt19937 rng (7);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> in (kBlock), l (kBlock), r (kBlock);

        double inSum = 0.0, outSum = 0.0;
        int counted = 0;
        const int settleBlocks = static_cast<int> (kSampleRate * 12.0 / kBlock);
        const int totalBlocks = settleBlocks + static_cast<int> (kSampleRate * 5.0 / kBlock);

        for (int b = 0; b < totalBlocks; ++b)
        {
            for (auto& s : in)
                s = dist (rng);

            reverb.process (in.data(), l.data(), r.data(), kBlock);

            if (b >= settleBlocks)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    inSum += static_cast<double> (in[i]) * in[i];
                    outSum += 0.5 * (static_cast<double> (l[i]) * l[i] + static_cast<double> (r[i]) * r[i]);
                    ++counted;
                }
            }
        }

        const double gain = std::sqrt (outSum / juce::jmax (1, counted)) / std::sqrt (inSum / juce::jmax (1, counted));
        minGain = juce::jmin (minGain, gain);
        maxGain = juce::jmax (maxGain, gain);

        std::printf ("  %5.1f s -> gain %.3f (%+.1f dB)\n", decay, gain, 20.0 * std::log10 (gain));
    }

    const double spreadDb = 20.0 * std::log10 (maxGain / juce::jmax (1.0e-9, minGain));
    std::printf ("  spread across the sweep: %.1f dB\n", spreadDb);

    check (spreadDb < 3.0,
           "wet level varies too much across the decay sweep (" + juce::String (spreadDb, 1) + " dB)");
}

void testSilenceInSilenceOut()
{
    std::printf ("Silence handling:\n");

    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setDecayTime (4.0f);
    reverb.setModulation (0.5f);

    std::vector<float> in (kBlock, 0.0f), l (kBlock), r (kBlock);
    float peak = 0.0f;

    for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
    {
        reverb.process (in.data(), l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
    }

    std::printf ("  peak from silent input: %.2e\n", peak);
    check (peak == 0.0f, "reverb generated signal from silence");
}
} // namespace

int main()
{
    std::printf ("=== Easy Effects DSP tests ===\n\n");

    testDecayAccuracy();
    std::printf ("\n");
    testStabilityUnderLoad();
    std::printf ("\n");
    testDecaySweepIsQuiet();
    std::printf ("\n");
    testWetLevelConsistency();
    std::printf ("\n");
    testSilenceInSilenceOut();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
