#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ee/dsp/Chorus.h"
#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/Overdrive.h"
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

/** Fires a half-second 220 Hz tone into the reverb, then measures the tail once
    the dry note is long gone. `shimmer` is the knob amount. */
struct ShimmerRun
{
    float drivenPeak = 0.0f;
    float earlyTailRms = 0.0f;   // 1.5 - 2.5 s after the note: the bloom window
    float lateTailRms = 0.0f;    // 7 - 8 s in: has it settled?
    double octaveRatio = 0.0;    // energy at 440 Hz vs 220 Hz in the bloom window
    double correlation = 0.0;    // L/R across the bloom window
    bool finite = true;
};

ShimmerRun runShimmer (float shimmer)
{
    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setDecayTime (5.0f);
    reverb.setResonance (0.5f);
    reverb.setShimmer (shimmer);

    std::vector<float> in (kBlock), l (kBlock), r (kBlock);

    const double wIn = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSampleRate;
    const double wOct = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;

    const int noteBlocks  = static_cast<int> (kSampleRate * 0.5 / kBlock);
    const int totalBlocks = static_cast<int> (kSampleRate * 8.0 / kBlock);
    const auto inWindow = [] (int b, double lo, double hi)
    {
        const double t = b * kBlock / kSampleRate;
        return t >= lo && t < hi;
    };

    ShimmerRun out;
    double bloomLL = 0.0, bloomRR = 0.0, bloomLR = 0.0;
    double gFundR = 0.0, gFundI = 0.0, gOctR = 0.0, gOctI = 0.0;
    long long ph = 0, lateN = 0; double lateSum = 0.0;

    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const double t = (b * kBlock + i) / kSampleRate;
            in[static_cast<size_t> (i)] = b < noteBlocks
                ? 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t))
                : 0.0f;
        }

        reverb.process (in.data(), l.data(), r.data(), kBlock);

        const bool bloom = inWindow (b, 1.5, 2.5);
        const bool late  = inWindow (b, 7.0, 8.0);

        for (int i = 0; i < kBlock; ++i)
        {
            const float lv = l[static_cast<size_t> (i)];
            const float rv = r[static_cast<size_t> (i)];
            const float m = 0.5f * (lv + rv);
            out.finite = out.finite && std::isfinite (lv) && std::isfinite (rv);

            if (b < noteBlocks)
                out.drivenPeak = juce::jmax (out.drivenPeak, std::abs (lv), std::abs (rv));

            if (bloom)
            {
                bloomLL += lv * lv; bloomRR += rv * rv; bloomLR += lv * rv;
                gFundR += m * std::cos (wIn * ph);  gFundI += m * std::sin (wIn * ph);
                gOctR  += m * std::cos (wOct * ph); gOctI  += m * std::sin (wOct * ph);
                ++ph;
            }
            if (late) { lateSum += m * m; ++lateN; }
        }
    }

    out.earlyTailRms = static_cast<float> (std::sqrt (juce::jmax (1.0e-20, (bloomLL + bloomRR) / juce::jmax (1LL, ph * 2))));
    out.lateTailRms  = static_cast<float> (std::sqrt (juce::jmax (1.0e-20, lateSum / juce::jmax (1LL, lateN))));
    const double fund = std::sqrt (gFundR * gFundR + gFundI * gFundI);
    const double oct  = std::sqrt (gOctR * gOctR + gOctI * gOctI);
    out.octaveRatio = oct / juce::jmax (1.0e-9, fund);
    out.correlation = bloomLR / std::sqrt (juce::jmax (1.0e-12, bloomLL * bloomRR));
    return out;
}

/** Shimmer must stay bounded, settle after the note is gone, add a clear octave
    into the tail, and not narrow the image. */
void testShimmer()
{
    std::printf ("Shimmer (octave feedback):\n");

    const ShimmerRun off = runShimmer (0.0f);
    const ShimmerRun on  = runShimmer (1.0f);

    std::printf ("  off: bloom rms %.4e  octave/fundamental %.3f  corr %.3f\n",
                off.earlyTailRms, off.octaveRatio, off.correlation);
    std::printf ("  on : bloom rms %.4e  octave/fundamental %.3f  corr %.3f  peak %.3f  late rms %.2e\n",
                on.earlyTailRms, on.octaveRatio, on.correlation, on.drivenPeak, on.lateTailRms);

    check (on.finite, "shimmer produced a non-finite sample");
    check (on.drivenPeak < 4.0f, "shimmer feedback ran away");
    check (on.lateTailRms < 5.0e-4f, "shimmer tail did not settle after the note stopped");
    check (on.earlyTailRms > off.earlyTailRms * 1.5f, "shimmer did not sustain the tail");
    check (on.octaveRatio > off.octaveRatio * 3.0 && on.octaveRatio > 0.2,
           "shimmer did not add an audible octave to the tail");
    check (on.correlation < off.correlation + 0.05,
           "shimmer narrowed the stereo image instead of widening it");
}

