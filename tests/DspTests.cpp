#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

#include "ee/dsp/AutoWah.h"
#include "ee/dsp/Chorus.h"
#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/Phaser.h"
#include "ee/dsp/SpringReverb.h"
#include "ee/dsp/Overdrive.h"
#include "ee/dsp/TapeCharacter.h"
#include "ee/dsp/TapeDelay.h"
#include "ee/dsp/TapeMachine.h"

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
// Tape machine (Peak Tape)

/** Puts every control at its resting position - which is what the pedal is at
    Saturation, Wear, Flutter and Noise 0, Tone centred and Stereo off. */
void restTapeMachine (ee::dsp::TapeMachine& machine)
{
    machine.setWear01 (0.0f);
    machine.setFlutter01 (0.0f);
    machine.setTone (0.0f);
    machine.setStereo01 (0.0f);
    machine.setNoise01 (0.0f);
    machine.setSaturation01 (0.0f);
}

void testTapeMachineAtRest()
{
    std::printf ("Tape machine: every control at rest is bit-exact pass-through\n");

    const int total = static_cast<int> (kSampleRate * 2.0);
    std::mt19937 rng (0x7a9e);
    std::normal_distribution<float> dist (0.0f, 0.12f);

    std::vector<float> source (total);
    for (int i = 0; i < total; ++i)
        source[i] = std::tanh (dist (rng));

    ee::dsp::TapeMachine machine;
    machine.prepare (kSampleRate);
    machine.reset();
    restTapeMachine (machine);

    std::vector<float> l (source), r (source);
    machine.process (l.data(), r.data(), total);

    const int latency = machine.getLatencySamples();
    float worst = 0.0f;
    for (int i = latency; i < total; ++i)
        worst = juce::jmax (worst, std::abs (l[i] - source[i - latency]));

    std::printf ("  latency %d samples (%.2f ms); largest difference: %.2e\n",
                 latency, 1000.0 * latency / kSampleRate, worst);
    check (latency > 0, "tape machine reports its transport latency");
    check (worst == 0.0f, "tape machine at rest is not bit exact");
}

void testTapeMachineSilence()
{
    std::printf ("Tape machine: with Noise down, silence in -> silence out\n");

    ee::dsp::TapeMachine machine;
    machine.prepare (kSampleRate);
    machine.reset();
    machine.setWear01 (1.0f);
    machine.setFlutter01 (1.0f);
    machine.setTone (1.0f);
    machine.setStereo01 (1.0f);
    machine.setNoise01 (0.0f);
    machine.setSaturation01 (1.0f);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    float peak = 0.0f;

    for (int b = 0; b < static_cast<int> (kSampleRate * 3.0 / kBlock); ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        machine.process (l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
    }

    std::printf ("  peak from silent input: %.2e\n", peak);
    check (peak == 0.0f, "tape machine makes noise with the Noise knob down");
}

void testTapeMachineNoiseIsConstant()
{
    std::printf ("Tape machine: the tape floor is constant, playing or not\n");

    const int total = static_cast<int> (kSampleRate * 4.0);

    // Everything but Noise at rest, so the machine is bit-exact apart from the
    // floor - which makes the floor exactly measurable as output minus input.
    const auto run = [total] (bool playing, double& noiseRms)
    {
        ee::dsp::TapeMachine machine;
        machine.prepare (kSampleRate);
        machine.reset();
        restTapeMachine (machine);
        machine.setNoise01 (1.0f);

        std::vector<float> source (total, 0.0f);
        if (playing)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSampleRate;
            for (int i = 0; i < total; ++i)
                source[i] = 0.3f * (float) std::sin (w * i);
        }

        std::vector<float> l (source), r (source);
        machine.process (l.data(), r.data(), total);

        const int latency = machine.getLatencySamples();
        double sumSq = 0.0;
        int n = 0;

        for (int i = latency + 2048; i < total; ++i)
        {
            const double residual = (double) l[i] - source[i - latency];
            sumSq += residual * residual;
            ++n;
        }

        noiseRms = std::sqrt (sumSq / juce::jmax (1, n));
    };

    double quiet = 0.0, loud = 0.0;
    run (false, quiet);
    run (true, loud);

    const double differenceDb = 20.0 * std::log10 (loud / juce::jmax (quiet, 1.0e-30));

    std::printf ("  floor with nothing playing: %.2e (%.1f dBFS)\n",
                 quiet, 20.0 * std::log10 (juce::jmax (quiet, 1.0e-30)));
    std::printf ("  floor under a 220 Hz note:  %.2e (%+.2f dB)\n", loud, differenceDb);

    check (quiet > 1.0e-4, "the tape floor is audible at Noise 100 %");
    check (std::abs (differenceDb) < 0.5, "the tape floor ducks or swells with the programme");
}

void testTapeMachineNoiseLoop()
{
    std::printf ("Tape machine: the tape floor recording loops without a seam\n");

    // A stand-in for the pedal's embedded floor: two seconds of band-limited
    // noise, so the loop wraps twice inside the run.
    const int tableLength = static_cast<int> (kSampleRate * 2.0);
    std::vector<float> table (static_cast<size_t> (tableLength));
    {
        std::mt19937 rng (0x10ad);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        float lp = 0.0f;
        for (int i = 0; i < tableLength; ++i)
        {
            lp += 0.25f * (dist (rng) - lp);
            table[static_cast<size_t> (i)] = lp * 0.05f;
        }
    }

    const float* channelPointers[1] = { table.data() };

    ee::dsp::TapeMachine machine;
    machine.prepare (kSampleRate);
    machine.reset();
    restTapeMachine (machine);
    machine.setNoiseSample (channelPointers, 1, tableLength, kSampleRate);
    machine.setNoise01 (1.0f);

    const int total = static_cast<int> (kSampleRate * 6.0);
    std::vector<float> out (static_cast<size_t> (total), 0.0f);

    for (int offset = 0; offset < total; offset += kBlock)
        machine.process (out.data() + offset, nullptr, juce::jmin (kBlock, total - offset));

    // The biggest step in the output must not exceed what the recording itself
    // does - a seam would show up as a click far above it.
    float tableStep = 0.0f;
    for (int i = 1; i < tableLength; ++i)
        tableStep = juce::jmax (tableStep, std::abs (table[static_cast<size_t> (i)]
                                                     - table[static_cast<size_t> (i - 1)]));

    float outStep = 0.0f;
    for (int i = 1; i < total; ++i)
        outStep = juce::jmax (outStep, std::abs (out[static_cast<size_t> (i)]
                                                 - out[static_cast<size_t> (i - 1)]));

    // Level per second: a loop that restarts or runs dry would show here.
    double quietest = 1.0e30, loudest = 0.0;
    const int perSecond = static_cast<int> (kSampleRate);
    for (int s = perSecond; s + perSecond <= total; s += perSecond)
    {
        double sumSq = 0.0;
        for (int i = 0; i < perSecond; ++i)
            sumSq += (double) out[static_cast<size_t> (s + i)] * out[static_cast<size_t> (s + i)];
        const double rms = std::sqrt (sumSq / perSecond);
        quietest = juce::jmin (quietest, rms);
        loudest = juce::jmax (loudest, rms);
    }

    std::printf ("  largest step: recording %.5f, looped output %.5f\n", tableStep, outStep);
    std::printf ("  level across the run: %.5f .. %.5f (%.2f dB spread)\n",
                 quietest, loudest, 20.0 * std::log10 (loudest / juce::jmax (quietest, 1.0e-30)));

    check (outStep <= tableStep * 1.2f, "the loop seam clicks");
    check (quietest > 0.0, "the tape floor runs out instead of looping");
    check (20.0 * std::log10 (loudest / juce::jmax (quietest, 1.0e-30)) < 1.0,
           "the looped floor is not a steady level");

    // And with the knob down it is still silent, recording or no recording.
    machine.setNoise01 (0.0f);
    std::vector<float> quiet (static_cast<size_t> (total), 0.0f);
    for (int offset = 0; offset < total; offset += kBlock)
        machine.process (quiet.data() + offset, nullptr, juce::jmin (kBlock, total - offset));

    float peak = 0.0f;
    for (int i = total / 2; i < total; ++i)
        peak = juce::jmax (peak, std::abs (quiet[static_cast<size_t> (i)]));

    std::printf ("  peak with the knob down: %.2e\n", peak);
    check (peak == 0.0f, "the floor still plays with the Noise knob down");
}

