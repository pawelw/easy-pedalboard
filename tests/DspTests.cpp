#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/TapeCharacter.h"
#include "ee/dsp/TapeDelay.h"

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
    reverb.setResonance (1.0f - modulation);

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
    reverb.setResonance (0.0f);

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
    reverb.setResonance (0.5f);

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
        reverb.setResonance (0.75f);

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
    reverb.setResonance (0.5f);

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

/** Runs an impulse through the delay and reports where each channel taps. */
void testDelayTaps()
{
    std::printf ("Delay tap placement (clean path):\n");

    constexpr float leftSeconds = 0.25f;
    constexpr float rightSeconds = 0.4f;

    ee::dsp::TapeDelay delay;
    delay.prepare (kSampleRate);
    delay.setDelaySeconds (leftSeconds, rightSeconds);
    delay.snapDelays();
    delay.setFeedback (0.0f);
    delay.setModulation (0.0f);

    const int total = static_cast<int> (kSampleRate);
    std::vector<float> inL (total, 0.0f), inR (total, 0.0f);
    std::vector<float> outL (total), outR (total);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    delay.process (inL.data(), inR.data(), outL.data(), outR.data(), total);

    const auto peakIndex = [] (const std::vector<float>& v)
    {
        int best = 0;
        for (int i = 0; i < static_cast<int> (v.size()); ++i)
            if (std::abs (v[i]) > std::abs (v[best]))
                best = i;
        return best;
    };

    const int l = peakIndex (outL);
    const int r = peakIndex (outR);
    const int expectedL = static_cast<int> (leftSeconds * kSampleRate);
    const int expectedR = static_cast<int> (rightSeconds * kSampleRate);

    std::printf ("  left  %d (expected %d), amplitude %.4f\n", l, expectedL, outL[l]);
    std::printf ("  right %d (expected %d), amplitude %.4f\n", r, expectedR, outR[r]);

    check (std::abs (l - expectedL) <= 2, "left tap is not where the time knob says");
    check (std::abs (r - expectedR) <= 2, "right tap is not where the time knob says");

    // The whole point of the neutral setting: no filtering, no drive, no loss.
    check (std::abs (outL[l] - 1.0f) < 0.005f, "clean path is not unity gain");

    int repeats = 0;
    for (int i = expectedL + 10; i < total; ++i)
        if (std::abs (outL[i]) > 0.01f)
            ++repeats;

    check (repeats == 0, "zero feedback still produced more than one repeat");
}

void testDelayStability()
{
    std::printf ("Delay stability at maximum feedback and mod:\n");

    ee::dsp::TapeDelay delay;
    delay.prepare (kSampleRate);
    delay.setDelaySeconds (0.12f, 0.18f);
    delay.snapDelays();
    delay.setFeedback (1.0f);
    delay.setModulation (1.0f);

    std::mt19937 rng (0xd31a);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < static_cast<int> (kSampleRate * 20.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            inL[i] = dist (rng);
            inR[i] = dist (rng);
        }

        delay.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            finite = finite && std::isfinite (outL[i]) && std::isfinite (outR[i]);
            peak = juce::jmax (peak, std::abs (outL[i]), std::abs (outR[i]));
        }
    }

    std::printf ("  peak after 20 s of full-scale noise: %.3f\n", peak);
    check (finite, "delay produced a non-finite sample");
    check (peak < 4.0f, "delay ran away under sustained input");

    std::fill (inL.begin(), inL.end(), 0.0f);
    std::fill (inR.begin(), inR.end(), 0.0f);

    delay.reset();
    float silentPeak = 0.0f;

    for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
    {
        delay.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            silentPeak = juce::jmax (silentPeak, std::abs (outL[i]), std::abs (outR[i]));
    }

    std::printf ("  peak from silent input: %.2e\n", silentPeak);
    check (silentPeak == 0.0f, "delay generated signal from silence");
}

void testTapeCharacter()
{
    std::printf ("Tape stage:\n");

    const int total = static_cast<int> (kSampleRate * 2.0);
    std::mt19937 rng (0x7a9e);
    std::normal_distribution<float> dist (0.0f, 0.12f);

    std::vector<float> source (total);
    for (int i = 0; i < total; ++i)
        source[i] = std::tanh (dist (rng));

    // At zero the line still runs, so the latency never jumps when the knob
    // leaves the stop; the samples that come out must still be the originals.
    {
        ee::dsp::TapeCharacter tape;
        tape.prepare (kSampleRate);
        tape.setAmount (0.0f);

        std::vector<float> l (source), r (source);
        tape.process (l.data(), r.data(), total);

        const int latency = tape.getLatencySamples();
        float worst = 0.0f;
        for (int i = latency; i < total; ++i)
            worst = juce::jmax (worst, std::abs (l[i] - source[i - latency]));

        std::printf ("  latency %d samples; largest difference at 0 %%: %.2e\n", latency, worst);
        check (worst == 0.0f, "tape at 0 % is not bit exact");
    }

    {
        ee::dsp::TapeCharacter tape;
        tape.prepare (kSampleRate);
        tape.setAmount (1.0f);

        std::vector<float> l (source), r (source);
        tape.process (l.data(), r.data(), total);

        const auto rms = [] (const std::vector<float>& v, int from)
        {
            double sum = 0.0;
            for (int i = from; i < static_cast<int> (v.size()); ++i)
                sum += static_cast<double> (v[i]) * v[i];
            return std::sqrt (sum / (v.size() - from));
        };

        const double before = rms (source, 0);
        const double after = rms (l, 512);
        const double changeDb = 20.0 * std::log10 (after / before);

        bool finite = true;
        for (int i = 0; i < total; ++i)
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);

        // The reference machine came back +0.5 dB; a character control that
        // changes the level is a volume control in disguise.
        std::printf ("  level change at 100 %%: %+.2f dB\n", changeDb);
        check (finite, "tape produced a non-finite sample");
        check (std::abs (changeDb) < 1.5, "tape at 100 % moves the level too far");

        double sides = 0.0;
        for (int i = 512; i < total; ++i)
            sides += static_cast<double> (l[i] - r[i]) * (l[i] - r[i]);
        sides = std::sqrt (sides / (total - 512));

        // Flutter is shared between the channels, so the only difference the
        // two sides should show is the grit. If the wobble leaked in per
        // channel this would be far larger, and the effect would be a chorus.
        const double sidesDb = 20.0 * std::log10 (sides / before);
        std::printf ("  difference between channels: %.1f dB below the source\n", -sidesDb);
        check (sidesDb < -18.0, "tape decorrelates the channels too much");
    }

    {
        ee::dsp::TapeCharacter tape;
        tape.prepare (kSampleRate);
        tape.setAmount (1.0f);

        std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
        float peak = 0.0f;

        for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            tape.process (l.data(), r.data(), kBlock);

            for (int i = 0; i < kBlock; ++i)
                peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }

        std::printf ("  peak from silent input: %.2e\n", peak);
        check (peak == 0.0f, "tape hisses into silence instead of riding the signal");
    }
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
    std::printf ("\n");
    testDelayTaps();
    std::printf ("\n");
    testDelayStability();
    std::printf ("\n");
    testTapeCharacter();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