/** Silence in still has to give exact silence out with shimmer fully up. */
void testShimmerSilence()
{
    std::printf ("Shimmer silence handling:\n");

    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setDecayTime (4.0f);
    reverb.setShimmer (1.0f);

    std::vector<float> in (kBlock, 0.0f), l (kBlock), r (kBlock);
    float peak = 0.0f;

    for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
    {
        reverb.process (in.data(), l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
    }

    std::printf ("  peak from silent input: %.2e\n", peak);
    check (peak == 0.0f, "shimmer generated signal from silence");
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

//==============================================================================
// Chorus

void testChorusSilence()
{
    std::printf ("Chorus: silence in -> silence out\n");

    ee::dsp::Chorus chorus;
    chorus.prepare (kSampleRate);
    chorus.reset();
    chorus.setRateHz (1.0f);
    chorus.setDepth01 (1.0f);
    chorus.setPhaseDegrees (90.0f);
    chorus.setMix01 (1.0f);

    std::vector<float> in (kBlock, 0.0f), l (kBlock, 0.0f), r (kBlock, 0.0f);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < 400; ++b)
    {
        chorus.process (in.data(), in.data(), l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                finite = false;
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    check (finite, "chorus output stays finite on silence");
    check (peak < 1.0e-6f, "chorus is silent on a silent input");
}

void testChorusBypassIsUnity()
{
    std::printf ("Chorus: Mix 0%% leaves the dry signal untouched\n");

    ee::dsp::Chorus chorus;
    chorus.prepare (kSampleRate);
    chorus.reset();
    chorus.setRateHz (2.0f);
    chorus.setDepth01 (0.7f);
    chorus.setPhaseDegrees (120.0f);
    chorus.setMix01 (0.0f);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
    double maxErr = 0.0;

    for (int b = 0; b < 60; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { inL[i] = dist (rng); inR[i] = dist (rng); }
        chorus.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            maxErr = juce::jmax (maxErr, (double) std::abs (outL[i] - inL[i]));
            maxErr = juce::jmax (maxErr, (double) std::abs (outR[i] - inR[i]));
        }
    }

    check (maxErr < 1.0e-6, "chorus dry path is transparent at Mix 0%");
}

/** Normalised L/R cross-correlation of the wet chorus output for a mono input,
    at a given Phase setting. 1 = mono, lower = wider. */
float chorusCorrelation (ee::dsp::Chorus& chorus, float phaseDeg)
{
    chorus.reset();
    chorus.setRateHz (0.8f);
    chorus.setDepth01 (0.6f);
    chorus.setPhaseDegrees (phaseDeg);
    chorus.setMix01 (1.0f);

    std::mt19937 rng (99);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    for (int b = 0; b < 20; ++b)   // warm up the delay lines
    {
        for (int i = 0; i < kBlock; ++i) { const float s = dist (rng); inL[i] = s; inR[i] = s; }
        chorus.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }

    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { const float s = dist (rng); inL[i] = s; inR[i] = s; }
        chorus.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            sumLR += (double) outL[i] * outR[i];
            sumLL += (double) outL[i] * outL[i];
            sumRR += (double) outR[i] * outR[i];
        }
    }

    const double denom = std::sqrt (sumLL * sumRR);
    return denom > 0.0 ? (float) (sumLR / denom) : 1.0f;
}

void testChorusWidensImage()
{
    std::printf ("Chorus: Phase knob decorrelates the stereo image\n");

    ee::dsp::Chorus chorus;
    chorus.prepare (kSampleRate);

    const float c90 = chorusCorrelation (chorus, 90.0f);
    const float c180 = chorusCorrelation (chorus, 180.0f);

    std::printf ("  L/R correlation: 90 deg = %.3f, 180 deg = %.3f\n", c90, c180);

    check (std::isfinite (c90) && std::isfinite (c180), "chorus correlation is finite");
    check (c90 < 0.95f, "chorus at Phase 90 deg is clearly wider than mono");
    check (c180 < c90 + 0.05f, "chorus widens further toward Phase 180 deg");
}

/** Short-time L/R behaviour of the pure wet output for a mono input: how mono
    it ever gets over an LFO cycle, and whether the stereo (side) energy ever
    briefly drops out. */
struct ChorusStereoStability
{
    float maxWindowCorrelation = 0.0f;   // 1 = momentarily collapsed to mono
    float minSideRmsRatio = 1.0f;        // min short-time side RMS / its mean
};