/** Peak-to-RMS of a steady sine through the machine at one Saturation setting,
    plus how far the level moved. A falling crest factor means the head really
    is flattening the wave rather than just turning it down. */
void tapeSaturationScore (float saturation, float& crest, double& levelDb)
{
    ee::dsp::TapeMachine machine;
    machine.prepare (kSampleRate);
    machine.reset();
    restTapeMachine (machine);
    machine.setSaturation01 (saturation);

    const double freq = 220.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / kSampleRate;
    const float amplitude = 0.2f;

    std::vector<float> buf (kBlock);
    double phase = 0.0;

    for (int b = 0; b < 60; ++b)   // settle the smoothing and the filter states
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = amplitude * (float) std::sin (phase); phase += w; }
        machine.process (buf.data(), nullptr, kBlock);
    }

    double sumSq = 0.0;
    float peak = 0.0f;
    int n = 0;

    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = amplitude * (float) std::sin (phase); phase += w; }
        machine.process (buf.data(), nullptr, kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            sumSq += (double) buf[i] * buf[i];
            peak = juce::jmax (peak, std::abs (buf[i]));
            ++n;
        }
    }

    const double rms = std::sqrt (sumSq / juce::jmax (1, n));
    crest = rms > 0.0 ? static_cast<float> (peak / rms) : 0.0f;
    levelDb = 20.0 * std::log10 (rms / (amplitude / std::sqrt (2.0)));
}

void testTapeMachineSaturation()
{
    std::printf ("Tape machine: Saturation squashes the wave without moving the level\n");

    float lowCrest = 0.0f, highCrest = 0.0f;
    double lowLevel = 0.0, highLevel = 0.0;

    tapeSaturationScore (0.1f, lowCrest, lowLevel);
    tapeSaturationScore (1.0f, highCrest, highLevel);

    std::printf ("  crest factor: 10 %% = %.3f, 100 %% = %.3f\n", lowCrest, highCrest);
    std::printf ("  level change: 10 %% = %+.2f dB, 100 %% = %+.2f dB\n", lowLevel, highLevel);

    check (std::isfinite (lowCrest) && std::isfinite (highCrest), "tape crest factor is finite");
    check (highCrest < lowCrest - 0.02f, "more Saturation flattens the waveform");

    // The make-up is matched at a reference level for exactly this reason: a
    // character control that changes the level is a volume control in disguise.
    check (std::abs (highLevel) < 2.0, "Saturation at 100 % moves the level too far");
}

/** Output RMS for a steady sine at `freq`, at one Tone setting. Two probes well
    either side of the pivot score the tilt without a one-pole band split - on
    white noise that split leaks so much high band into its "low" bucket that a
    real tilt barely shows. */
double tapeToneResponse (float tone, double freq)
{
    ee::dsp::TapeMachine machine;
    machine.prepare (kSampleRate);
    machine.reset();
    restTapeMachine (machine);
    machine.setTone (tone);

    const double w = 2.0 * juce::MathConstants<double>::pi * freq / kSampleRate;
    std::vector<float> buf (kBlock);
    double phase = 0.0;

    for (int b = 0; b < 120; ++b)   // settle the smoothing and the filter states
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.25f * (float) std::sin (phase); phase += w; }
        machine.process (buf.data(), nullptr, kBlock);
    }

    double sumSq = 0.0;
    int n = 0;

    for (int b = 0; b < 100; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.25f * (float) std::sin (phase); phase += w; }
        machine.process (buf.data(), nullptr, kBlock);

        for (int i = 0; i < kBlock; ++i) { sumSq += (double) buf[i] * buf[i]; ++n; }
    }

    return std::sqrt (sumSq / juce::jmax (1, n));
}

void testTapeMachineToneTilt()
{
    std::printf ("Tape machine: Tone tilts dark to bright either side of centre\n");

    const auto ratio = [] (float tone)
    {
        return tapeToneResponse (tone, 6000.0) / tapeToneResponse (tone, 200.0);
    };

    const double dark = ratio (-1.0f);
    const double flat = ratio (0.0f);
    const double bright = ratio (1.0f);

    std::printf ("  6 kHz / 200 Hz: dark = %.3f (%+.1f dB), centre = %.3f, bright = %.3f (%+.1f dB)\n",
                 dark, 20.0 * std::log10 (dark / flat), flat,
                 bright, 20.0 * std::log10 (bright / flat));

    check (dark < flat * 0.5, "turning Tone down does not darken the balance");
    check (bright > flat * 2.0, "turning Tone up does not brighten the balance");

    // A centre-detented knob has to lean as far one way as the other.
    const double downDb = -20.0 * std::log10 (dark / flat);
    const double upDb = 20.0 * std::log10 (bright / flat);
    check (std::abs (downDb - upDb) < 2.0, "Tone is lopsided about its centre");
}

void testTapeMachineStereoWidens()
{
    std::printf ("Tape machine: the Stereo switch opens the image\n");

    const auto sideEnergy = [] (bool stereo)
    {
        ee::dsp::TapeMachine machine;
        machine.prepare (kSampleRate);
        machine.reset();
        restTapeMachine (machine);
        machine.setStereo01 (stereo ? 1.0f : 0.0f);

        // A tone, not noise: white noise decorrelates at any offset at all, so
        // it would score a hair of delay the same as a wide image.
        const double w = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSampleRate;
        double phase = 0.0;

        std::vector<float> l (kBlock), r (kBlock);
        double side = 0.0, mid = 0.0;

        for (int b = 0; b < 600; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = 0.3f * (float) std::sin (phase);   // mono, fanned to both sides
                phase += w;
                l[i] = s;
                r[i] = s;
            }

            machine.process (l.data(), r.data(), kBlock);

            if (b < 100)   // let the width modulation get moving
                continue;

            for (int i = 0; i < kBlock; ++i)
            {
                const double d = (double) l[i] - r[i];
                const double m = (double) l[i] + r[i];
                side += d * d;
                mid += m * m;
            }
        }

        return 10.0 * std::log10 (juce::jmax (side, 1.0e-30) / juce::jmax (mid, 1.0e-30));
    };

    const double off = sideEnergy (false);
    const double on = sideEnergy (true);

    std::printf ("  side vs mid on a mono source: off = %.1f dB, on = %.1f dB\n", off, on);

    // Off, both channels share one transport, so a mono source stays exactly
    // mono - the difference signal is not small, it is nothing at all.
    check (off < -200.0, "the two channels are not identical with Stereo off");
    check (on > off + 40.0, "the Stereo switch does not widen a mono source");
}