ChorusStereoStability chorusStereoStability (float phaseDeg, float depth01, float rateHz)
{
    ee::dsp::Chorus chorus;
    chorus.prepare (kSampleRate);
    chorus.reset();
    chorus.setRateHz (rateHz);
    chorus.setDepth01 (depth01);
    chorus.setPhaseDegrees (phaseDeg);
    chorus.setMix01 (1.0f);   // pure wet, so we measure the effect itself

    std::mt19937 rng (2024);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    for (int b = 0; b < static_cast<int> (kSampleRate * 0.5 / kBlock); ++b)  // warm up
    {
        for (int i = 0; i < kBlock; ++i) { const float s = dist (rng); inL[i] = s; inR[i] = s; }
        chorus.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }

    const int totalSamples = static_cast<int> (kSampleRate * 8.0);
    std::vector<float> wetL, wetR;
    wetL.reserve (static_cast<size_t> (totalSamples));
    wetR.reserve (static_cast<size_t> (totalSamples));
    while (static_cast<int> (wetL.size()) < totalSamples)
    {
        for (int i = 0; i < kBlock; ++i) { const float s = dist (rng); inL[i] = s; inR[i] = s; }
        chorus.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i) { wetL.push_back (outL[i]); wetR.push_back (outR[i]); }
    }

    const int windowLen = 4096;   // ~85 ms at 48 kHz
    const int hop = 1024;
    const int produced = static_cast<int> (wetL.size());

    ChorusStereoStability out;
    std::vector<float> sideRms;
    double sideRmsSum = 0.0;

    for (int start = 0; start + windowLen <= produced; start += hop)
    {
        double ll = 0.0, rr = 0.0, lr = 0.0, ss = 0.0;
        for (int i = 0; i < windowLen; ++i)
        {
            const float l = wetL[static_cast<size_t> (start + i)];
            const float r = wetR[static_cast<size_t> (start + i)];
            ll += (double) l * l;
            rr += (double) r * r;
            lr += (double) l * r;
            const float side = 0.5f * (l - r);
            ss += (double) side * side;
        }
        const double denom = std::sqrt (ll * rr);
        out.maxWindowCorrelation = juce::jmax (
            out.maxWindowCorrelation, denom > 0.0 ? (float) (lr / denom) : 1.0f);

        const float srms = (float) std::sqrt (ss / windowLen);
        sideRms.push_back (srms);
        sideRmsSum += srms;
    }

    const float meanSide =
        sideRms.empty() ? 0.0f : (float) (sideRmsSum / (double) sideRms.size());
    for (float s : sideRms)
        out.minSideRmsRatio =
            juce::jmin (out.minSideRmsRatio, meanSide > 0.0f ? s / meanSide : 1.0f);

    return out;
}

void testChorusStereoIsStable()
{
    std::printf ("Chorus: stereo image holds at every Phase setting\n");

    // The setting the dropout was reported at (Phase ~176, Depth ~32 %,
    // Rate ~0.5 Hz), plus the extremes of the knob.
    for (float phase : { 30.0f, 90.0f, 176.0f, 180.0f })
    {
        const auto st = chorusStereoStability (phase, 0.32f, 0.5f);
        std::printf ("  phase %5.1f deg: max short-time L/R corr %.3f, min side RMS ratio %.3f\n",
                     phase, st.maxWindowCorrelation, st.minSideRmsRatio);

        check (st.maxWindowCorrelation < 0.9f,
               "chorus stereo image never collapses toward mono");
        check (st.minSideRmsRatio > 0.35f,
               "chorus width never briefly drops out");
    }
}

//==============================================================================
// Overdrive

void testOverdriveSilence()
{
    std::printf ("Overdrive: silence in -> silence out\n");

    ee::dsp::Overdrive od;
    od.prepare (kSampleRate);
    od.reset();
    od.setDrive01 (1.0f);
    od.setTone01 (0.5f);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < 400; ++b)
    {
        od.process (l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                finite = false;
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    check (finite, "overdrive output stays finite on silence");
    check (peak < 1.0e-6f, "overdrive is silent on a silent input");
}

void testOverdriveStability()
{
    std::printf ("Overdrive: bounded and finite under a hot input at full drive\n");

    ee::dsp::Overdrive od;
    od.prepare (kSampleRate);
    od.reset();
    od.setDrive01 (1.0f);
    od.setTone01 (1.0f);

    std::mt19937 rng (4321);
    std::uniform_real_distribution<float> dist (-1.5f, 1.5f);   // deliberately over 0 dBFS

    std::vector<float> l (kBlock), r (kBlock);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < 2000; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { l[i] = dist (rng); r[i] = dist (rng); }
        od.process (l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                finite = false;
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    std::printf ("  output peak = %.3f\n", peak);
    check (finite, "overdrive output stays finite under load");
    check (peak < 4.0f, "overdrive output stays bounded under load");
}

/** Peak-to-RMS ratio (crest factor) of a steady sine put through the drive. A
    clean sine is ~1.41; the flatter the clipper squashes it, the closer to 1. */
float overdriveCrest (float drive01)
{
    ee::dsp::Overdrive od;
    od.prepare (kSampleRate);
    od.reset();
    od.setDrive01 (drive01);
    od.setTone01 (0.5f);

    const double freq = 220.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / kSampleRate;

    std::vector<float> buf (kBlock);
    double phase = 0.0;

    for (int b = 0; b < 40; ++b)   // let the filter states settle
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.2f * (float) std::sin (phase); phase += w; }
        od.process (buf.data(), nullptr, kBlock);
    }

    double sumSq = 0.0;
    float peak = 0.0f;
    int n = 0;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.2f * (float) std::sin (phase); phase += w; }
        od.process (buf.data(), nullptr, kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            sumSq += (double) buf[i] * buf[i];
            peak = juce::jmax (peak, std::abs (buf[i]));
            ++n;
        }
    }

    const float rms = (float) std::sqrt (sumSq / juce::jmax (1, n));
    return rms > 0.0f ? peak / rms : 0.0f;
}

void testOverdriveAddsHarmonics()
{
    std::printf ("Overdrive: more Drive flattens the wave (crest factor falls)\n");

    const float lowDrive = overdriveCrest (0.1f);
    const float highDrive = overdriveCrest (0.95f);

    std::printf ("  crest factor: Drive 10%% = %.3f, Drive 95%% = %.3f\n", lowDrive, highDrive);

    check (std::isfinite (lowDrive) && std::isfinite (highDrive), "overdrive crest factor is finite");
    check (highDrive < lowDrive - 0.05f, "raising Drive squashes the waveform");
    check (highDrive < 1.2f, "at full Drive the sine is heavily clipped");
}

/** RMS of the output above vs below ~1 kHz for a white-noise input, at a given
    Tone setting. A rising ratio means a brighter voicing. */
float overdriveHighLowRatio (float tone01)
{
    ee::dsp::Overdrive od;
    od.prepare (kSampleRate);
    od.reset();
    od.setDrive01 (0.3f);
    od.setTone01 (tone01);

    // One-pole split at 1 kHz to score the balance.
    const float splitCoeff =
        1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 1000.0f / (float) kSampleRate);

    std::mt19937 rng (777);
    std::uniform_real_distribution<float> dist (-0.3f, 0.3f);
    std::vector<float> buf (kBlock);

    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < kBlock; ++i) buf[i] = dist (rng);
        od.process (buf.data(), nullptr, kBlock);
    }

    float lp = 0.0f;
    double lowSq = 0.0, highSq = 0.0;
    int n = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < kBlock; ++i) buf[i] = dist (rng);
        od.process (buf.data(), nullptr, kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            lp += splitCoeff * (buf[i] - lp);
            const float high = buf[i] - lp;
            lowSq += (double) lp * lp;
            highSq += (double) high * high;
            ++n;
        }
    }

    const float lowRms = (float) std::sqrt (lowSq / juce::jmax (1, n));
    const float highRms = (float) std::sqrt (highSq / juce::jmax (1, n));
    return lowRms > 0.0f ? highRms / lowRms : 0.0f;
}

void testOverdriveToneTilt()
{
    std::printf ("Overdrive: Tone tilts the balance from dark to bright\n");

    const float dark = overdriveHighLowRatio (0.0f);
    const float bright = overdriveHighLowRatio (1.0f);

    std::printf ("  high/low ratio: Tone 0%% = %.3f, Tone 100%% = %.3f\n", dark, bright);

    check (std::isfinite (dark) && std::isfinite (bright), "overdrive tone ratio is finite");
    check (bright > dark * 1.5f, "Tone up makes the pedal clearly brighter");
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
    testShimmer();
    std::printf ("\n");
    testShimmerSilence();
    std::printf ("\n");
    testDelayTaps();
    std::printf ("\n");
    testDelayStability();
    std::printf ("\n");
    testTapeCharacter();
    std::printf ("\n");
    testChorusSilence();
    std::printf ("\n");
    testChorusBypassIsUnity();
    std::printf ("\n");
    testChorusWidensImage();
    std::printf ("\n");
    testChorusStereoIsStable();
    std::printf ("\n");
    testOverdriveSilence();
    std::printf ("\n");
    testOverdriveStability();
    std::printf ("\n");
    testOverdriveAddsHarmonics();
    std::printf ("\n");
    testOverdriveToneTilt();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