void testTapeMachineStability()
{
    std::printf ("Tape machine: recovers from non-finite input\n");

    ee::dsp::TapeMachine machine;
    machine.prepare (kSampleRate);
    machine.reset();
    machine.setWear01 (1.0f);
    machine.setFlutter01 (1.0f);
    machine.setTone (0.7f);
    machine.setStereo01 (1.0f);
    machine.setNoise01 (0.8f);
    machine.setSaturation01 (1.0f);

    std::vector<float> l (kBlock), r (kBlock);
    double phase = 0.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * 330.0 / kSampleRate;

    bool finite = true;
    float peak = 0.0f;

    for (int b = 0; b < 300; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            float s = 0.6f * (float) std::sin (phase);
            phase += w;

            // A burst of garbage early on, then a lone NaN once it is running.
            if (b == 2)
                s = (i % 2 == 0) ? std::numeric_limits<float>::quiet_NaN()
                                 : std::numeric_limits<float>::infinity();
            else if (b == 150 && i == 3)
                s = std::numeric_limits<float>::quiet_NaN();

            l[i] = s;
            r[i] = s;
        }

        machine.process (l.data(), r.data(), kBlock);

        if (b < 6 || (b >= 150 && b < 153))   // the blocks the garbage lands in
            continue;

        for (int i = 0; i < kBlock; ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    std::printf ("  peak after the garbage: %.3f\n", peak);
    check (finite, "tape machine keeps producing non-finite samples after a NaN");
    check (peak < 4.0f, "tape machine runs away after a NaN");
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

    std::printf ("  peak from silent input: %.3e\n", peak);

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
// Phaser

void testPhaserSilence()
{
    std::printf ("Phaser: silence in -> silence out\n");

    ee::dsp::Phaser phaser;
    phaser.prepare (kSampleRate);
    phaser.reset();
    phaser.setRateHz (1.0f);
    phaser.setDepth01 (1.0f);

    std::vector<float> in (kBlock, 0.0f), l (kBlock, 0.0f), r (kBlock, 0.0f);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < 400; ++b)
    {
        phaser.process (in.data(), in.data(), l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                finite = false;
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    check (finite, "phaser output stays finite on silence");
    check (peak < 1.0e-6f, "phaser is silent on a silent input");
}

void testPhaserStability()
{
    std::printf ("Phaser: bounded and finite under a hot input at full depth\n");

    ee::dsp::Phaser phaser;
    phaser.prepare (kSampleRate);
    phaser.reset();
    phaser.setRateHz (ee::dsp::phaser::kRateMaxHz);
    phaser.setDepth01 (1.0f);

    std::mt19937 rng (9182);
    std::uniform_real_distribution<float> dist (-1.5f, 1.5f);   // deliberately over 0 dBFS

    std::vector<float> l (kBlock), r (kBlock);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < 3000; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { l[i] = dist (rng); r[i] = dist (rng); }
        phaser.process (l.data(), r.data(), l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                finite = false;
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    std::printf ("  output peak = %.3f\n", peak);
    check (finite, "phaser output stays finite under load");
    check (peak < 4.0f, "phaser output stays bounded under load");
}

/** Peak-to-trough spread of the block RMS envelope over one full LFO period,
    for a steady 600 Hz sine. The moving notch sweeps past the tone once per
    cycle, so a deeper sweep drags the level through a wider range. */
float phaserEnvelopeSpread (float depth01)
{
    constexpr float rateHz = 0.5f;   // 2 s period

    ee::dsp::Phaser phaser;
    phaser.prepare (kSampleRate);
    phaser.reset();
    phaser.setRateHz (rateHz);
    phaser.setDepth01 (depth01);

    const double freq = 600.0;   // sits inside the sweep band
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / kSampleRate;
    double phase = 0.0;

    std::vector<float> buf (kBlock);

    // Six near-unity all-pass sections wrapped in a feedback ring settle slowly,
    // so give them a generous run before measuring.
    for (int b = 0; b < 800; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.25f * (float) std::sin (phase); phase += w; }
        phaser.process (buf.data(), nullptr, buf.data(), buf.data(), kBlock);
    }

    const int periodBlocks = juce::roundToInt (kSampleRate / (rateHz * kBlock));
    float loRms = 1.0e9f, hiRms = 0.0f;
    for (int b = 0; b < 2 * periodBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.25f * (float) std::sin (phase); phase += w; }
        phaser.process (buf.data(), nullptr, buf.data(), buf.data(), kBlock);

        double sumSq = 0.0;
        for (int i = 0; i < kBlock; ++i) sumSq += (double) buf[i] * buf[i];
        const float rms = (float) std::sqrt (sumSq / kBlock);
        loRms = juce::jmin (loRms, rms);
        hiRms = juce::jmax (hiRms, rms);
    }

    return hiRms - loRms;
}

void testPhaserSweeps()
{
    std::printf ("Phaser: the notch sweeps past the tone, and Depth widens it\n");

    const float narrow = phaserEnvelopeSpread (0.1f);   // small sweep
    const float wide = phaserEnvelopeSpread (1.0f);      // full sweep

    std::printf ("  envelope spread: narrow = %.4f, wide = %.4f\n", narrow, wide);

    check (std::isfinite (narrow) && std::isfinite (wide), "phaser envelope spread is finite");
    check (wide > 0.03f, "a running sweep makes the level breathe");
    check (wide > narrow + 0.01f, "more Depth widens the sweep");
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

/** Goertzel power (magnitude squared, unnormalised) at one frequency. */
double goertzelPower (const std::vector<float>& x, double freq, double fs)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / fs;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x)
    {
        const double s0 = (double) v + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

void testOverdriveAntiAliasing()
{
    std::printf ("Overdrive: the oversampler keeps clipping aliases out of the band\n");

    // A 7 kHz tone hammered by the clipper would, without oversampling, fold
    // strong difference tones down to 1 kHz / 6 kHz / 13 kHz. With the 2x
    // oversampled clip stage those should stay far below the harmonics.
    ee::dsp::Overdrive od;
    od.prepare (kSampleRate);
    od.reset();
    od.setDrive01 (0.9f);
    od.setTone01 (0.5f);

    const double f0 = 7000.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * f0 / kSampleRate;

    std::vector<float> buf (kBlock);
    double phase = 0.0;
    for (int b = 0; b < 40; ++b)   // settle the filters
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.3f * (float) std::sin (phase); phase += w; }
        od.process (buf.data(), nullptr, kBlock);
    }

    std::vector<float> out;
    out.reserve (1 << 16);
    while ((int) out.size() < (1 << 16))
    {
        for (int i = 0; i < kBlock; ++i) { buf[i] = 0.3f * (float) std::sin (phase); phase += w; }
        od.process (buf.data(), nullptr, kBlock);
        for (int i = 0; i < kBlock; ++i) out.push_back (buf[i]);
    }

    const double fund = goertzelPower (out, 7000.0, kSampleRate);
    const double aliasLow  = goertzelPower (out, 1000.0, kSampleRate);
    const double aliasMid  = goertzelPower (out, 6000.0, kSampleRate);
    const double aliasHigh = goertzelPower (out, 13000.0, kSampleRate);
    const double worstAlias = juce::jmax (aliasLow, aliasMid, aliasHigh);

    const double aliasDb = 10.0 * std::log10 (worstAlias / juce::jmax (fund, 1.0e-30));
    std::printf ("  worst fold-down tone is %.1f dB below the 7 kHz fundamental\n", aliasDb);

    check (std::isfinite (aliasDb), "overdrive alias level is finite");
    check (aliasDb < -30.0, "overdrive clipping aliases stay >30 dB down");
}

//==============================================================================
// Auto-wah (LFO-driven modulated filter)

namespace autowah_test
{
    // A steady probe tone through the engine; returns the out/in power at the
    // probe frequency in each `hopMs` window after `settleBlocks`.
    struct Rig
    {
        ee::dsp::AutoWah wah;
        double probeHz = 1000.0;
        float inAmp = 0.35f;
        double phase = 0.0;

        void prep (float period, float range, int type,
                   bool stereoOn, float decay01, float shape01 = 0.5f)
        {
            wah.prepare (kSampleRate);
            wah.reset();
            wah.setPeriodSeconds (period);
            wah.setFreq01 (0.35f);
            wah.setQ01 (0.55f);
            wah.setMix01 (1.0f);
            wah.setRange01 (range);
            wah.setType (type);
            wah.setStereo (stereoOn);
            wah.setDecay01 (decay01);
            wah.setShape01 (shape01);
        }

        // Fills L and R (R may be null) with the probe tone for `n` samples.
        void fill (float* l, float* r, int n)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * probeHz / kSampleRate;
            for (int i = 0; i < n; ++i)
            {
                const float s = inAmp * (float) std::sin (phase);
                phase += w;
                l[i] = s;
                if (r != nullptr) r[i] = s;
            }
        }
    };

    // Per-hop out/in power at the probe, one channel, after settling.
    std::vector<double> hopRatios (Rig& rig, int settleBlocks, int hops, double hopMs)
    {
        const int hop = (int) (kSampleRate * hopMs * 0.001);
        std::vector<float> buf ((size_t) hop);

        for (int b = 0; b < settleBlocks; ++b)
        {
            rig.fill (buf.data(), nullptr, hop);
            rig.wah.process (buf.data(), nullptr, hop);
        }

        std::vector<double> out;
        for (int h = 0; h < hops; ++h)
        {
            std::vector<float> in ((size_t) hop);
            rig.fill (in.data(), nullptr, hop);
            for (int i = 0; i < hop; ++i) buf[(size_t) i] = in[(size_t) i];
            rig.wah.process (buf.data(), nullptr, hop);

            const double so = goertzelPower (std::vector<float> (buf.begin(), buf.end()),
                                             rig.probeHz, kSampleRate);
            const double si = goertzelPower (in, rig.probeHz, kSampleRate);
            out.push_back (so / juce::jmax (si, 1.0e-30));
        }
        return out;
    }

    double spread (const std::vector<double>& v)   // max / min
    {
        double lo = 1.0e30, hi = 0.0;
        for (double x : v) { lo = juce::jmin (lo, x); hi = juce::jmax (hi, x); }
        return hi / juce::jmax (lo, 1.0e-30);
    }
}

void testAutoWahSilence()
{
    std::printf ("Auto-wah: silence in -> silence out\n");

    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.2f);
    wah.setRange01 (1.0f);
    wah.setMix01 (1.0f);
    wah.setDecay01 (1.0f);   // LFO latched on
    wah.setQ01 (0.7f);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { l[i] = 0.0f; r[i] = 0.0f; }
        wah.process (l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) finite = false;
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    check (finite, "auto-wah output stays finite on silence");
    check (peak < 1.0e-6f, "auto-wah is silent on a silent input");
}

void testAutoWahMixZeroIsDry()
{
    std::printf ("Auto-wah: Mix 0 leaves the dry signal untouched\n");

    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.15f);
    wah.setRange01 (1.0f);
    wah.setDecay01 (1.0f);
    wah.setMix01 (0.0f);
    wah.setQ01 (0.6f);

    const double w = 2.0 * juce::MathConstants<double>::pi * 300.0 / kSampleRate;
    double phase = 0.0;
    float maxDiff = 0.0f;
    bool finite = true;

    std::vector<float> buf (kBlock), dry (kBlock);
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const float s = 0.5f * (float) std::sin (phase);
            phase += w;
            buf[i] = s;
            dry[i] = s;
        }
        wah.process (buf.data(), nullptr, kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (buf[i])) finite = false;
            maxDiff = juce::jmax (maxDiff, std::abs (buf[i] - dry[i]));
        }
    }

    std::printf ("  largest deviation from dry = %.2e\n", (double) maxDiff);
    check (finite, "auto-wah output stays finite at Mix 0");
    check (maxDiff < 1.0e-6f, "Mix 0 is bit-exact dry");
}

void testAutoWahStability()
{
    std::printf ("Auto-wah: bounded and finite under a hot input at extreme settings\n");

    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.03f);   // fastest LFO
    wah.setRange01 (1.0f);
    wah.setQ01 (1.0f);
    wah.setMix01 (1.0f);
    wah.setDecay01 (1.0f);
    wah.setStereo (true);

    std::mt19937 rng (4471);
    std::uniform_real_distribution<float> dist (-1.5f, 1.5f);

    std::vector<float> l (kBlock), r (kBlock);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < 3000; ++b)
    {
        wah.setType (b % 3);                              // sweep LP / BP / HP
        wah.setFreq01 (0.5f + 0.5f * std::sin ((float) b * 0.05f));

        for (int i = 0; i < kBlock; ++i) { l[i] = dist (rng); r[i] = dist (rng); }
        wah.process (l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) finite = false;
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    std::printf ("  output peak = %.3f\n", peak);
    check (finite, "auto-wah output stays finite under load");
    check (peak < 4.0f, "auto-wah output stays bounded under load");
}

void testAutoWahLfoModulates()
{
    std::printf ("Auto-wah: the LFO sweeps the filter, and Amount 0 leaves it still\n");

    autowah_test::Rig moving;
    moving.prep (0.25f, 0.85f, /*BP*/ 1, false, /*Decay latched*/ 1.0f, 0.5f);
    const auto mv = autowah_test::hopRatios (moving, 200, 60, 8.0);

    autowah_test::Rig still;
    still.prep (0.25f, 0.0f, 1, false, 1.0f, 0.5f);
    const auto st = autowah_test::hopRatios (still, 200, 60, 8.0);

    const double movSpread = autowah_test::spread (mv);
    const double stillSpread = autowah_test::spread (st);
    std::printf ("  probe spread: moving = %.2f, Amount 0 = %.2f\n", movSpread, stillSpread);

    check (std::isfinite (movSpread) && std::isfinite (stillSpread), "auto-wah LFO spreads are finite");
    check (movSpread > 3.0, "the LFO swings the probe through the filter");
    check (stillSpread < 1.2, "Amount 0 holds the filter still");
}

void testAutoWahStereoOpposes()
{
    std::printf ("Auto-wah: Stereo runs the two channels' LFOs in opposition\n");

    auto lrCorr = [] (bool stereoOn)
    {
        autowah_test::Rig rig;
        rig.prep (0.3f, 0.85f, 1, stereoOn, 1.0f, 0.5f);

        const int hop = (int) (kSampleRate * 0.008);
        std::vector<float> l ((size_t) hop), r ((size_t) hop);

        for (int b = 0; b < 200; ++b) { rig.fill (l.data(), r.data(), hop); rig.wah.process (l.data(), r.data(), hop); }

        std::vector<double> el, er;
        for (int h = 0; h < 90; ++h)
        {
            std::vector<float> inl ((size_t) hop);
            rig.fill (inl.data(), nullptr, hop);
            for (int i = 0; i < hop; ++i) { l[(size_t) i] = inl[(size_t) i]; r[(size_t) i] = inl[(size_t) i]; }
            rig.wah.process (l.data(), r.data(), hop);
            const double si = juce::jmax (goertzelPower (inl, rig.probeHz, kSampleRate), 1.0e-30);
            el.push_back (goertzelPower (std::vector<float> (l.begin(), l.end()), rig.probeHz, kSampleRate) / si);
            er.push_back (goertzelPower (std::vector<float> (r.begin(), r.end()), rig.probeHz, kSampleRate) / si);
        }

        double ml = 0.0, mr = 0.0;
        for (size_t i = 0; i < el.size(); ++i) { ml += el[i]; mr += er[i]; }
        ml /= (double) el.size(); mr /= (double) er.size();
        double num = 0.0, dl = 0.0, dr = 0.0;
        for (size_t i = 0; i < el.size(); ++i)
        {
            const double a = el[i] - ml, b = er[i] - mr;
            num += a * b; dl += a * a; dr += b * b;
        }
        return num / std::sqrt (juce::jmax (dl * dr, 1.0e-30));
    };

    const double wide = lrCorr (true);
    const double mono = lrCorr (false);
    std::printf ("  L/R probe-envelope correlation: Stereo = %.2f, Mono = %.2f\n", wide, mono);

    check (std::isfinite (wide) && std::isfinite (mono), "auto-wah stereo correlation is finite");
    check (mono > 0.9, "Mono keeps the channels together");
    check (wide < 0.0, "Stereo pushes the channels into opposite phase");
}

void testAutoWahFilterType()
{
    std::printf ("Auto-wah: Type picks low- / band- / high-pass\n");

    auto lowHighRatio = [] (int type)
    {
        ee::dsp::AutoWah wah;
        wah.prepare (kSampleRate);
        wah.reset();
        wah.setPeriodSeconds (1.0f);
        wah.setRange01 (0.0f);      // hold the cutoff at Freq
        wah.setFreq01 (0.4f);        // ~500 Hz, between the two tones
        wah.setQ01 (0.4f);
        wah.setMix01 (1.0f);
        wah.setDecay01 (1.0f);
        wah.setType (type);

        const double wLo = 2.0 * juce::MathConstants<double>::pi * 150.0 / kSampleRate;
        const double wHi = 2.0 * juce::MathConstants<double>::pi * 4000.0 / kSampleRate;
        double p = 0.0;
        std::vector<float> buf (kBlock);

        for (int b = 0; b < 600; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            { buf[i] = 0.4f * (float) (std::sin (wLo * (p + i)) + std::sin (wHi * (p + i))); }
            p += kBlock;
            wah.process (buf.data(), nullptr, kBlock);
        }

        std::vector<float> out;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            { buf[i] = 0.4f * (float) (std::sin (wLo * (p + i)) + std::sin (wHi * (p + i))); }
            p += kBlock;
            wah.process (buf.data(), nullptr, kBlock);
            for (int i = 0; i < kBlock; ++i) out.push_back (buf[i]);
        }

        const double lo = goertzelPower (out, 150.0, kSampleRate);
        const double hi = goertzelPower (out, 4000.0, kSampleRate);
        return lo / juce::jmax (hi, 1.0e-30);
    };

    const double lp = lowHighRatio (0);
    const double bp = lowHighRatio (1);
    const double hp = lowHighRatio (2);
    std::printf ("  low/high energy ratio: LP = %.2f, BP = %.2f, HP = %.3f\n", lp, bp, hp);

    check (std::isfinite (lp) && std::isfinite (bp) && std::isfinite (hp), "auto-wah type ratios are finite");
    check (lp > bp * 2.0, "low-pass keeps the low tone over the high");
    check (hp < bp * 0.5, "high-pass keeps the high tone over the low");
}

void testAutoWahDecayGate()
{
    std::printf ("Auto-wah: Decay sets how fast the wobble flattens after a note\n");

    // A sub-gate 1 kHz probe runs the whole time, with a loud 0-250 ms burst on
    // top to open the gate. In the 400-750 ms window the burst is long gone, so
    // only the probe is left: with a short Decay the gate has closed and the
    // filter sits still (small band ripple), with Decay latched the LFO keeps
    // sweeping the probe (large band ripple).
    auto afterRipple = [] (float decay01)
    {
        ee::dsp::AutoWah wah;
        wah.prepare (kSampleRate);
        wah.reset();
        wah.setPeriodSeconds (0.12f);
        wah.setRange01 (0.9f);
        wah.setFreq01 (0.35f);
        wah.setQ01 (0.55f);
        wah.setMix01 (1.0f);
        wah.setType (1);
        wah.setDecay01 (decay01);

        const int total = (int) (kSampleRate * 1.0);
        const int burstEnd = (int) (kSampleRate * 0.25);
        const double w = 2.0 * juce::MathConstants<double>::pi * 1000.0 / kSampleRate;

        std::vector<float> out ((size_t) total);
        for (int n = 0; n < total; ++n)
        {
            const double probe = 0.004 * std::sin (w * n);            // below the gate
            const double burst = n < burstEnd ? 0.4 * std::sin (w * n) : 0.0;
            out[(size_t) n] = (float) (probe + burst);
        }
        for (int off = 0; off < total; off += kBlock)
            wah.process (out.data() + off, nullptr, juce::jmin (kBlock, total - off));

        const int hop = (int) (kSampleRate * 0.02);
        double lo = 1.0e30, hi = 0.0;
        for (int a = (int) (kSampleRate * 0.40); a + hop <= (int) (kSampleRate * 0.75); a += hop)
        {
            std::vector<float> so (out.begin() + a, out.begin() + a + hop);
            const double p = goertzelPower (so, 1000.0, kSampleRate);
            lo = juce::jmin (lo, p);
            hi = juce::jmax (hi, p);
        }
        return hi / juce::jmax (lo, 1.0e-30);   // band ripple over the window
    };

    const double shortDecay = afterRipple (0.05f);
    const double latched     = afterRipple (1.0f);
    std::printf ("  1 kHz band ripple after the note: short Decay = %.2f, latched = %.2f\n",
                shortDecay, latched);

    check (std::isfinite (shortDecay) && std::isfinite (latched), "auto-wah decay ripple is finite");
    check (latched > shortDecay * 3.0, "a short Decay flattens the filter once the note stops");
}

void testAutoWahDecayZeroIsOneShot()
{
    std::printf ("Auto-wah: Decay 0 gives a single one-way sweep per pluck\n");

    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.28f);   // half of this - one sweep - per pluck
    wah.setRange01 (0.9f);
    wah.setFreq01 (0.35f);
    wah.setQ01 (0.55f);
    wah.setMix01 (1.0f);
    wah.setType (1);
    wah.setDecay01 (0.0f);          // one-shot

    // A quiet continuous 1 kHz probe (below the gate) plus two loud bursts on
    // top. The probe's 1 kHz band should ripple right after each burst - while
    // the half cycle plays - then go flat until the next.
    const int total = (int) (kSampleRate * 1.3);
    const int b1 = (int) (kSampleRate * 0.15);
    const int b2 = (int) (kSampleRate * 0.75);
    const int burst = (int) (kSampleRate * 0.05);
    const double w = 2.0 * juce::MathConstants<double>::pi * 1000.0 / kSampleRate;

    std::vector<float> out ((size_t) total);
    for (int n = 0; n < total; ++n)
    {
        const double probe = 0.004 * std::sin (w * n);
        const bool on = (n >= b1 && n < b1 + burst) || (n >= b2 && n < b2 + burst);
        out[(size_t) n] = (float) (probe + (on ? 0.4 * std::sin (w * n) : 0.0));
    }
    for (int off = 0; off < total; off += kBlock)
        wah.process (out.data() + off, nullptr, juce::jmin (kBlock, total - off));

    const int hop = (int) (kSampleRate * 0.02);
    auto ripple = [&] (double s0, double s1)
    {
        double lo = 1.0e30, hi = 0.0;
        for (int a = (int) (s0 * kSampleRate); a + hop <= (int) (s1 * kSampleRate); a += hop)
        {
            std::vector<float> so (out.begin() + a, out.begin() + a + hop);
            const double p = goertzelPower (so, 1000.0, kSampleRate);
            lo = juce::jmin (lo, p);
            hi = juce::jmax (hi, p);
        }
        return hi / juce::jmax (lo, 1.0e-30);
    };

    const double sweep1 = ripple (0.21, 0.34);   // just after pluck 1
    const double rest   = ripple (0.45, 0.72);   // long gap - should be flat
    const double sweep2 = ripple (0.81, 0.94);   // just after pluck 2

    std::printf ("  1 kHz band ripple: sweep 1 = %.2f, rest = %.2f, sweep 2 = %.2f\n",
                sweep1, rest, sweep2);

    check (std::isfinite (sweep1) && std::isfinite (rest) && std::isfinite (sweep2),
           "auto-wah one-shot ripple is finite");
    check (sweep1 > rest * 3.0, "the pluck triggers one sweep, then it flattens");
    check (sweep2 > rest * 3.0, "the next pluck triggers another single sweep");
}

/** The one-shot must stop at the bottom of the wave: after a pluck the sweep
    runs down and stays there until the gate lets go, rather than turning round
    and climbing back up through the same frequencies. */
void testAutoWahOneShotIsOneWay()
{
    std::printf ("Auto-wah: Decay 0 sweeps one way, with no return leg\n");

    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.30f);
    wah.setRange01 (0.9f);
    wah.setFreq01 (0.35f);
    wah.setQ01 (0.55f);
    wah.setMix01 (1.0f);
    wah.setShape01 (0.5f);          // triangle: +1 at the top, -1 half a cycle on
    wah.setDecay01 (0.0f);

    // One pluck, then silence. Track the sweep exponent the pedal publishes.
    const int total = (int) (kSampleRate * 0.6);
    const int burst = (int) (kSampleRate * 0.03);
    const double w = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSampleRate;

    std::vector<float> buf ((size_t) total);
    for (int n = 0; n < total; ++n)
        buf[(size_t) n] = n < burst ? (float) (0.5 * std::sin (w * n)) : 0.0f;

    float peakMod = -1.0e9f, troughMod = 1.0e9f, afterTrough = -1.0e9f;
    bool haveTrough = false;

    for (int off = 0; off < total; off += kBlock)
    {
        const int n = juce::jmin (kBlock, total - off);
        wah.process (buf.data() + off, nullptr, n);

        const float mod = wah.modL();
        peakMod = juce::jmax (peakMod, mod);

        if (mod < troughMod && ! haveTrough)
            troughMod = mod;
        else if (troughMod < -0.05f)
            haveTrough = true;      // past the bottom of the sweep

        if (haveTrough)
            afterTrough = juce::jmax (afterTrough, mod);
    }

    std::printf ("  sweep exponent: peak = %.3f, trough = %.3f, after = %.3f\n",
                peakMod, troughMod, afterTrough);

    check (peakMod > 0.2f, "the pluck opens the sweep");
    check (troughMod < -0.2f, "and it runs down to the bottom of the wave");
    check (afterTrough < 0.05f,
           "then the gate lets go - it never climbs back up");
}

void testAutoWahRateFollowsLevel()
{
    std::printf ("Auto-wah: the LFO runs faster the harder you play\n");

    // Free-run LFO cycles completed over 3 s of a steady tone, latched Decay so
    // the LFO never stops. Count phase wraps.
    auto cyclesFor = [] (float amp)
    {
        autowah_test::Rig rig;
        rig.inAmp = amp;
        rig.probeHz = 300.0;
        rig.prep (0.25f, 0.9f, 1, false, /*Decay latched*/ 1.0f, 0.5f);

        std::vector<float> buf (kBlock);
        for (int b = 0; b < 120; ++b) { rig.fill (buf.data(), nullptr, kBlock); rig.wah.process (buf.data(), nullptr, kBlock); }

        double prev = rig.wah.phase();
        double cycles = 0.0;
        const int blocks = (int) (kSampleRate * 3.0 / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            rig.fill (buf.data(), nullptr, kBlock);
            rig.wah.process (buf.data(), nullptr, kBlock);
            const double p = rig.wah.phase();
            if (p < prev) cycles += 1.0;
            prev = p;
        }
        return cycles;
    };

    const double quiet = cyclesFor (0.03f);
    const double loud  = cyclesFor (0.6f);
    std::printf ("  LFO cycles in 3 s: quiet = %.0f, loud = %.0f\n", quiet, loud);

    check (std::isfinite (quiet) && std::isfinite (loud), "auto-wah cycle counts are finite");
    check (loud > quiet * 1.08, "a hard hit speeds the LFO up");
}

void testAutoWahRetrigger()
{
    std::printf ("Auto-wah: every string hit restarts the LFO at its top\n");

    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.30f);   // slow enough that a reset is unmistakable
    wah.setRange01 (0.9f);
    wah.setFreq01 (0.35f);
    wah.setQ01 (0.55f);
    wah.setMix01 (1.0f);
    wah.setType (1);
    wah.setDecay01 (0.3f);          // not latched

    const int total = (int) (kSampleRate * 1.2);
    const int b1 = (int) (kSampleRate * 0.20);
    const int b2 = (int) (kSampleRate * 0.70);
    const int burst = (int) (kSampleRate * 0.06);
    const double w = 2.0 * juce::MathConstants<double>::pi * 300.0 / kSampleRate;

    std::vector<float> buf ((size_t) total);
    for (int n = 0; n < total; ++n)
    {
        const bool on = (n >= b1 && n < b1 + burst) || (n >= b2 && n < b2 + burst);
        buf[(size_t) n] = on ? 0.5f * (float) std::sin (w * n) : 0.0f;
    }

    const int lo = (int) (kSampleRate * 0.003);
    const int hi = (int) (kSampleRate * 0.055);
    double minAfter1 = 1.0e9, minAfter2 = 1.0e9;

    for (int off = 0; off < total; off += kBlock)
    {
        const int len = juce::jmin (kBlock, total - off);
        wah.process (buf.data() + off, nullptr, len);
        const double ph = wah.phase();
        const int mid = off + len / 2;
        if (mid >= b1 + lo && mid < b1 + hi) minAfter1 = juce::jmin (minAfter1, ph);
        if (mid >= b2 + lo && mid < b2 + hi) minAfter2 = juce::jmin (minAfter2, ph);
    }

    std::printf ("  lowest LFO phase just after each hit: pluck 1 = %.3f, pluck 2 = %.3f\n",
                minAfter1, minAfter2);

    check (std::isfinite (minAfter1) && std::isfinite (minAfter2), "auto-wah retrigger phases are finite");
    check (minAfter1 < 0.15, "the first hit restarts the LFO near phase 0");
    check (minAfter2 < 0.15, "the second hit restarts it too");
}

void testAutoWahHoldsLevel()
{
    std::printf ("Auto-wah: the wet path stays near the dry level, not way down\n");

    // Measured right across the Type morph and at both ends of Q: the per-tap
    // make-up is crossfaded with the taps, so no position on the knob may cost
    // the player level when Mix is turned up.
    auto ratioFor = [] (float typeMorph, float q01)
    {
        ee::dsp::AutoWah wah;
        wah.prepare (kSampleRate);
        wah.reset();
        wah.setPeriodSeconds (0.3f);
        wah.setRange01 (0.6f);
        wah.setFreq01 (0.35f);
        wah.setQ01 (q01);
        wah.setMix01 (1.0f);
        wah.setDecay01 (1.0f);
        wah.setTypeMorph01 (typeMorph);

        std::mt19937 rng (2027);
        std::normal_distribution<float> dist (0.0f, 0.2f);

        std::vector<float> buf (kBlock);
        double inSq = 0.0, outSq = 0.0;

        for (int b = 0; b < 1200; ++b)
        {
            for (int i = 0; i < kBlock; ++i) buf[i] = dist (rng);
            std::vector<float> dry (buf.begin(), buf.end());
            wah.process (buf.data(), nullptr, kBlock);

            if (b >= 200)
                for (int i = 0; i < kBlock; ++i)
                {
                    inSq  += (double) dry[i] * dry[i];
                    outSq += (double) buf[i] * buf[i];
                }
        }

        return std::sqrt (outSq / juce::jmax (inSq, 1.0e-30));
    };

    bool allFinite = true, allUp = true, allSane = true;

    for (const float q : { 0.2f, 0.6f })
    {
        std::printf ("  wet / dry RMS at Q %.0f %%:", q * 100.0f);

        for (const float morph : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const double ratio = ratioFor (morph, q);
            std::printf ("  %.0f%%->%.2f", morph * 100.0f, ratio);

            allFinite = allFinite && std::isfinite (ratio);
            allUp = allUp && ratio > 0.8;
            allSane = allSane && ratio < 2.0;
        }
        std::printf ("\n");
    }

    check (allFinite, "auto-wah level ratios are finite");
    check (allUp, "no Type position guts the level");
    check (allSane, "and none of them blows it up");
}

void testAutoWahMixRampIsSmooth()
{
    std::printf ("Auto-wah: a Mix jump ramps rather than steps (no tick)\n");

    // Worst case: a resonant band-pass whose wet output is out of phase with the
    // dry. An un-smoothed Mix jump from 0 to 1 would step the output by roughly
    // |wet - dry| in a single sample; the ramp must keep every step near the
    // wet signal's own steady sample-to-sample slope.
    auto run = [] (bool jumpMidway)
    {
        ee::dsp::AutoWah wah;
        wah.prepare (kSampleRate);
        wah.reset();
        wah.setPeriodSeconds (0.25f);
        wah.setFreq01 (0.4f);
        wah.setQ01 (0.5f);
        wah.setRange01 (0.9f);
        wah.setType (1);
        wah.setDecay01 (1.0f);
        wah.setMix01 (jumpMidway ? 0.0f : 1.0f);

        const double w = 2.0 * juce::MathConstants<double>::pi * 520.0 / kSampleRate;
        double phase = 0.0;
        std::vector<float> buf (kBlock);

        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < kBlock; ++i) { buf[i] = 0.5f * (float) std::sin (phase); phase += w; }
            wah.process (buf.data(), nullptr, kBlock);
        }

        if (jumpMidway)
            wah.setMix01 (1.0f);

        float maxStep = 0.0f;
        float prev = 0.0f;
        for (int b = 0; b < 24; ++b)   // ~130 ms - covers the 15 ms ramp
        {
            for (int i = 0; i < kBlock; ++i) { buf[i] = 0.5f * (float) std::sin (phase); phase += w; }
            wah.process (buf.data(), nullptr, kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                if (b > 0 || i > 0)
                    maxStep = juce::jmax (maxStep, std::abs (buf[i] - prev));
                prev = buf[i];
            }
        }
        return maxStep;
    };

    const float steady = run (false);
    const float acrossJump = run (true);
    std::printf ("  largest sample step: steady wet = %.4f, across the Mix jump = %.4f\n",
                steady, acrossJump);

    check (std::isfinite (acrossJump), "auto-wah mix-jump step is finite");
    check (acrossJump < steady * 1.5f + 0.02f, "the Mix jump does not step the output");
}
//==============================================================================
// Peak Spring - the dispersive spring tank.

/** Drives the tank to a steady state, then measures how long the tail takes to
    fall 60 dB once the input stops. */
double measureSpringRt60 (float decaySeconds)
{
    ee::dsp::SpringReverb spring;
    spring.prepare (kSampleRate);
    spring.reset();
    spring.setDecayTime (decaySeconds);

    std::mt19937 rng (7);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

    std::vector<float> in (kBlock), l (kBlock), r (kBlock);

    // Two seconds of noise is long enough for even the longest spring to fill.
    for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
    {
        for (auto& v : in)
            v = dist (rng);
        spring.process (in.data(), l.data(), r.data(), kBlock);
    }

    std::fill (in.begin(), in.end(), 0.0f);

    float peak = 0.0f;
    double elapsed = 0.0;
    double rt60 = 0.0;

    const int blocks = static_cast<int> (kSampleRate * 20.0 / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        spring.process (in.data(), l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            const float m = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            peak = juce::jmax (peak, m);

            const double t = elapsed + static_cast<double> (i) / kSampleRate;
            if (m > peak * 0.001f)   // -60 dB relative to the loudest sample seen
                rt60 = t;
        }

        elapsed += static_cast<double> (kBlock) / kSampleRate;
    }

    return rt60;
}

void testSpringDecay()
{
    std::printf ("Spring tank decay vs the knob:\n");

    for (float target : { 0.6f, 1.8f, 4.0f, 8.0f })
    {
        const double measured = measureSpringRt60 (target);
        std::printf ("  knob %.1f s -> tail %.2f s\n", target, measured);

        // The loop shelves damp the bands either side of the ring band, so the
        // broadband tail lands short of the nominal RT60 - by design, and by
        // the same margin the reference tank shows. Wide bounds on purpose:
        // this is a "the knob does what it says" check, not a voicing lock,
        // which is what the config header is for.
        check (measured > target * 0.3, "spring tail far shorter than the knob at "
                                            + juce::String (target, 1) + " s");
        check (measured < target * 2.0 + 0.5, "spring tail far longer than the knob at "
                                                  + juce::String (target, 1) + " s");
    }
}

void testSpringSilence()
{
    std::printf ("Spring tank on a silent input:\n");

    ee::dsp::SpringReverb spring;
    spring.prepare (kSampleRate);
    spring.reset();
    spring.setDecayTime (ee::dsp::SpringReverb::kMaxDecay);

    std::vector<float> in (kBlock, 0.0f), l (kBlock), r (kBlock);
    float peak = 0.0f;

    for (int b = 0; b < static_cast<int> (kSampleRate * 5.0 / kBlock); ++b)
    {
        spring.process (in.data(), l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            peak = juce::jmax (peak, juce::jmax (std::abs (l[i]), std::abs (r[i])));
    }

    std::printf ("  peak out of silence: %.3g\n", peak);
    check (peak < 1.0e-6f, "spring tank is not silent on a silent input");
}

void testSpringStability()
{
    std::printf ("Spring tank under sustained full-scale noise:\n");

    ee::dsp::SpringReverb spring;
    spring.prepare (kSampleRate);
    spring.reset();
    spring.setDecayTime (ee::dsp::SpringReverb::kMaxDecay);

    std::mt19937 rng (4321);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> in (kBlock), l (kBlock), r (kBlock);
    float peak = 0.0f;
    bool finite = true;

    for (int b = 0; b < static_cast<int> (kSampleRate * 30.0 / kBlock); ++b)
    {
        for (auto& v : in)
            v = dist (rng);

        spring.process (in.data(), l.data(), r.data(), kBlock);

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
    check (peak < 8.0f, "spring tank is not energy-stable (peak " + juce::String (peak, 2) + ")");
}

void testSpringDecaySweepIsQuiet()
{
    std::printf ("Spring decay sweep (click / discontinuity check):\n");

    ee::dsp::SpringReverb spring;
    spring.prepare (kSampleRate);
    spring.reset();

    std::mt19937 rng (55);
    std::uniform_real_distribution<float> dist (-0.25f, 0.25f);

    std::vector<float> in (kBlock), l (kBlock), r (kBlock);
    float maxJump = 0.0f;
    float previous = 0.0f;
    bool finite = true;

    const int blocks = static_cast<int> (kSampleRate * 10.0 / kBlock);

    for (int b = 0; b < blocks; ++b)
    {
        const float phase = static_cast<float> (b) / static_cast<float> (blocks);
        const float t = 1.0f - std::abs (2.0f * phase - 1.0f);
        spring.setDecayTime (ee::dsp::SpringReverb::kMinDecay
                             + t * (ee::dsp::SpringReverb::kMaxDecay - ee::dsp::SpringReverb::kMinDecay));

        for (auto& v : in)
            v = dist (rng);

        spring.process (in.data(), l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            if (! std::isfinite (l[i]))
                finite = false;
            maxJump = juce::jmax (maxJump, std::abs (l[i] - previous));
            previous = l[i];
        }
    }

    std::printf ("  largest sample-to-sample jump: %.4f\n", maxJump);
    check (finite, "spring decay sweep produced NaN or Inf");
    check (maxJump < 0.5f, "spring decay sweep produced a discontinuity ("
                               + juce::String (maxJump, 3) + ")");
}

void testSpringDisperses()
{
    std::printf ("Spring dispersion (an impulse must come back as a chirp):\n");

    ee::dsp::SpringReverb spring;
    spring.prepare (kSampleRate);
    spring.reset();
    spring.setDecayTime (1.0f);

    std::vector<float> in (kBlock, 0.0f), l (kBlock), r (kBlock);
    in[0] = 1.0f;

    // Collect the first 120 ms - two or three trips round the shortest spring.
    const int captured = static_cast<int> (kSampleRate * 0.12);
    std::vector<float> tail;
    tail.reserve (static_cast<size_t> (captured + kBlock));

    while (static_cast<int> (tail.size()) < captured)
    {
        spring.process (in.data(), l.data(), r.data(), kBlock);
        tail.insert (tail.end(), l.begin(), l.end());
        std::fill (in.begin(), in.end(), 0.0f);
    }

    // A plain delay loop returns the impulse as an impulse: one sample carrying
    // nearly all the energy. Dispersion smears it, so the loudest sample should
    // hold only a small fraction of what came back.
    double energy = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < captured; ++i)
    {
        const float v = tail[static_cast<size_t> (i)];
        energy += static_cast<double> (v) * v;
        peak = juce::jmax (peak, std::abs (v));
    }

    const double concentration = energy > 0.0 ? (peak * peak) / energy : 1.0;
    std::printf ("  loudest sample holds %.2f %% of the returned energy\n", concentration * 100.0);

    check (energy > 1.0e-9, "the tank returned nothing at all");
    check (concentration < 0.10, "the tank is not dispersing - the impulse came back as an impulse");

    // Both sides have to carry a tail; the two tanks differ only in length.
    double rightEnergy = 0.0;
    for (int i = 0; i < kBlock; ++i)
        rightEnergy += static_cast<double> (r[i]) * r[i];
    check (std::isfinite (rightEnergy), "the right tank went non-finite");
}

} // namespace

int main()
{
    std::printf ("=== Synth Peak DSP tests ===\n\n");

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
    testTapeMachineAtRest();
    std::printf ("\n");
    testTapeMachineSilence();
    std::printf ("\n");
    testTapeMachineSaturation();
    std::printf ("\n");
    testTapeMachineNoiseIsConstant();
    std::printf ("\n");
    testTapeMachineNoiseLoop();
    std::printf ("\n");
    testTapeMachineToneTilt();
    std::printf ("\n");
    testTapeMachineStereoWidens();
    std::printf ("\n");
    testTapeMachineStability();
    std::printf ("\n");
    testChorusSilence();
    std::printf ("\n");
    testChorusBypassIsUnity();
    std::printf ("\n");
    testChorusWidensImage();
    std::printf ("\n");
    testChorusStereoIsStable();
    std::printf ("\n");
    testPhaserSilence();
    std::printf ("\n");
    testPhaserStability();
    std::printf ("\n");
    testPhaserSweeps();
    std::printf ("\n");
    testOverdriveSilence();
    std::printf ("\n");
    testOverdriveStability();
    std::printf ("\n");
    testOverdriveAddsHarmonics();
    std::printf ("\n");
    testOverdriveToneTilt();
    std::printf ("\n");
    testOverdriveAntiAliasing();
    std::printf ("\n");
    testAutoWahSilence();
    std::printf ("\n");
    testAutoWahMixZeroIsDry();
    std::printf ("\n");
    testAutoWahStability();
    std::printf ("\n");
    testAutoWahLfoModulates();
    std::printf ("\n");
    testAutoWahStereoOpposes();
    std::printf ("\n");
    testAutoWahFilterType();
    std::printf ("\n");
    testAutoWahDecayGate();
    std::printf ("\n");
    testAutoWahDecayZeroIsOneShot();
    testAutoWahOneShotIsOneWay();
    std::printf ("\n");
    testAutoWahRateFollowsLevel();
    std::printf ("\n");
    testAutoWahRetrigger();
    std::printf ("\n");
    testAutoWahHoldsLevel();
    std::printf ("\n");
    testAutoWahMixRampIsSmooth();
    std::printf ("\n");
    testSpringDecay();
    std::printf ("\n");
    testSpringSilence();
    std::printf ("\n");
    testSpringStability();
    std::printf ("\n");
    testSpringDecaySweepIsQuiet();
    std::printf ("\n");
    testSpringDisperses();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
