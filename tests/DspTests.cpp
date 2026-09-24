#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "ee/dsp/AutoWah.h"
#include "ee/dsp/BitCrusher.h"
#include "ee/dsp/BreakpointLfo.h"
#include "ee/plugin/ModRouter.h"
#include "ee/plugin/SafeParse.h"
#include "ee/dsp/Chorus.h"
#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/SpaceReverb.h"
#include "ee/dsp/Grainer.h"
#include "ee/dsp/GrainerConfig.h"
#include "ee/dsp/ModDelayLine.h"
#include "ee/dsp/Phaser.h"
#include "ee/dsp/RingModulator.h"
#include "ee/dsp/Rust.h"
#include "ee/dsp/SpringReverb.h"
#include "ee/dsp/Overdrive.h"
#include "ee/dsp/BitBitLimiter.h"
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

/** FNV-1a over the output of a shimmered reverb fed a burst of noise, so two
    renders can be compared bit for bit. `seedNoise` varies the input, which is
    how the perturbing render below differs from the two that are compared. */
uint64_t shimmerChecksum (unsigned seedNoise)
{
    ee::dsp::FdnReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.reset();
    reverb.setDecayTime (4.0f);
    reverb.setShimmer (1.0f);

    std::mt19937 rng (seedNoise);
    std::uniform_real_distribution<float> dist (-0.3f, 0.3f);
    std::vector<float> in (kBlock), l (kBlock), r (kBlock);

    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash] (float v)
    {
        uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof bits);
        hash = (hash ^ bits) * 1099511628211ull;
    };

    for (int b = 0; b < static_cast<int> (kSampleRate * 3.0 / kBlock); ++b)
    {
        for (auto& x : in)
            x = b < 40 ? dist (rng) : 0.0f;

        reverb.process (in.data(), l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            mix (l[static_cast<size_t> (i)]);
            mix (r[static_cast<size_t> (i)]);
        }
    }

    return hash;
}

/** The shimmer's pitch shifter used to draw its modulation from one static
    generator shared by the whole process, and left several members
    uninitialised. Either makes a render depend on what ran before it, and the
    first is a data race between plugin instances. Run under TSan this is the
    test for the race; under any build it asserts the render is reproducible. */
void testShimmerReproducible()
{
    std::printf ("Shimmer reproducibility (per-instance generator, no shared state):\n");

    const uint64_t first = shimmerChecksum (1);
    (void) shimmerChecksum (99);             // would advance a shared generator
    const uint64_t second = shimmerChecksum (1);

    std::printf ("  sequential renders: %016llx %016llx\n",
                 static_cast<unsigned long long> (first), static_cast<unsigned long long> (second));
    check (first == second, "a shimmered render differs from an identical one run after another");

    uint64_t a = 0, b = 0;
    std::thread ta ([&a] { a = shimmerChecksum (1); });
    std::thread tb ([&b] { b = shimmerChecksum (1); });
    ta.join();
    tb.join();

    std::printf ("  concurrent renders: %016llx %016llx\n",
                 static_cast<unsigned long long> (a), static_cast<unsigned long long> (b));
    check (a == first && b == first, "concurrent shimmered renders differ from a solo one");
}

/** The Shimmer octave selector's two failure modes, both reported by ear: at 0
    the tail kept climbing after the note stopped (a coherent loop with gain
    over 1 at long Decay), and -1 gave no lower octave at all (DaisySP's downward
    shift ratio leaves the read head standing still). */
void testShimmerOctaves()
{
    std::printf ("Shimmer octaves (0 must decay, -1 must land an octave down):\n");

    constexpr double sr = 48000.0;
    constexpr double note = 110.0;
    constexpr int block = 512;

    // One 2 s note, then silence to 14 s. Returns the wet left channel.
    auto render = [&] (int octave)
    {
        ee::dsp::FdnReverb reverb;
        reverb.prepare (sr);
        reverb.setDecayTime (10.0f);
        reverb.setShimmer (1.0f);
        reverb.setOctave (octave);

        std::vector<float> in (block), l (block), r (block), wet;
        double phase = 0.0;
        int n = 0;
        for (int b = 0; b < static_cast<int> (sr * 14.0 / block); ++b)
        {
            for (int i = 0; i < block; ++i, ++n)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * note / sr;
                in[static_cast<size_t> (i)] = n < static_cast<int> (sr * 2.0)
                    ? 0.3f * static_cast<float> (std::sin (phase) + 0.5 * std::sin (2.0 * phase)) : 0.0f;
            }
            reverb.process (in.data(), l.data(), r.data(), block);
            wet.insert (wet.end(), l.begin(), l.end());
        }
        return wet;
    };

    auto rmsDb = [&] (const std::vector<float>& x, double fromSec, double toSec)
    {
        double sum = 0.0;
        const auto a = static_cast<size_t> (fromSec * sr), b = static_cast<size_t> (toSec * sr);
        for (size_t i = a; i < b; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return 10.0 * std::log10 (sum / static_cast<double> (b - a) + 1.0e-12);
    };

    auto binDb = [&] (const std::vector<float>& x, double hz, double fromSec, double toSec)
    {
        double re = 0.0, im = 0.0;
        const auto a = static_cast<size_t> (fromSec * sr), b = static_cast<size_t> (toSec * sr);
        for (size_t i = a; i < b; ++i)
        {
            const double ang = 2.0 * juce::MathConstants<double>::pi * hz * static_cast<double> (i) / sr;
            re += x[i] * std::cos (ang);
            im += x[i] * std::sin (ang);
        }
        return 20.0 * std::log10 (std::sqrt (re * re + im * im) / static_cast<double> (b - a) + 1.0e-9);
    };

    const auto unison = render (0);
    const auto down = render (-1);
    const auto up = render (1);

    for (const auto* w : { &unison, &down, &up })
        check (rmsDb (*w, 12.0, 14.0) < rmsDb (*w, 8.0, 10.0) - 3.0,
               "the tail is still decaying at 12 s (octave " + juce::String (w == &unison ? "0" : w == &down ? "-1" : "+1") + ")");

    // The lower octave's note is 55 Hz; nothing plays there, so energy at it is
    // the shifter's. It measures about -55 dB with the fix and sits at the
    // ~-96 dB floor without it (the note's own harmonic hides a like-for-like
    // check at +1, so only the floor is asserted).
    const double lowerDb = binDb (down, note * 0.5, 1.0, 5.0);
    std::printf ("  -1 octave content at 55 Hz: %.1f dB\n", lowerDb);
    check (lowerDb > -70.0, "-1 octave puts energy an octave below the note");
}

/** Dragging Studio's Size knob must not click. Size moves every delay the room
    has, and used to jump each of them to its new length at the top of the next
    block - a discontinuity in the audio per block of the drag. Measured as the
    second difference of a sustained tone's wet output: a smooth signal has a
    small one everywhere, a click is a spike many times the rest. */
void testStudioSizeSweepDoesNotClick()
{
    std::printf ("Studio Size sweep (no clicks):\n");

    constexpr double sr = 48000.0;
    constexpr int block = 256;

    // Largest second difference of the wet output over 1 s in which Size is
    // dragged from 0.6 to 1.4 in one step per block (or not at all).
    auto worstStepOf = [&] (bool sweepSize)
    {
        ee::dsp::SpaceReverb reverb;
        reverb.prepare (sr);
        reverb.setDecayTime (3.0f);
        reverb.setSize (0.6f);

        std::vector<float> in (block), l (block), r (block), wet;
        double phase = 0.0;
        const int settle = static_cast<int> (sr * 2.0 / block);
        const int sweep = static_cast<int> (sr * 1.0 / block);

        for (int b = 0; b < settle + sweep + settle / 2; ++b)
        {
            for (auto& x : in)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * 330.0 / sr;
                x = 0.3f * static_cast<float> (std::sin (phase));
            }

            if (sweepSize && b >= settle && b < settle + sweep)
                reverb.setSize (0.6f + 0.8f * static_cast<float> (b - settle + 1) / static_cast<float> (sweep));

            reverb.process (in.data(), in.data(), l.data(), r.data(), block);
            if (b >= settle)
                wet.insert (wet.end(), l.begin(), l.end());
        }

        // The largest second difference: a sine has a small one everywhere, a
        // step in the signal has one as big as the step.
        double worst = 0.0;
        for (size_t i = 1; i + 1 < wet.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (wet[i - 1]) - 2.0 * wet[i] + wet[i + 1]));
        return worst;
    };

    const double still = worstStepOf (false);
    const double swept = worstStepOf (true);
    std::printf ("  largest second difference: %.5f held still, %.5f dragged (tone peaks near 0.13)\n", still, swept);
    check (swept < still * 5.0, "dragging Size leaves no step in the wet output");
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

/** The head-shift warp a moving Time knob puts on the output must not be
    written back into the feedback loop and repeated for the rest of the tail -
    see TapeDelay::setDelaySeconds and kLoopFadeSeconds.

    Measured as purity rather than as pitch: fill the loop with one tone, move
    the time while it rings, and ask how much of what is still circulating a
    couple of seconds later is at that tone's own frequency. A tap that glides
    reads the line at a changing rate, which does not shift the tone so much as
    smear it - at this size of move the read pointer briefly runs backwards -
    so the surviving tail is barely at the tone's frequency at all. */
void testDelayTimeChangeStaysOutOfTheLoop()
{
    std::printf ("Delay time change does not recirculate:\n");

    constexpr float toneHz = 1000.0f;
    constexpr float feedback = 0.75f;

    ee::dsp::TapeDelay delay;
    delay.prepare (kSampleRate);
    delay.setDelaySeconds (0.25f, 0.25f);
    delay.snapDelays();
    delay.setFeedback (feedback);
    delay.setModulation (0.0f);

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    // A second of tone, which at a quarter-second delay is four trips round the
    // loop - long enough that what is circulating is the tone and nothing else.
    int n = 0;
    for (int b = 0; b < static_cast<int> (kSampleRate / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i, ++n)
        {
            inL[i] = std::sin (2.0f * juce::MathConstants<float>::pi * toneHz * n / static_cast<float> (kSampleRate));
            inR[i] = inL[i];
        }

        delay.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }

    // The knob moves with the input already stopped, so everything measured
    // below came out of the loop rather than off the input.
    std::fill (inL.begin(), inL.end(), 0.0f);
    std::fill (inR.begin(), inR.end(), 0.0f);
    delay.setDelaySeconds (0.40f, 0.40f);

    std::vector<float> tail;
    const int tailBlocks = static_cast<int> (kSampleRate * 3.0 / kBlock);
    const int measureFrom = static_cast<int> (kSampleRate * 2.0 / kBlock);

    for (int b = 0; b < tailBlocks; ++b)
    {
        delay.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        if (b >= measureFrom)
            tail.insert (tail.end(), outL.begin(), outL.end());
    }

    // One DFT bin at the tone, against the window's total energy.
    double re = 0.0, im = 0.0, energy = 0.0;
    for (size_t i = 0; i < tail.size(); ++i)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * toneHz * static_cast<double> (i) / kSampleRate;
        re += tail[i] * std::cos (w);
        im += tail[i] * std::sin (w);
        energy += static_cast<double> (tail[i]) * tail[i];
    }

    const double purity = energy > 0.0 ? 2.0 * (re * re + im * im) / static_cast<double> (tail.size()) / energy : 0.0;

    std::printf ("  tail energy at %.0f Hz, two seconds after the move: %.1f %%\n", toneHz, purity * 100.0);

    // A stepped feedback tap measures around 0.91 here; a gliding one, 0.007.
    // The threshold sits between the two rather than beside either.
    check (purity > 0.5, "a time change warped the signal circulating in the delay");
}

/** Moving the Tape section from one side of the delay to the other must not
    click - see BitBitDelayProcessor's tapeIn/tapeOut.

    The click was never the *colour* changing. It was that one tape section
    handed a different signal keeps playing the previous one out of its delay
    line for six milliseconds first, and a splice from one signal to another is
    a step in the waveform however gently the knobs around it move. The fix is
    that the section does not move: there is one at each end of the delay, and
    what the router does is turn one down while the other comes up.

    Measured by level rather than by slew. A splice between two waveforms can
    land anywhere in their cycles and be small by luck, so the two signals here
    differ by 28 dB instead: the loud one is what the section was being fed
    before the flip, the quiet one is what it is fed after. Anything of the loud
    signal appearing in the quiet path is the stale line coming out, and no
    phase relationship can hide it.
*/
void testTapeRouterDoesNotSplice()
{
    std::printf ("Tape router placement change:\n");

    const int total = static_cast<int> (kSampleRate);
    const int flipAt = total / 2;

    constexpr float kLoudAmp = 0.5f;
    constexpr float kQuietAmp = 0.02f;

    std::vector<float> loud (total), quiet (total);
    for (int i = 0; i < total; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kSampleRate);
        loud[i] = kLoudAmp * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * t);
        quiet[i] = kQuietAmp * std::sin (2.0f * juce::MathConstants<float>::pi * 660.0f * t);
    }

    const auto peakOver = [] (const std::vector<float>& v, int from, int to)
    {
        float worst = 0.0f;
        for (int i = juce::jmax (0, from); i < juce::jmin (static_cast<int> (v.size()), to); ++i)
            worst = juce::jmax (worst, std::abs (v[i]));
        return worst;
    };

    struct Section
    {
        ee::dsp::TapeTransport transport;
        ee::dsp::TapeCharacter tape;

        void prepare (double sr)
        {
            transport.prepare (sr);
            tape.prepare (sr);
        }

        void setAmounts (float wear, float flutter) noexcept
        {
            transport.setFlutter01 (flutter);
            tape.setAmount (wear);
        }

        void process (float* l, float* r, int n) noexcept
        {
            transport.process (l, r, n);
            tape.process (l, r, n);
        }
    };

    constexpr float kWear = 0.6f;
    constexpr float kFlutter = 0.6f;
    constexpr int block = 64;

    // The router's own glide, as a fraction per block - the processor smooths it
    // over a quarter of a second.
    const float perBlock = static_cast<float> (block / (0.25 * kSampleRate));

    std::vector<float> moved (total, 0.0f), paired (total, 0.0f);

    {
        // The old arrangement: one section, which was on the dry path and is
        // now handed the repeats.
        Section one;
        one.prepare (kSampleRate);
        one.setAmounts (kWear, kFlutter);

        for (int i = 0; i + block <= total; i += block)
        {
            const float* source = i < flipAt ? loud.data() : quiet.data();
            std::vector<float> l (source + i, source + i + block), r (l);
            one.process (l.data(), r.data(), block);
            std::copy (l.begin(), l.end(), moved.begin() + i);
        }
    }

    {
        // The new one: this section has been on the repeats all along and only
        // its amount changes.
        Section post;
        post.prepare (kSampleRate);
        post.setAmounts (0.0f, 0.0f);

        float placement = 0.0f;

        for (int i = 0; i + block <= total; i += block)
        {
            if (i >= flipAt)
                placement = juce::jmin (1.0f, placement + perBlock);

            post.setAmounts (kWear * placement, kFlutter * placement);

            std::vector<float> l (quiet.begin() + i, quiet.begin() + i + block), r (l);
            post.process (l.data(), r.data(), block);
            std::copy (l.begin(), l.end(), paired.begin() + i);
        }
    }

    // The first 20 ms after the flip: long enough to contain the six
    // milliseconds of stale line the old arrangement flushes out.
    const int to = flipAt + static_cast<int> (kSampleRate * 0.02);

    const float movedPeak = peakOver (moved, flipAt, to);
    const float pairedPeak = peakOver (paired, flipAt, to);

    std::printf ("  the quiet signal alone:                     %.4f\n", kQuietAmp);
    std::printf ("  ...a section moved across the delay:        %.4f\n", movedPeak);
    std::printf ("  ...a section at each end, amounts crossed:  %.4f\n", pairedPeak);

    // The moved section dumps most of the loud signal into the quiet path; the
    // paired one never carries anything that was not already there.
    check (movedPeak > kQuietAmp * 5.0f,
           "the moved-section model failed to splice - the test no longer proves anything");
    check (pairedPeak < kQuietAmp * 2.0f, "crossing the tape amounts leaked the other path into this one");
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

        // The level claim is a midband one - the stage rolls the top off on
        // purpose, so broadband noise would measure that loss and not the
        // level. Two one-pole lowpasses at 3 kHz keep the energy where the
        // stage is meant to leave it alone, and the scale puts the rms at
        // the level its makeup gain is calibrated for (TapeTuning::levelReference).
        std::vector<float> band (source);
        {
            const float coeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 3000.0f
                                                 / static_cast<float> (kSampleRate));
            float z1 = 0.0f, z2 = 0.0f;
            double energy = 0.0;
            for (auto& v : band)
            {
                z1 += coeff * (v - z1);
                z2 += coeff * (z1 - z2);
                v = z2;
                energy += static_cast<double> (v) * v;
            }
            const float scale = tape.getTuning().levelReference
                                / static_cast<float> (std::sqrt (energy / band.size()));
            for (auto& v : band)
                v *= scale;
        }

        std::vector<float> l (band), r (band);
        tape.process (l.data(), r.data(), total);

        const auto rms = [] (const std::vector<float>& v, int from)
        {
            double sum = 0.0;
            for (int i = from; i < static_cast<int> (v.size()); ++i)
                sum += static_cast<double> (v[i]) * v[i];
            return std::sqrt (sum / (v.size() - from));
        };

        const double before = rms (band, 0);
        const double after = rms (l, 512);
        const double changeDb = 20.0 * std::log10 (after / before);

        bool finite = true;
        for (int i = 0; i < total; ++i)
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);

        // The reference machine came back +0.5 dB; a character control that
        // changes the level is a volume control in disguise. With the shipped
        // drive it took 4 dB off before TapeCharacter calibrated its makeup at a
        // played level rather than at the origin.
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
// Tape machine (BitBit Tape)

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

/** The delay the tape machine is applying to a ramp, sample by sample: a ramp
    comes out as itself read `d` samples late, so `d = i - y / slope`. Returns
    the smallest and largest, over the second half of the run. */
std::pair<float, float> tapeDelayRange (double sampleRate, bool lowLatency, float flutter, float stereo)
{
    const int total = static_cast<int> (sampleRate * 3.0);
    constexpr float slope = 1.0e-3f;

    ee::dsp::TapeMachine machine;
    machine.setLowLatency (lowLatency);
    machine.prepare (sampleRate);
    machine.reset();
    restTapeMachine (machine);
    machine.setFlutter01 (flutter);
    machine.setStereo01 (stereo);

    std::vector<float> l (total), r (total);
    for (int i = 0; i < total; ++i)
        l[i] = r[i] = slope * static_cast<float> (i);

    machine.process (l.data(), r.data(), total);

    float lo = 1.0e9f, hi = -1.0e9f;
    for (int i = total / 2; i < total; ++i)
    {
        const float d = static_cast<float> (i) - l[i] / slope;
        lo = juce::jmin (lo, d);
        hi = juce::jmax (hi, d);
    }

    return { lo, hi };
}

void testTapeMachineLowLatency()
{
    std::printf ("Tape machine: Low Latency shortens the transport and keeps the wow inside it\n");

    // The latency it reports, in either mode, agrees with what latencyFor()
    // works out without a prepared machine.
    bool agrees = true;
    for (const double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (const bool low : { false, true })
        {
            ee::dsp::TapeMachine m;
            m.setLowLatency (low);
            m.prepare (rate);
            agrees = agrees && m.getLatencySamples() == ee::dsp::TapeMachine::latencyFor (rate, low);
        }
    check (agrees, "latencyFor() matches a prepared machine, both modes, every rate");

    const int normal = ee::dsp::TapeMachine::latencyFor (kSampleRate, false);
    const int low = ee::dsp::TapeMachine::latencyFor (kSampleRate, true);
    std::printf ("  %d samples (%.2f ms) normal, %d samples (%.2f ms) low latency\n", normal,
                 1000.0 * normal / kSampleRate, low, 1000.0 * low / kSampleRate);
    check (low < normal, "low latency reports less");

    // At rest it is still a bit-exact delay, just a shorter one.
    {
        const int total = static_cast<int> (kSampleRate);
        std::mt19937 rng (0x1a7e);
        std::normal_distribution<float> dist (0.0f, 0.12f);
        std::vector<float> source (total);
        for (auto& v : source)
            v = std::tanh (dist (rng));

        ee::dsp::TapeMachine machine;
        machine.setLowLatency (true);
        machine.prepare (kSampleRate);
        machine.reset();
        restTapeMachine (machine);

        std::vector<float> l (source), r (source);
        machine.process (l.data(), r.data(), total);

        float worst = 0.0f;
        for (int i = low; i < total; ++i)
            worst = juce::jmax (worst, std::abs (l[i] - source[i - low]));

        check (worst == 0.0f, "low latency at rest is a bit-exact delay of exactly what it reports");
    }

    // The point of the depth scale: Flutter and Stereo at full must stay inside
    // the shorter line. Measured off a ramp, so it is the delay the audio
    // actually gets and not what the code says it asked for.
    const auto [nLo, nHi] = tapeDelayRange (kSampleRate, false, 1.0f, 1.0f);
    const auto [lLo, lHi] = tapeDelayRange (kSampleRate, true, 1.0f, 1.0f);

    const float wearStage = static_cast<float> (ee::dsp::TapeCharacter::latencyFor (kSampleRate));
    const float lowNominal = static_cast<float> (ee::dsp::TapeTransport::latencyFor (kSampleRate, true));
    const float lowLimit = ee::dsp::tape::kWobbleLimit * lowNominal;

    std::printf ("  Flutter + Stereo at 100 %%, delay swings %.1f..%.1f samples normal, %.1f..%.1f low latency\n",
                 nLo, nHi, lLo, lHi);

    // Mono has no width to make room for, so the wow keeps its full depth in the
    // short line - what the Delay, which never opens Stereo, gets.
    const auto [mnLo, mnHi] = tapeDelayRange (kSampleRate, false, 1.0f, 0.0f);
    const auto [mlLo, mlHi] = tapeDelayRange (kSampleRate, true, 1.0f, 0.0f);
    std::printf ("  Flutter 100 %%, Stereo off: swing %.1f samples normal, %.1f low latency\n", mnHi - mnLo, mlHi - mlLo);
    check (std::abs ((mlHi - mlLo) - (mnHi - mnLo)) < 1.0f, "in mono, low latency gives up none of Flutter's depth");

    check (lLo >= wearStage + lowNominal - lowLimit - 0.5f && lHi <= wearStage + lowNominal + lowLimit + 0.5f,
           "low latency never swings past its own ceiling, and so never reaches the write head");
    check (lHi - lLo > 1.0f, "...and still moves");
    check ((lHi - lLo) < (nHi - nLo), "...by less than the normal transport, which is the trade");
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

/** The parsers in JUCE recurse per level of nesting, so a file that is only
    opening tags overflows the host's stack instead of failing to parse. The
    scans in ee/plugin/SafeParse.h refuse it first; this holds them to refusing
    the deep and to admitting everything a state actually looks like. */
void testSafeParse()
{
    std::printf ("SafeParse: deep nesting is refused before the parser, real state is not\n");

    const auto deepXml = [] (int depth)
    {
        return juce::String ("<S>") + juce::String::repeatedString ("<a>", depth)
               + juce::String::repeatedString ("</a>", depth) + "</S>";
    };

    check (ee::plugin::parseXmlText (deepXml (10)) != nullptr, "a shallow tree parses");
    check (ee::plugin::parseXmlText (deepXml (200000)) == nullptr, "200000 levels are refused, not recursed into");
    check (ee::plugin::parseXmlText (juce::String ("<S>") + juce::String::repeatedString ("<a>", 500000)) == nullptr,
           "...and so is an unterminated run of opening tags");

    // What a real state looks like: a root, a row of self-closing children with
    // '/' and '>' inside quoted values that must not be mistaken for tag ends.
    const juce::String realistic =
        "<PARAMETERS stateVersion=\"1\" lfo=\"[{&quot;x&quot;:0}]\"><PARAM id=\"a\" value=\"0.5\"/>"
        "<PARAM id=\"b/>\" value=\"1\"/><PARAM id=\"c\" value=\"2\"></PARAM></PARAMETERS>";
    check (ee::plugin::xmlNestingWithin (realistic.toRawUTF8(), realistic.getNumBytesAsUTF8()),
           "a realistic state is within the limit");

    // A '/>' hidden in an attribute must not be read as a close, or a deep file
    // could disguise itself as a shallow one.
    const auto disguised = juce::String ("<S>") + juce::String::repeatedString ("<a x=\"/>\">", 500) + "</S>";
    check (! ee::plugin::xmlNestingWithin (disguised.toRawUTF8(), disguised.getNumBytesAsUTF8()),
           "'/>' inside a quoted attribute does not hide nesting");

    check (ee::plugin::parseJson ("[{\"a\":[1,2,{\"b\":null}]}]").isArray(), "shallow JSON parses");
    check (ee::plugin::parseJson (juce::String::repeatedString ("[", 100000)).isVoid(), "100000 nested arrays are refused");
    check (ee::plugin::parseJson (juce::String::repeatedString ("{\"a\":", 100000)).isVoid(), "...and nested objects");
    check (ee::plugin::parseJson ("[\"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[\"]").isArray(),
           "brackets inside a string do not count as depth");
}

void testModDelayLineWrapBoundary()
{
    std::printf ("ModDelayLine: a read that wraps the buffer edge stays inside it\n");

    // readPos = writeIndex - delay is a hair below zero, and adding the buffer
    // size back rounds to exactly `size` in float - one past the last sample.
    // The value is only checkable as "still the constant we filled it with",
    // but the real assertion is ASan's: this is a read of the heap next door.
    ee::dsp::ModDelayLine line;
    line.prepare (kSampleRate, 0.1f);

    const int size = static_cast<int> (kSampleRate * 0.1f) + 4;   // as prepare() sizes it
    int writeIndex = 0;
    bool constant = true;

    for (int n = 0; n < 3 * size; ++n)
    {
        line.write (1.0f);
        line.advance();
        writeIndex = (writeIndex + 1) % size;

        // The first lap is still filling the line with ones.
        if (n < size || writeIndex < 2 || writeIndex > size - 3)
            continue;

        // Delays within a few 1e-5 of the write index, either side.
        for (int j = -40; j <= 40; ++j)
        {
            const float delay = static_cast<float> (writeIndex) + 1.0e-5f * static_cast<float> (j);
            constant = constant && std::abs (line.read (delay) - 1.0f) < 1.0e-4f;
        }
    }

    check (constant, "a delay line full of ones reads ones across the wrap point");
}

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

void testAutoWahRetriggerSoftAfterLoud()
{
    std::printf ("Auto-wah: a soft note after a loud one still retriggers\n");

    // The old detector compared a fast follower against a 150 ms trailing
    // average of itself; after a loud pluck that average stayed high long enough
    // to swallow a quieter note landing ~400 ms later. The onset gate measures
    // the envelope's rise over a fixed 12 ms window instead, so the second hit
    // reads as its own attack regardless of what preceded it.
    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.30f);
    wah.setRange01 (0.9f);
    wah.setFreq01 (0.35f);
    wah.setQ01 (0.55f);
    wah.setMix01 (1.0f);
    wah.setType (1);
    wah.setDecay01 (0.3f);

    const int total = (int) (kSampleRate * 1.0);
    const int b1 = (int) (kSampleRate * 0.15);
    const int b2 = (int) (kSampleRate * 0.55);   // 400 ms after the first
    const int burst = (int) (kSampleRate * 0.06);
    const double w = 2.0 * juce::MathConstants<double>::pi * 300.0 / kSampleRate;

    std::vector<float> buf ((size_t) total);
    for (int n = 0; n < total; ++n)
    {
        const float amp = (n >= b1 && n < b1 + burst) ? 0.60f
                        : (n >= b2 && n < b2 + burst) ? 0.30f   // 6 dB down
                        : 0.0f;
        buf[(size_t) n] = amp * (float) std::sin (w * n);
    }

    const int lo = (int) (kSampleRate * 0.003);
    const int hi = (int) (kSampleRate * 0.055);
    double minAfter2 = 1.0e9;

    for (int off = 0; off < total; off += kBlock)
    {
        const int len = juce::jmin (kBlock, total - off);
        wah.process (buf.data() + off, nullptr, len);
        const int mid = off + len / 2;
        if (mid >= b2 + lo && mid < b2 + hi) minAfter2 = juce::jmin (minAfter2, wah.phase());
    }

    std::printf ("  lowest LFO phase just after the soft hit: %.3f\n", minAfter2);
    check (std::isfinite (minAfter2), "soft-after-loud retrigger phase is finite");
    check (minAfter2 < 0.15, "the soft note restarts the LFO near phase 0");
}

void testAutoWahLowNoteTriggersOnce()
{
    std::printf ("Auto-wah: one hard low-E pluck kicks the sweep exactly once\n");

    // A hard hit on a low string blooms and beats for tens of ms; feeding the
    // onset gate the raw rectified signal (or giving it no refractory time) let
    // that ripple through the window and retriggered the sweep several times on
    // a single note. The gate is fed the smoothed follower and holds off for
    // kOnsetLockoutMs after a hit.
    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.50f);   // slow, so every phase reset is obvious
    wah.setRange01 (0.9f);
    wah.setFreq01 (0.35f);
    wah.setQ01 (0.55f);
    wah.setMix01 (1.0f);
    wah.setType (1);
    wah.setDecay01 (0.3f);          // not latched

    const int total = (int) (kSampleRate * 1.6);
    const int b1 = (int) (kSampleRate * 0.15);
    const double f0 = 82.41;        // low E
    const double w = 2.0 * juce::MathConstants<double>::pi * f0 / kSampleRate;

    std::vector<float> buf ((size_t) total);
    for (int n = 0; n < total; ++n)
    {
        if (n < b1) { buf[(size_t) n] = 0.0f; continue; }
        const double t = (double) (n - b1) / kSampleRate;
        const double attack = 1.0 - std::exp (-t / 0.004);   // ~4 ms rise
        const double decay  = std::exp (-t / 0.9);           // slow ring
        // Fundamental plus two harmonics - the beating between them is what
        // used to re-fire the gate mid-note.
        const double s = std::sin (w * n)
                       + 0.6 * std::sin (2.0 * w * n)
                       + 0.35 * std::sin (3.0 * w * n);
        buf[(size_t) n] = (float) (0.7 * attack * decay * s);
    }

    int retriggers = 0;
    double prev = wah.phase();
    for (int off = 0; off < total; off += kBlock)
    {
        const int len = juce::jmin (kBlock, total - off);
        wah.process (buf.data() + off, nullptr, len);
        const double ph = wah.phase();
        const int mid = off + len / 2;
        // A phase reset is a drop to near 0 from a mid value; a natural LFO wrap
        // comes from up near 1. Only count from the pluck onward.
        if (mid >= b1 && ph < 0.06 && prev > 0.06 && prev < 0.85)
            ++retriggers;
        prev = ph;
    }

    std::printf ("  phase resets during the note: %d\n", retriggers);
    check (retriggers == 1, "a single low-E pluck retriggers the sweep once, not repeatedly");
}

void testAutoWahQuietNoteTriggers()
{
    std::printf ("Auto-wah: a quiet note still retriggers the sweep\n");

    // The onset gate measures the envelope's rise as a fraction of where it
    // started, not an absolute delta, so a softly played note reads as the same
    // attack a loud one does. A fixed absolute threshold used to sit above a
    // quiet note's whole envelope and never fire.
    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.reset();
    wah.setPeriodSeconds (0.30f);
    wah.setRange01 (0.9f);
    wah.setFreq01 (0.35f);
    wah.setQ01 (0.55f);
    wah.setMix01 (1.0f);
    wah.setType (1);
    wah.setDecay01 (0.3f);

    const int total = (int) (kSampleRate * 0.8);
    const int b1 = (int) (kSampleRate * 0.20);
    const int burst = (int) (kSampleRate * 0.06);
    const double w = 2.0 * juce::MathConstants<double>::pi * 300.0 / kSampleRate;

    std::vector<float> buf ((size_t) total);
    for (int n = 0; n < total; ++n)
    {
        const bool on = n >= b1 && n < b1 + burst;
        buf[(size_t) n] = on ? 0.04f * (float) std::sin (w * n) : 0.0f;   // ~ -28 dBFS
    }

    const int lo = (int) (kSampleRate * 0.003);
    const int hi = (int) (kSampleRate * 0.055);
    double minAfter = 1.0e9;

    for (int off = 0; off < total; off += kBlock)
    {
        const int len = juce::jmin (kBlock, total - off);
        wah.process (buf.data() + off, nullptr, len);
        const int mid = off + len / 2;
        if (mid >= b1 + lo && mid < b1 + hi) minAfter = juce::jmin (minAfter, wah.phase());
    }

    std::printf ("  lowest LFO phase just after the quiet hit: %.3f\n", minAfter);
    check (std::isfinite (minAfter), "quiet-note retrigger phase is finite");
    check (minAfter < 0.15, "the quiet note restarts the LFO near phase 0");
}

void testAutoWahHoldsLevel()
{
    std::printf ("Auto-wah: the wet path stays in a sane band, not silent or blown up\n");

    // Measured right across the Type morph and at both ends of Q. The per-tap
    // make-up is deliberately half of full wet-vs-dry parity (see kMakeupDb* in
    // AutoWahConfig.h), so the wet path sits below the dry level on purpose - the
    // band-pass tap most of all, since it keeps the least spectrum. This guards
    // the range, not parity: nothing here may go silent, run away, or turn NaN.
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
            allUp = allUp && ratio > 0.2;
            allSane = allSane && ratio < 1.6;
        }
        std::printf ("\n");
    }

    check (allFinite, "auto-wah level ratios are finite");
    check (allUp, "no Type position drops the wet path to silence");
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
// BitBit Spring - the dispersive spring tank.

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

// --------------------------------------------------------------- ring modulator

void fillRingSine (std::vector<float>& l, std::vector<float>& r, double& phase, double hz)
{
    const double inc = hz / kSampleRate;
    for (int i = 0; i < kBlock; ++i)
    {
        const float s = 0.6f * std::sin (static_cast<float> (phase * 2.0 * juce::MathConstants<double>::pi));
        l[static_cast<size_t> (i)] = s;
        r[static_cast<size_t> (i)] = s;
        phase += inc;
        phase -= std::floor (phase);
    }
}

void testRingModulator()
{
    std::printf ("Ring modulator: silence, bounds, and reproducibility\n");

    // 1. Silence in -> silence out, both modes.
    for (int mode : { 0, 1 })
    {
        ee::dsp::RingModulator ring;
        ring.prepare (kSampleRate);
        ring.setFrequency01 (0.5f);
        ring.setTweak01 (1.0f);
        ring.setMode (mode);
        ring.setLowpass01 (0.4f);

        std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
        float peak = 0.0f;
        bool finite = true;
        for (int b = 0; b < 200; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            ring.process (l.data(), r.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                finite = finite && std::isfinite (l[static_cast<size_t> (i)]);
                peak = juce::jmax (peak, std::abs (l[static_cast<size_t> (i)]));
            }
        }
        check (finite && peak < 1.0e-6f, "silent input stays silent");
    }

    // 2. A unit-ish sine in stays finite and bounded across a mode/knob sweep -
    //    the carrier only multiplies, so nothing should grow past the input
    //    (plus the octave make-up headroom in Green Lantern).
    {
        float worst = 0.0f;
        bool finite = true;
        for (int mode : { 0, 1 })
            for (float f : { 0.0f, 0.3f, 0.6f, 1.0f })
                for (float tw : { 0.0f, 0.5f, 1.0f })
                    for (float lp : { 0.0f, 0.5f, 1.0f })
                    {
                        ee::dsp::RingModulator ring;
                        ring.prepare (kSampleRate);
                        ring.setMode (mode);
                        ring.setFrequency01 (f);
                        ring.setTweak01 (tw);
                        ring.setLowpass01 (lp);

                        std::vector<float> l (kBlock), r (kBlock);
                        double phase = 0.0;
                        for (int b = 0; b < 120; ++b)
                        {
                            fillRingSine (l, r, phase, 220.0);
                            ring.process (l.data(), r.data(), kBlock);
                            for (int i = 0; i < kBlock; ++i)
                            {
                                finite = finite && std::isfinite (l[static_cast<size_t> (i)]);
                                worst = juce::jmax (worst, std::abs (l[static_cast<size_t> (i)]));
                            }
                        }
                    }
        check (finite, "output stays finite for a sine input");
        check (worst < 2.0f, "output stays bounded (worst " + juce::String (worst, 3) + ")");
    }

    // 3. Two identical renders are bit-for-bit equal - the engine has no RNG,
    //    so a render can be checksummed.
    {
        auto render = [] (std::vector<float>& out)
        {
            ee::dsp::RingModulator ring;
            ring.prepare (kSampleRate);
            ring.setMode (1);
            ring.setFrequency01 (0.42f);
            ring.setTweak01 (0.7f);
            ring.setLowpass01 (0.55f);

            std::vector<float> l (kBlock), r (kBlock);
            double phase = 0.0;
            out.clear();
            for (int b = 0; b < 200; ++b)
            {
                fillRingSine (l, r, phase, 196.0);
                ring.process (l.data(), r.data(), kBlock);
                out.insert (out.end(), l.begin(), l.end());
            }
        };

        std::vector<float> a, b;
        render (a);
        render (b);
        check (a == b, "two identical renders match sample for sample");
    }
}

// ------------------------------------------------------------------------ rust

void testRust()
{
    std::printf ("Rust: silence, bounds, reproducibility, wear as a memory, mode Tone scaling\n");

    // 1. Silence in -> silence out, both modes, everything up.
    for (int mode : { 0, 1 })
    {
        ee::dsp::Rust rust;
        rust.prepare (kSampleRate);
        rust.setGrind01 (1.0f);
        rust.setTone01 (0.5f);
        rust.setMode (mode);

        std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
        float peak = 0.0f;
        bool finite = true;
        for (int b = 0; b < 400; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            rust.process (l.data(), r.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                finite = finite && std::isfinite (l[static_cast<size_t> (i)]);
                peak = juce::jmax (peak, std::abs (l[static_cast<size_t> (i)]));
            }
        }
        check (finite && peak < 1.0e-6f, "silent input stays silent");
    }

    // 2. A sine in stays finite and bounded across a mode / knob sweep.
    {
        float worst = 0.0f;
        bool finite = true;
        for (int mode : { 0, 1 })
            for (float grind : { 0.0f, 0.5f, 1.0f })
                for (float tone : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    ee::dsp::Rust rust;
                    rust.prepare (kSampleRate);
                    rust.setMode (mode);
                    rust.setGrind01 (grind);
                    rust.setTone01 (tone);

                    std::vector<float> l (kBlock), r (kBlock);
                    double phase = 0.0;
                    for (int b = 0; b < 240; ++b)
                    {
                        fillRingSine (l, r, phase, 220.0);
                        rust.process (l.data(), r.data(), kBlock);
                        for (int i = 0; i < kBlock; ++i)
                        {
                            finite = finite && std::isfinite (l[static_cast<size_t> (i)]);
                            worst = juce::jmax (worst, std::abs (l[static_cast<size_t> (i)]));
                        }
                    }
                }
        check (finite, "output stays finite for a sine input");
        check (worst < 2.0f, "output stays bounded (worst " + juce::String (worst, 3) + ")");
    }

    // 3. Two identical renders are bit-for-bit equal - the only RNG is the
    //    warble walk, fixed-seeded in reset(), so a render can be checksummed.
    {
        auto render = [] (std::vector<float>& out)
        {
            ee::dsp::Rust rust;
            rust.prepare (kSampleRate);
            rust.setMode (1);
            rust.setGrind01 (0.7f);
            rust.setTone01 (0.6f);

            std::vector<float> l (kBlock), r (kBlock);
            double phase = 0.0;
            out.clear();
            for (int b = 0; b < 300; ++b)
            {
                fillRingSine (l, r, phase, 196.0);
                rust.process (l.data(), r.data(), kBlock);
                out.insert (out.end(), l.begin(), l.end());
            }
        };

        std::vector<float> a, b;
        render (a);
        render (b);
        check (a == b, "two identical renders match sample for sample");
    }

    // 4. Wear is a memory: driven with a steady loud tone the output diverges
    //    from the input more as time goes on (the corrosion builds), then heals
    //    back toward the input once the tone stops.
    {
        ee::dsp::Rust rust;
        rust.prepare (kSampleRate);
        rust.setMode (0); // Oxide
        rust.setGrind01 (0.8f);
        rust.setTone01 (0.9f);

        std::vector<float> l (kBlock), r (kBlock), dry (kBlock);
        double phase = 0.0, dryPhase = 0.0;

        auto meanAbsDiff = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            double acc = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                acc += std::abs (a[i] - b[i]);
            return static_cast<float> (acc / static_cast<double> (a.size()));
        };

        float earlyDiff = 0.0f, lateDiff = 0.0f;
        const int blocksPerSecond = static_cast<int> (kSampleRate / kBlock);

        for (int b = 0; b < blocksPerSecond * 4; ++b)
        {
            fillRingSine (l, r, phase, 110.0);
            fillRingSine (dry, dry, dryPhase, 110.0); // same tone, untouched
            rust.process (l.data(), r.data(), kBlock);

            if (b == 2)
                earlyDiff = meanAbsDiff (l, dry);
            if (b == blocksPerSecond * 3)
                lateDiff = meanAbsDiff (l, dry);
        }

        check (lateDiff > 0.02f && lateDiff > earlyDiff * 2.0f,
               "wear builds: late diff " + juce::String (lateDiff, 4) + " >> early diff "
                   + juce::String (earlyDiff, 4));

        // Now silence for long enough to heal, then a quiet probe should pass
        // near-clean.
        for (int b = 0; b < blocksPerSecond * 6; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            rust.process (l.data(), r.data(), kBlock);
        }

        // Measure the probe in its first few ms, before the probe itself has
        // had time to rust the path again (the attack is hundreds of ms).
        float probeDiff = 0.0f;
        double probePhase = 0.0, probeDryPhase = 0.0;
        for (int b = 0; b < 8; ++b)
        {
            fillRingSine (l, r, probePhase, 110.0);
            fillRingSine (dry, dry, probeDryPhase, 110.0);
            rust.process (l.data(), r.data(), kBlock);
            if (b == 2)
                probeDiff = meanAbsDiff (l, dry);
        }

        check (probeDiff < lateDiff * 0.5f,
               "wear heals: probe diff " + juce::String (probeDiff, 4) + " << driven diff "
                   + juce::String (lateDiff, 4));
    }

    // 5. At a real guitar level (~0.2, well below a normalised sine) and the
    //    factory knob positions, sustained playing must audibly corrode the
    //    signal - a continuous difference, not the odd sparse click. This is
    //    the regression for the detector that tracked the waveform troughs and
    //    so never let wear accumulate: the engine passed the guitar through
    //    almost untouched.
    {
        ee::dsp::Rust eng;
        eng.prepare (kSampleRate);
        eng.setMode (0); // Oxide
        eng.setGrind01 (ee::dsp::rust::kDefaultGrindPct * 0.01f);
        eng.setTone01 (ee::dsp::rust::kDefaultTonePct * 0.01f);

        std::vector<float> l (kBlock), r (kBlock), dry (kBlock);
        double phase = 0.0, dryPhase = 0.0;

        double diffSq = 0.0, inSq = 0.0;
        long n = 0;
        const int blocksPerSecond = static_cast<int> (kSampleRate / kBlock);

        for (int b = 0; b < blocksPerSecond * 2; ++b)
        {
            fillRingSine (l, r, phase, 130.0);
            fillRingSine (dry, dry, dryPhase, 130.0);
            for (int i = 0; i < kBlock; ++i) // knock 0.6 down to a guitar-ish 0.2
            {
                l[static_cast<size_t> (i)] *= 0.33f;
                r[static_cast<size_t> (i)] *= 0.33f;
                dry[static_cast<size_t> (i)] *= 0.33f;
            }
            eng.process (l.data(), r.data(), kBlock);

            if (b >= blocksPerSecond) // measure only after wear has had a second to build
                for (int i = 0; i < kBlock; ++i)
                {
                    const double d = l[static_cast<size_t> (i)] - dry[static_cast<size_t> (i)];
                    diffSq += d * d;
                    inSq += dry[static_cast<size_t> (i)] * dry[static_cast<size_t> (i)];
                    ++n;
                }
        }

        const float diffRms = static_cast<float> (std::sqrt (diffSq / static_cast<double> (n)));
        const float inRms   = static_cast<float> (std::sqrt (inSq / static_cast<double> (n)));
        check (diffRms > 0.2f * inRms,
               "default patch corrodes a guitar-level signal: diff rms " + juce::String (diffRms, 4)
                   + " vs input rms " + juce::String (inRms, 4));
    }

    // 6. Neither mode adds anything on top of the signal. Drive it hard, then
    //    feed silence: the output must be at the noise floor almost at once -
    //    every stage shapes the input, there is no crackle / hiss / dropout
    //    generator in either voicing.
    for (int mode : { 0, 1 })
    {
        ee::dsp::Rust rust;
        rust.prepare (kSampleRate);
        rust.setMode (mode);
        rust.setGrind01 (1.0f);
        rust.setTone01 (0.6f);

        std::vector<float> l (kBlock), r (kBlock);
        double phase = 0.0;
        const int blocksPerSecond = static_cast<int> (kSampleRate / kBlock);

        for (int b = 0; b < blocksPerSecond * 2; ++b) // 2 s of loud tone
        {
            fillRingSine (l, r, phase, 120.0);
            rust.process (l.data(), r.data(), kBlock);
        }

        float tailPeak = 0.0f;
        for (int b = 0; b < blocksPerSecond; ++b) // 1 s of silence after
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            rust.process (l.data(), r.data(), kBlock);

            if (b >= blocksPerSecond / 2) // measure the second half-second
                for (int i = 0; i < kBlock; ++i)
                    tailPeak = juce::jmax (tailPeak, std::abs (l[static_cast<size_t> (i)]));
        }

        check (tailPeak < 1.0e-3f,
               juce::String (mode == 0 ? "Oxide" : "Contact")
                   + " tail is silent within a second of the input stopping (peak "
                   + juce::String (tailPeak, 6) + ")");
    }

    // 7. Contact is darker than Oxide at the same Tone knob - the engine scales
    //    the knob down before the map. Feed quiet white noise (Grind 0, wear low
    //    enough that the Contact clip stays open) and compare the high-frequency
    //    content: Contact's should be clearly lower.
    {
        auto renderNoiseHf = [] (int mode) -> float
        {
            ee::dsp::Rust rust;
            rust.prepare (kSampleRate);
            rust.setMode (mode);
            rust.setGrind01 (0.0f);
            rust.setTone01 (0.8f);

            juce::Random rng (2001);
            std::vector<float> l (kBlock), r (kBlock);
            double diffSq = 0.0;
            long n = 0;
            const int blocksPerSecond = static_cast<int> (kSampleRate / kBlock);

            for (int b = 0; b < blocksPerSecond * 2; ++b)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const float s = (rng.nextFloat() * 2.0f - 1.0f) * 0.05f;
                    l[static_cast<size_t> (i)] = s;
                    r[static_cast<size_t> (i)] = s;
                }
                rust.process (l.data(), r.data(), kBlock);

                if (b >= blocksPerSecond) // after the tone LP has settled
                    for (int i = 1; i < kBlock; ++i)
                    {
                        const double d = l[static_cast<size_t> (i)] - l[static_cast<size_t> (i - 1)];
                        diffSq += d * d;
                        ++n;
                    }
            }
            return static_cast<float> (std::sqrt (diffSq / static_cast<double> (n)));
        };

        const float oxideHf   = renderNoiseHf (0);
        const float contactHf = renderNoiseHf (1);
        check (contactHf < oxideHf * 0.9f,
               "Contact Tone lands darker than Oxide: HF " + juce::String (contactHf, 5) + " vs "
                   + juce::String (oxideHf, 5));
    }
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

//==============================================================================
// Grainer
//
// Two things to guard. The buffer bookkeeping - a live grain overtaking the
// write head, or walking off the old end - shows up as a wrap discontinuity
// rather than an explosion, hence the click test. And the feedback path, which
// means the engine is no longer feed-forward and could in principle latch a
// non-finite value or build without bound; the freeze/stretch tests exercise
// the frozen read head on top.

struct GrainerRun
{
    float peak = 0.0f;
    float rms = 0.0f;
    float maxJump = 0.0f;
    int maxActive = 0;
    bool finite = true;
};

/** Runs `seconds` of a generated input through the engine and measures it.
    `source` is called per sample; return silence to measure a tail. */
template <typename Source>
GrainerRun runGrainer (float sizeMs, float densityHz, float timeMs, float pitch, double seconds, Source&& source,
                       float feedback = 0.0f, bool freeze = false, float stretch = 1.0f,
                       int shapeFamily = ee::dsp::Grainer::kShapeTriangle, float shape = -1.0f)
{
    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (sizeMs);
    grainer.setDensityHz (densityHz);
    grainer.setTimeMs (timeMs);
    grainer.setFeedback (feedback);
    grainer.setStretch (stretch);
    grainer.setFreeze (freeze);
    grainer.setShapeFamily (shapeFamily);
    if (shape >= 0.0f)
        grainer.setShape (shape);

    // The helper keeps one `pitch` axis for brevity: -1 is the low group only,
    // 0 unison, +1 the high group only.
    grainer.setPitchMix (juce::jmax (0.0f, -pitch), 1.0f - std::abs (pitch), juce::jmax (0.0f, pitch));

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    GrainerRun result;
    double sumSquares = 0.0;
    long long counted = 0;
    float previous = 0.0f;
    int n = 0;

    const int blocks = static_cast<int> (kSampleRate * seconds / kBlock);

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const float s = source (n++);
            inL[static_cast<size_t> (i)] = s;
            inR[static_cast<size_t> (i)] = s;
        }

        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        result.maxActive = juce::jmax (result.maxActive, grainer.getActiveGrains());

        for (int i = 0; i < kBlock; ++i)
        {
            const float l = outL[static_cast<size_t> (i)];
            const float r = outR[static_cast<size_t> (i)];

            if (! std::isfinite (l) || ! std::isfinite (r))
                result.finite = false;

            result.peak = juce::jmax (result.peak, juce::jmax (std::abs (l), std::abs (r)));
            result.maxJump = juce::jmax (result.maxJump, std::abs (l - previous));
            previous = l;

            sumSquares += static_cast<double> (l) * l;
            ++counted;
        }
    }

    result.rms = counted > 0 ? static_cast<float> (std::sqrt (sumSquares / static_cast<double> (counted))) : 0.0f;
    return result;
}

void testGrainerSilence()
{
    std::printf ("Grainer on a silent input:\n");

    const auto run = runGrainer (120.0f, 40.0f, 400.0f, 1.0f, 5.0, [] (int) { return 0.0f; });

    std::printf ("  peak out of silence: %.3g\n", run.peak);
    check (run.peak == 0.0f, "grainer generated signal from silence");
}

void testGrainerFiniteUnderSweep()
{
    std::printf ("Grainer swept over its whole range under full-scale noise:\n");

    std::mt19937 rng (2024);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const float sizes[] = { 20.0f, 60.0f, 120.0f, 300.0f, 500.0f };
    const float densities[] = { 1.0f, 5.0f, 12.0f, 25.0f, 40.0f };
    const float times[] = { 20.0f, 100.0f, 800.0f, 2000.0f };
    const float pitches[] = { -1.0f, -0.4f, 0.0f, 0.4f, 1.0f };

    bool finite = true;
    float worstPeak = 0.0f;
    int worstActive = 0;

    for (float size : sizes)
        for (float density : densities)
            for (float timeMs : times)
                for (float pitch : pitches)
                {
                    const auto run = runGrainer (size, density, timeMs, pitch, 0.5,
                                                 [&rng, &noise] (int) { return noise (rng); });

                    finite = finite && run.finite;
                    worstPeak = juce::jmax (worstPeak, run.peak);
                    worstActive = juce::jmax (worstActive, run.maxActive);
                }

    std::printf ("  worst peak %.3g, most grains at once %d of %d\n",
                 worstPeak, worstActive, ee::dsp::config::kMaxGrains);

    check (finite, "grain sweep produced NaN or Inf");
    check (worstPeak < 4.0f, "grain sweep ran away (peak " + juce::String (worstPeak, 2) + ")");
    check (worstActive <= ee::dsp::config::kMaxGrains, "the grain pool overflowed");
}

/** The same sweep as testGrainerFiniteUnderSweep, over the three Shape Family
    windows testGrainerFiniteUnderSweep never touches (it runs the Triangle
    default throughout). Each is evaluated per sample rather than stepped by a
    multiply (see Grainer::envelopeOf), so this is exactly the coverage that
    would catch one of them going non-finite or unbounded somewhere across
    Shape's own travel - a bad exponent in Gaussian or Spike, a divide by
    zero at Sinc's centre. */
void testGrainerShapeFamiliesAreFiniteUnderSweep()
{
    std::printf ("Grainer: the three Shape Family windows, swept under full-scale noise:\n");

    std::mt19937 rng (2025);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const int families[] = { ee::dsp::Grainer::kShapeGaussian, ee::dsp::Grainer::kShapeSinc,
                             ee::dsp::Grainer::kShapeSpike };
    const float shapes[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    const float sizes[] = { 20.0f, 120.0f, 500.0f };
    const float densities[] = { 2.0f, 12.0f, 30.0f };

    static const char* const names[] = { "Gaussian", "Sinc", "Spike" };

    for (int f = 0; f < 3; ++f)
    {
        bool finite = true;
        float worstPeak = 0.0f;

        for (float shape : shapes)
            for (float size : sizes)
                for (float density : densities)
                {
                    const auto run = runGrainer (size, density, 300.0f, 0.0f, 0.5,
                                                 [&rng, &noise] (int) { return noise (rng); },
                                                 0.0f, false, 1.0f, families[f], shape);

                    finite = finite && run.finite;
                    worstPeak = juce::jmax (worstPeak, run.peak);
                }

        std::printf ("  %-9s worst peak %.3g\n", names[f], worstPeak);
        check (finite, juce::String (names[f]) + " produced NaN or Inf somewhere across Shape");

        // A ceiling checking for runaway, not for loudness: familyEnvelopeRms
        // level-matches every family by its RMS, on purpose - Spike's whole
        // character is the same average energy concentrated into a much
        // narrower instant, so a higher crest factor here is the shape doing
        // its job, not a bug. 8x (+18 dB) is still nowhere near indicating an
        // actual runaway (unbounded growth, not a loud transient), and this is
        // the raw engine - BitBitGrainProcessor::outputLimiter is what a real
        // signal path has downstream of exactly this.
        check (worstPeak < 8.0f, juce::String (names[f]) + " ran away (peak " + juce::String (worstPeak, 2) + ")");
    }
}

/** Every Shape Family window is defined to reach exactly 0 at a grain's first
    and last sample (see Grainer::familyEnvelope's own note) - what stops a
    Family switch from clicking. Checked here directly rather than through a
    click-detector on the summed cloud, because with more than one grain
    active the edges of one are covered by the middle of another; one grain at
    a time, sized to outlast the block, isolates each edge instead. */
void testGrainerShapeFamiliesCloseToZero()
{
    std::printf ("Grainer: every Shape Family window opens and closes at zero:\n");

    const int families[] = { ee::dsp::Grainer::kShapeGaussian, ee::dsp::Grainer::kShapeSinc,
                             ee::dsp::Grainer::kShapeSpike };
    static const char* const names[] = { "Gaussian", "Sinc", "Spike" };

    for (int f = 0; f < 3; ++f)
    {
        for (float shape : { 0.0f, 0.5f, 1.0f })
        {
            ee::dsp::Grainer grainer;
            grainer.prepare (kSampleRate);
            grainer.reset();
            grainer.setSizeMs (400.0f); // one grain comfortably outlasts one block
            grainer.setDensityHz (1.0f);
            grainer.setTimeMs (100.0f);
            grainer.setShapeFamily (families[f]);
            grainer.setShape (shape);

            auto tuning = grainer.getTuning();
            tuning.windowJitter = 0.0f; // isolate the family's own formula from the stray
            grainer.setTuning (tuning);

            std::vector<float> in (kBlock, 0.6f), l (kBlock), r (kBlock);
            float firstSpawnSample = -1.0f;
            float peak = 0.0f;
            int sinceFirstActive = -1;

            for (int b = 0; b < 200; ++b)
            {
                grainer.process (in.data(), in.data(), l.data(), r.data(), kBlock);

                if (sinceFirstActive < 0 && grainer.getActiveGrains() > 0)
                {
                    firstSpawnSample = std::abs (l[0]);
                    sinceFirstActive = 0;
                }

                for (int i = 0; i < kBlock; ++i)
                    peak = juce::jmax (peak, std::abs (l[static_cast<size_t> (i)]));

                if (sinceFirstActive >= 0)
                    ++sinceFirstActive;

                if (grainer.getActiveGrains() == 0 && sinceFirstActive > 0)
                    break; // the one grain has run its course
            }

            std::printf ("  %-9s shape %.1f: first sample %.2e, peak %.3f\n", names[f], shape,
                         firstSpawnSample, peak);
            check (firstSpawnSample >= 0.0f && firstSpawnSample < 1.0e-3f,
                   juce::String (names[f]) + " clicks on: its first sample is not silent");
        }
    }
}

/** The same failure testGrainerShapeChangeDoesNotJumpTheLevel guards for
    Triangle, for the three windows it never touches: switching Shape Family -
    the dropdown below the knob, not the knob itself - must not make grains
    already sounding (or the next ones spawned) jump in level, because
    familyEnvelopeRms corrects for it the same way the closed form does for
    Triangle. Run at fixed Shape (0.5) so only Family is moving. */
void testGrainerShapeFamilyChangeDoesNotJumpTheLevel()
{
    std::printf ("Grainer Shape Family: switching windows does not jump the level:\n");

    const int families[] = { ee::dsp::Grainer::kShapeTriangle, ee::dsp::Grainer::kShapeGaussian,
                             ee::dsp::Grainer::kShapeSinc, ee::dsp::Grainer::kShapeSpike };
    static const char* const names[] = { "Triangle", "Gaussian", "Sinc", "Spike" };

    auto steadyRms = [] (int family) -> double
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (300.0f);
        grainer.setDensityHz (8.0f);
        grainer.setTimeMs (300.0f);
        grainer.setShapeFamily (family);
        grainer.setShape (0.5f);

        std::vector<float> in (kBlock), l (kBlock), r (kBlock);
        double sumSq = 0.0;
        long long n = 0;
        double phase = 0.0;
        const double w = 2.0 * juce::MathConstants<double>::pi * 300.0 / kSampleRate;

        // 6 s to reach a steady cloud, measure the last 2.
        const int blocks = static_cast<int> (kSampleRate * 6.0 / kBlock);
        const int measureFrom = static_cast<int> (kSampleRate * 4.0 / kBlock);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                in[static_cast<size_t> (i)] = 0.1f * static_cast<float> (std::sin (phase));
                phase += w;
            }

            grainer.process (in.data(), in.data(), l.data(), r.data(), kBlock);

            if (b >= measureFrom)
                for (int i = 0; i < kBlock; ++i)
                {
                    sumSq += static_cast<double> (l[static_cast<size_t> (i)]) * l[static_cast<size_t> (i)];
                    ++n;
                }
        }

        return std::sqrt (sumSq / juce::jmax (1LL, n));
    };

    double rms[4];
    for (int f = 0; f < 4; ++f)
        rms[f] = steadyRms (families[f]);

    const double loudest = *std::max_element (rms, rms + 4);
    const double quietest = *std::min_element (rms, rms + 4);

    for (int f = 0; f < 4; ++f)
        std::printf ("  %-9s steady rms %.4f\n", names[f], rms[f]);

    std::printf ("  spread: %+.1f dB (loudest against quietest)\n", 20.0 * std::log10 (loudest / quietest));

    // A generous ceiling: familyEnvelopeRms corrects each family to the same
    // reference, but the correction is Shape's own mean, not a per-grain
    // guarantee, and the families are different enough in character (Sinc
    // rings, Spike is spikier) that a little spread is real, not a bug. What
    // this actually guards is a family with no compensation at all - which
    // reads as several dB, not a fraction of one.
    check (loudest < quietest * 2.0, "a Shape Family window is not level-matched against the others");
}

void testGrainerReadsStayBehindTheWriteHead()
{
    std::printf ("Grainer on a pure tone, hunting for a wrap discontinuity:\n");

    // A grain that overtakes the write head, or that runs off the old end of
    // the buffer, reads a sample from an unrelated part of the recording. On a
    // windowed sine that lands as a step, and nothing else here can make one.
    const auto tone = [] (int n)
    { return 0.7f * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * n / kSampleRate); };

    float worstRatio = 0.0f;

    // The extremes are what the offset arithmetic has to survive: longest
    // grains pitched an octave up (the most source spanned) and the deepest
    // spray with the reversals that come with it.
    const float pitches[] = { 0.0f, 1.0f, -1.0f };
    const float times[] = { 20.0f, 2000.0f };

    for (float pitch : pitches)
        for (float timeMs : times)
        {
            const auto run = runGrainer (500.0f, 30.0f, timeMs, pitch, 4.0, tone);
            const float ratio = run.peak > 0.0f ? run.maxJump / run.peak : 0.0f;
            worstRatio = juce::jmax (worstRatio, ratio);
        }

    std::printf ("  worst sample-to-sample jump: %.1f %% of peak\n", worstRatio * 100.0f);

    // A 200 Hz tone moves under 3 % of its peak per sample at 48 k. Overlapping
    // grains at spread pitches raise that, but a genuine wrap error is a step
    // of most of the peak.
    check (worstRatio < 0.25f, "grain reads crossed a buffer boundary (jump "
                                   + juce::String (worstRatio * 100.0f, 1) + " % of peak)");
}

void testGrainerLevelHoldsAcrossDensity()
{
    std::printf ("Grainer output level across the Density range:\n");

    std::mt19937 rng (99);
    std::uniform_real_distribution<float> noise (-0.7f, 0.7f);

    // Only the part of the range where grains actually overlap: below one
    // grain at a time there is silence between them and the RMS is meant to
    // fall away.
    const float densities[] = { 10.0f, 20.0f, 30.0f, 40.0f };

    float loudest = 0.0f;
    float quietest = 1.0e9f;

    for (float density : densities)
    {
        const auto run = runGrainer (120.0f, density, 400.0f, 0.0f, 3.0,
                                     [&rng, &noise] (int) { return noise (rng); });

        std::printf ("  %5.1f /s -> rms %.4f\n", density, run.rms);

        loudest = juce::jmax (loudest, run.rms);
        quietest = juce::jmin (quietest, run.rms);
    }

    const float spreadDb = juce::Decibels::gainToDecibels (loudest / juce::jmax (1.0e-9f, quietest));
    std::printf ("  spread: %.1f dB\n", spreadDb);

    // Density is a texture control, not a volume control. Some drift is honest
    // - the grains are correlated fragments of one source, and the attack share
    // makes most of them fragments of the *same* moment of it, which correlates
    // them further - but it must not read as turning the pedal up.
    check (spreadDb < 5.0f, "Density doubles as a volume knob (" + juce::String (spreadDb, 1) + " dB)");
}

/** Runs a burst of noise, then silence, and returns how long the cloud kept
    going - the time until the output stayed under `floorLevel`. Also reports
    whether every sample stayed finite and the worst peak seen. */
struct TailRun
{
    float seconds = 0.0f;
    float peak = 0.0f;
    bool finite = true;
};

TailRun grainerTailRun (float timeMs, float feedback)
{
    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (120.0f);
    grainer.setDensityHz (20.0f);
    grainer.setTimeMs (timeMs);
    grainer.setFeedback (feedback);

    std::mt19937 rng (11);
    std::uniform_real_distribution<float> noise (-0.7f, 0.7f);

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    for (int b = 0; b < static_cast<int> (kSampleRate * 1.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }

    std::fill (inL.begin(), inL.end(), 0.0f);
    std::fill (inR.begin(), inR.end(), 0.0f);

    constexpr float floorLevel = 0.002f;
    TailRun result;

    const int blocks = static_cast<int> (kSampleRate * 20.0 / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            const float s = outL[static_cast<size_t> (i)];
            if (! std::isfinite (s))
                result.finite = false;
            result.peak = juce::jmax (result.peak, std::abs (s));
            if (std::abs (s) > floorLevel)
                result.seconds = static_cast<float> (b * kBlock + i) / static_cast<float> (kSampleRate);
        }
    }

    return result;
}

void testGrainerFeedbackLengthensTail()
{
    std::printf ("Grainer tail length across the Feedback range:\n");

    // Feedback recirculates the granulated output, so a higher setting has to
    // ring for longer after the input stops - and stay finite and bounded doing
    // it, which is the guard the feed-forward engine never needed.
    const float feedbacks[] = { 0.0f, 0.3f, 0.6f, 0.85f };

    float dryTail = 0.0f;
    float longest = -1.0f;
    bool finite = true;
    float worstPeak = 0.0f;

    for (float fb : feedbacks)
    {
        const auto run = grainerTailRun (300.0f, fb);
        std::printf ("  fb %.2f -> tail %.2f s (peak %.3g)\n", fb, run.seconds, run.peak);

        if (fb == 0.0f)
            dryTail = run.seconds;
        longest = juce::jmax (longest, run.seconds);
        finite = finite && run.finite;
        worstPeak = juce::jmax (worstPeak, run.peak);
    }

    check (finite, "the Feedback tail went non-finite");
    check (worstPeak < 4.0f, "the Feedback tail ran away (peak " + juce::String (worstPeak, 2) + ")");
    check (longest > dryTail + 0.3f, "more Feedback did not make a longer tail");
}

void testGrainerAttackCapture()
{
    std::printf ("Grainer attack capture:\n");

    // A plucked string: a loud attack and then a long, quiet ring. Without
    // attack capture the cloud is built from whatever the window reaches,
    // which after the first moment is all sustain - and the cloud fades with
    // the note. Drawing most grains from the attack keeps it present for as
    // long as the string is still sounding, which is the point of the feature.
    const auto pluck = [] (int n)
    {
        const double t = static_cast<double> (n) / kSampleRate;
        return 0.9f * static_cast<float> (std::exp (-1.2 * t)
                                          * std::sin (2.0 * juce::MathConstants<double>::pi * 196.0 * t));
    };

    const auto sustainLevel = [&pluck] (float attackShare)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (120.0f);
        grainer.setDensityHz (20.0f);
        grainer.setTimeMs (400.0f);
        grainer.setFeedback (0.0f); // this test is about attack capture, not the feedback tail

        auto tuning = grainer.getTuning();
        tuning.attackShare = attackShare;
        grainer.setTuning (tuning);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

        double squares = 0.0;
        long long counted = 0;
        int n = 0;

        const int blocks = static_cast<int> (kSampleRate * 3.0 / kBlock);
        const int measureFrom = static_cast<int> (kSampleRate * 1.0 / kBlock);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = pluck (n++);

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

            if (b < measureFrom)
                continue;

            for (int i = 0; i < kBlock; ++i)
            {
                squares += static_cast<double> (outL[static_cast<size_t> (i)]) * outL[static_cast<size_t> (i)];
                ++counted;
            }
        }

        return counted > 0 ? static_cast<float> (std::sqrt (squares / static_cast<double> (counted))) : 0.0f;
    };

    const float without = sustainLevel (0.0f);
    const float with = sustainLevel (0.9f);

    std::printf ("  cloud level over the ring: without %.4f, with %.4f (%.1f dB up)\n", without, with,
                 juce::Decibels::gainToDecibels (with / juce::jmax (1.0e-9f, without)));

    check (with > without * 1.2f, "drawing grains from the attack did not keep the cloud present");
}

void testGrainerGridFrozenCapturesEachBar()
{
    std::printf ("Grainer Grid, frozen (bar-locked capture):\n");

    // 120 bpm, so a sixteenth is 6000 samples and a bar 96000. Every sixteenth
    // is its own tone, and the downbeat's tone changes every bar - so which
    // tone dominates the cloud says exactly which slice the grains replay.
    // Bar-locked, each bar should sing its own downbeat for the whole bar.
    // Plain Freeze on a steady input has no onsets to retrigger on after the
    // first, so it keeps looping bar 0 and must not.
    const double ppqPerSample = 2.0 / kSampleRate;
    const int sixteenth = static_cast<int> (0.25 / ppqPerSample);
    const int bar = 16 * sixteenth;
    const auto downbeatHz = [] (int b) { return 1100.0 + 200.0 * b; };

    const auto source = [&] (int n)
    {
        const int b = n / bar;
        const int k = (n % bar) / sixteenth;
        const double hz = k == 0 ? downbeatHz (b) : 300.0 + 40.0 * k;
        return 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / kSampleRate));
    };

    // Share of the three candidate downbeat tones that belongs to bar b's own.
    const auto ownDownbeatShare = [&] (bool grid, int b)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (100.0f);
        grainer.setDensityHz (8.0f);
        grainer.setTimeMs (20.0f);
        grainer.setFeedback (0.0f);
        grainer.setScatter (0.0f);
        grainer.setReverse (0.0f);
        grainer.setStereo (0.0f);
        grainer.setFreeze (true);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        std::vector<float> measured;

        // Skip the first 300 ms of the bar, where the downbeat slice is still
        // being played in live, and the tail of the last grain.
        const int from = b * bar + static_cast<int> (0.3 * kSampleRate);
        const int to = (b + 1) * bar - sixteenth;

        for (int n = 0; n < to; n += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = source (n + i);

            ee::dsp::Grainer::Transport transport;
            transport.synced = true;
            transport.playing = true;
            transport.ppqStart = n * ppqPerSample;
            transport.cyclesPerQuarter = 4.0;
            transport.ppqPerSample = ppqPerSample;
            transport.grid = grid;

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock, transport);

            for (int i = 0; i < kBlock; ++i)
                if (n + i >= from && n + i < to)
                    measured.push_back (outL[static_cast<size_t> (i)]);
        }

        const double own = goertzelPower (measured, downbeatHz (b), kSampleRate);
        double all = 0.0;
        for (int other = 0; other <= 2; ++other)
            all += goertzelPower (measured, downbeatHz (other), kSampleRate);
        return own / juce::jmax (1.0e-30, all);
    };

    for (int b = 1; b <= 2; ++b)
    {
        const double locked = ownDownbeatShare (true, b);
        const double onsets = ownDownbeatShare (false, b);
        std::printf ("  bar %d: its own downbeat's share of the cloud - Grid %.3f, plain Freeze %.3f\n", b,
                     locked, onsets);
        check (locked > 0.9, "Grid did not replay this bar's downbeat");
        check (onsets < 0.5, "plain Freeze followed the bar line - the control no longer controls anything");
    }
}

void testGrainerGridLiveTapsWholeSixteenths()
{
    std::printf ("Grainer Grid, live (the Time tap in whole sixteenths):\n");

    // 120 bpm again, one tone per sixteenth. Time is 300 ms - 2.4 sixteenths,
    // deliberately off the grid. With Grid on it rounds to exactly two, so a
    // grain spawned on sixteenth j plays nothing but sixteenth j-2's tone.
    // Off, the tap straddles the line between j-3 and j-2 and every grain is
    // a mix of both. Attack capture is off: the input has no attacks worth
    // the name, and Grid leaves attack grains alone by design.
    const double ppqPerSample = 2.0 / kSampleRate;
    const int sixteenth = static_cast<int> (0.25 / ppqPerSample);
    const auto toneHz = [] (int j) { return 400.0 + 100.0 * (((j % 12) + 12) % 12); };

    const auto source = [&] (int n)
    {
        return 0.5f
               * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * toneHz (n / sixteenth) * n
                                               / kSampleRate));
    };

    const int slots = 40;

    // The grain spawned on each sixteenth j from 8 on, rendered.
    const auto render = [&] (bool grid, float scatter)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (90.0f);
        grainer.setDensityHz (8.0f);
        grainer.setTimeMs (300.0f);
        grainer.setFeedback (0.0f);
        grainer.setScatter (scatter);
        grainer.setReverse (0.0f);
        grainer.setStereo (0.0f);

        auto tuning = grainer.getTuning();
        tuning.attackShare = 0.0f;
        // This test is about the Time tap, which followDensity replaces while
        // the transport plays - see testGrainerFollowDensityReadsOnTheGrid.
        tuning.followDensity = 0.0f;
        grainer.setTuning (tuning);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        std::vector<float> rendered;
        rendered.reserve (static_cast<size_t> (slots * sixteenth + kBlock));

        for (int n = 0; n < slots * sixteenth; n += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = source (n + i);

            ee::dsp::Grainer::Transport transport;
            transport.synced = true;
            transport.playing = true;
            transport.ppqStart = n * ppqPerSample;
            transport.cyclesPerQuarter = 4.0;
            transport.ppqPerSample = ppqPerSample;
            transport.grid = grid;

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock, transport);
            rendered.insert (rendered.end(), outL.begin(), outL.end());
        }

        return rendered;
    };

    const auto grainOf = [&] (const std::vector<float>& rendered, int j)
    {
        const auto from = rendered.begin() + j * sixteenth + 480;
        return std::vector<float> (from, from + 3600);
    };

    // Inside each grain, the two tones its tap could straddle: the share that
    // is the whole-sixteenth one.
    const auto cleanShare = [&] (bool grid)
    {
        const auto rendered = render (grid, 0.0f);
        double wanted = 0.0, both = 0.0;
        for (int j = 8; j < slots - 1; ++j)
        {
            const auto grain = grainOf (rendered, j);
            const double onGrid = goertzelPower (grain, toneHz (j - 2), kSampleRate);
            const double before = goertzelPower (grain, toneHz (j - 3), kSampleRate);
            wanted += onGrid;
            both += onGrid + before;
        }
        return wanted / juce::jmax (1.0e-30, both);
    };

    const double on = cleanShare (true);
    const double off = cleanShare (false);
    std::printf ("  share of each grain that is one clean sixteenth: Grid %.3f, off %.3f\n", on, off);
    check (on > 0.97, "Grid did not put the live tap on a whole sixteenth");
    check (off < 0.9, "without Grid the tap already sat on the grid - the control no longer controls anything");

    // Scatter counts whole sixteenths: at 25 % a good share of grains should
    // move off the two-sixteenth tap onto a neighbour, still landing cleanly on
    // one. The old share-of-Time mapping could not move it below ~42 % here.
    const auto scattered = render (true, 0.25f);
    int moved = 0, counted = 0;
    double onSomeSixteenth = 0.0, total = 0.0;
    for (int j = 8; j < slots - 1; ++j)
    {
        const auto grain = grainOf (scattered, j);
        double best = 0.0;
        int bestTap = 0;
        for (int tap = 1; tap <= 6; ++tap)
        {
            const double p = goertzelPower (grain, toneHz (j - tap), kSampleRate);
            if (p > best)
            {
                best = p;
                bestTap = tap;
            }
            total += p;
        }
        onSomeSixteenth += best;
        moved += bestTap != 2 ? 1 : 0;
        ++counted;
    }
    std::printf ("  Scatter 25 %%: %d of %d grains moved off the two-sixteenth tap, %.3f of energy on one sixteenth\n",
                 moved, counted, onSomeSixteenth / juce::jmax (1.0e-30, total));
    check (moved >= counted / 5 && moved <= (4 * counted) / 5, "Scatter 25 % did not move the live grid tap");
    check (onSomeSixteenth / juce::jmax (1.0e-30, total) > 0.95, "a scattered live grid tap fell between sixteenths");
}

void testGrainerFollowDensityReadsOnTheGrid()
{
    std::printf ("Grainer followDensity (the read point follows the Density division):\n");

    // 120 bpm, one tone per sixteenth, Smooth and attack capture off. A grain
    // spawned at ppq t reads from the sixteenth-note gridline at or before one
    // grain period ago: at 1/32 (period = half a sixteenth) that alternates
    // between one and two periods back, at 1/8 (two sixteenths) it is always
    // two sixteenths back.
    const double ppqPerSample = 2.0 / kSampleRate;
    const int sixteenth = static_cast<int> (0.25 / ppqPerSample);
    const auto toneHz = [] (int j) { return 400.0 + 100.0 * (((j % 12) + 12) % 12); };

    const auto source = [&] (int n)
    {
        return 0.5f
               * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * toneHz (n / sixteenth) * n
                                               / kSampleRate));
    };

    const int slots = 60;

    const auto render = [&] (double cyclesPerQuarter, bool follow)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (30.0f);
        grainer.setDensityHz (static_cast<float> (cyclesPerQuarter * 2.0));
        grainer.setTimeMs (300.0f);
        grainer.setFeedback (0.0f);
        grainer.setScatter (0.0f);
        grainer.setReverse (0.0f);
        grainer.setStereo (0.0f);

        auto tuning = grainer.getTuning();
        tuning.attackShare = 0.0f;
        tuning.followDensity = follow ? 1.0f : 0.0f;
        grainer.setTuning (tuning);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        std::vector<float> rendered;

        for (int n = 0; n < slots * sixteenth; n += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = source (n + i);

            ee::dsp::Grainer::Transport transport;
            transport.synced = true;
            transport.playing = true;
            transport.ppqStart = n * ppqPerSample;
            transport.cyclesPerQuarter = cyclesPerQuarter;
            transport.ppqPerSample = ppqPerSample;
            transport.grid = true;

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock, transport);
            rendered.insert (rendered.end(), outL.begin(), outL.end());
        }

        return rendered;
    };

    // The sixteenth whose tone dominates the grain that spawned `startSample`
    // into the render, searched over the few it could plausibly be.
    const auto tapOf = [&] (const std::vector<float>& rendered, int startSample, int spawnSixteenth)
    {
        const std::vector<float> grain (rendered.begin() + startSample + 96, rendered.begin() + startSample + 1300);
        double best = 0.0;
        int bestBack = 0;
        for (int back = 0; back <= 5; ++back)
        {
            const double p = goertzelPower (grain, toneHz (spawnSixteenth - back), kSampleRate);
            if (p > best)
            {
                best = p;
                bestBack = back;
            }
        }
        return bestBack;
    };

    // 1/32: spawns every half sixteenth. Even ones (on a sixteenth) read one
    // sixteenth back, odd ones (half way through one) read that same sixteenth.
    {
        const auto rendered = render (8.0, true);
        int right = 0, counted = 0;
        for (int k = 16; k < 2 * (slots - 3); ++k)
        {
            const int spawnSixteenth = k / 2;
            const int expected = (k % 2 == 0) ? 1 : 0;
            right += tapOf (rendered, k * sixteenth / 2, spawnSixteenth) == expected ? 1 : 0;
            ++counted;
        }
        std::printf ("  1/32: %d of %d grains read the sixteenth the rule names\n", right, counted);
        check (right >= (counted * 9) / 10, "at 1/32 the read point did not alternate one and two periods back");
    }

    // 1/8: spawns every two sixteenths, always reading two sixteenths back.
    {
        const auto rendered = render (2.0, true);
        int right = 0, counted = 0;
        for (int k = 8; k < slots / 2 - 2; ++k)
        {
            right += tapOf (rendered, k * 2 * sixteenth, 2 * k) == 2 ? 1 : 0;
            ++counted;
        }
        std::printf ("  1/8: %d of %d grains read two sixteenths back\n", right, counted);
        check (right >= (counted * 9) / 10, "at 1/8 the read point did not sit two sixteenths back");
    }

    // With the switch off the same 1/32 setup falls back to the Time tap (300
    // ms = two sixteenths after rounding), whatever the division is.
    {
        const auto rendered = render (8.0, false);
        int onTimeTap = 0, counted = 0;
        for (int k = 16; k < 2 * (slots - 3); k += 2)
        {
            onTimeTap += tapOf (rendered, k * sixteenth / 2, k / 2) == 2 ? 1 : 0;
            ++counted;
        }
        std::printf ("  switch off: %d of %d grains on the Time tap\n", onTimeTap, counted);
        check (onTimeTap >= (counted * 9) / 10, "with followDensity off the read point left the Time tap");
    }
}

void testGrainerAttackLeadsWithOctaves()
{
    std::printf ("Grainer attack octaves (the first grain after a struck note):\n");

    // 120 bpm, a grain every eighth. A 220 Hz pluck lands just after a spawn
    // tick, so the next tick's grain is the first one after the attack and
    // sounds alone - the grain before it ended long ago. Whatever pitches
    // that grain holds are exactly what the attack spawned.
    const double ppqPerSample = 2.0 / kSampleRate;
    const int eighth = static_cast<int> (0.5 / ppqPerSample);
    const int bar = 8 * eighth;
    const int plucks = 8;

    const auto pluck = [&] (int n)
    {
        const int into = n % bar - eighth / 8;
        if (into < 0)
            return 0.0f;
        const double t = into / kSampleRate;
        return 0.8f * static_cast<float> (std::exp (-6.0 * t) * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t));
    };

    // Per pluck: the level of 110 / 220 / 440 Hz in the first grain after it.
    const auto firstGrains = [&] (float low, float unison, float high)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (150.0f);
        grainer.setDensityHz (4.0f);
        grainer.setTimeMs (100.0f);
        grainer.setFeedback (0.0f);
        grainer.setScatter (0.0f);
        grainer.setReverse (0.0f);
        grainer.setStereo (0.0f);
        grainer.setScaleBlend (0.0f);
        grainer.setPitchMix (low, unison, high);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock), rendered;
        for (int n = 0; n < plucks * bar; n += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = pluck (n + i);

            ee::dsp::Grainer::Transport transport;
            transport.synced = true;
            transport.playing = true;
            transport.ppqStart = n * ppqPerSample;
            transport.cyclesPerQuarter = 2.0;
            transport.ppqPerSample = ppqPerSample;

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock, transport);
            rendered.insert (rendered.end(), outL.begin(), outL.end());
        }

        std::vector<std::array<double, 3>> levels;
        for (int p = 0; p < plucks; ++p)
        {
            const auto from = rendered.begin() + p * bar + eighth + 480;
            const std::vector<float> grain (from, from + 4800);
            levels.push_back ({ goertzelPower (grain, 110.0, kSampleRate), goertzelPower (grain, 220.0, kSampleRate),
                                goertzelPower (grain, 440.0, kSampleRate) });
        }
        return levels;
    };

    const auto describe = [] (const char* name, const std::vector<std::array<double, 3>>& levels, int good)
    {
        std::printf ("  %-34s %d of %zu plucks as expected\n", name, good, levels.size());
    };

    // Low barely on, High off: every first grain is the octave below, alone -
    // even though the odds say it should almost never be picked.
    {
        const auto levels = firstGrains (1.0f, 98.0f, 0.0f);
        int good = 0;
        for (const auto& l : levels)
            good += (l[0] > 10.0 * l[1] && l[0] > 10.0 * l[2]) ? 1 : 0;
        describe ("Low 1 / Unison 98: octave below", levels, good);
        check (good == plucks, "the first grain after an attack was not the octave below");
    }

    // Low and High both barely on: every first grain holds all three octaves.
    {
        const auto levels = firstGrains (1.0f, 98.0f, 1.0f);
        int good = 0;
        for (const auto& l : levels)
        {
            const double loudest = std::max ({ l[0], l[1], l[2] });
            good += (l[0] > 0.02 * loudest && l[1] > 0.02 * loudest && l[2] > 0.02 * loudest) ? 1 : 0;
        }
        describe ("Low 1 / Unison 98 / High 1: all three", levels, good);
        check (good == plucks, "the first grain after an attack did not hold all three octaves");
    }

    // Control - Low off: the random pick as before, which at these odds is
    // the note itself.
    {
        const auto levels = firstGrains (0.0f, 98.0f, 1.0f);
        int noLow = 0;
        for (const auto& l : levels)
            noLow += l[0] < 0.01 * l[1] ? 1 : 0;
        describe ("control, Low 0: no octave below", levels, noLow);
        check (noLow == plucks, "with Low off the attack still spawned the octave below");
    }
}

void testGrainerStereoIsBalanced()
{
    std::printf ("Grainer stereo placement:\n");

    std::mt19937 rng (3);
    std::uniform_real_distribution<float> noise (-0.7f, 0.7f);

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (120.0f);
    grainer.setDensityHz (20.0f);
    grainer.setTimeMs (400.0f);
    grainer.setFeedback (0.0f);
    grainer.setStereo (1.0f); // Spray takes priority over Wide, left at its default of 0 here

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    double leftSquares = 0.0;
    double rightSquares = 0.0;

    for (int b = 0; b < static_cast<int> (kSampleRate * 12.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            leftSquares += static_cast<double> (outL[static_cast<size_t> (i)]) * outL[static_cast<size_t> (i)];
            rightSquares += static_cast<double> (outR[static_cast<size_t> (i)]) * outR[static_cast<size_t> (i)];
        }
    }

    const float balanceDb = juce::Decibels::gainToDecibels (
        static_cast<float> (std::sqrt (leftSquares / juce::jmax (1.0e-12, rightSquares))));

    std::printf ("  L/R balance at full width: %.2f dB\n", balanceDb);

    // Grains are panned at random, so the two sides even out over a run of any
    // length - give or take which side the louder ones happened to land on,
    // which is why this is not tighter. What it is really guarding is a
    // placement bug that starves one side, and that shows up as many decibels
    // rather than as a fraction of one.
    check (std::abs (balanceDb) < 1.5f, "the grain cloud is lopsided (" + juce::String (balanceDb, 2) + " dB)");
}

/** Wide at 0 with Spray also closed: nextPan() falls through to
    panAlternateSign * width, which is zero either way, so the cloud folds to
    mono - see Grainer::nextPan() and GrainerConfig.h's WIDE. */
void testGrainerWideZeroIsMonoWhenSprayIsAlsoClosed()
{
    std::printf ("Grainer Wide at 0 with Spray closed:\n");

    std::mt19937 rng (5);
    std::uniform_real_distribution<float> noise (-0.7f, 0.7f);

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (120.0f);
    grainer.setDensityHz (20.0f);
    grainer.setTimeMs (400.0f);
    grainer.setFeedback (0.0f);
    grainer.setStereo (0.0f); // Spray closed...
    grainer.setWidth (0.0f);  // ...and Wide shut too: nothing left to pan with

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    double diffSquares = 0.0;
    double sumSquares = 0.0;

    for (int b = 0; b < static_cast<int> (kSampleRate * 6.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            const float l = outL[static_cast<size_t> (i)];
            const float r = outR[static_cast<size_t> (i)];
            diffSquares += static_cast<double> (l - r) * (l - r);
            sumSquares += static_cast<double> (l) * l + static_cast<double> (r) * r;
        }
    }

    const float diffDb = juce::Decibels::gainToDecibels (
        static_cast<float> (std::sqrt (diffSquares / juce::jmax (1.0e-12, sumSquares))), -120.0f);

    std::printf ("  L-R difference: %.1f dB below the signal\n", diffDb);

    check (diffDb < -80.0f,
           "Wide and Spray both at 0 should leave the cloud mono (L-R only " + juce::String (diffDb, 1) + " dB down)");
}

/** Spray closed (`stereo` 0) hands panning to Wide alone: grains hard-alternate
    left/right/left/right at Wide's own reach rather than a random draw. */
void testGrainerWideAlternatesSidesWhenSprayIsClosed()
{
    std::printf ("Grainer Wide alternation with Spray closed:\n");

    std::mt19937 rng (7);
    std::uniform_real_distribution<float> noise (-0.7f, 0.7f);

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (60.0f);
    grainer.setDensityHz (20.0f);
    grainer.setTimeMs (400.0f);
    grainer.setFeedback (0.0f);
    grainer.setStereo (0.0f); // Spray fully closed - Wide alone decides pan
    grainer.setWidth (1.0f);

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    for (int b = 0; b < static_cast<int> (kSampleRate * 4.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }

    const uint32_t count = grainer.grainEventCount();
    const uint32_t seen = std::min<uint32_t> (count, ee::dsp::Grainer::kGrainEventRing);
    check (seen >= 20, "not enough grains spawned to check alternation (" + juce::String (static_cast<int> (seen)) + ")");

    // Attack-stack voices born together share one pan draw on purpose (they
    // are one musical event, not several) - collapsing consecutive repeats
    // before checking alternation is what tells those apart from a side
    // genuinely repeating.
    std::vector<float> flips;
    for (uint32_t i = count - seen; i < count; ++i)
    {
        const float pan = grainer.grainEventAt (i).pan;
        check (std::abs (std::abs (pan) - 1.0f) < 1.0e-4f,
               "grain landed off Wide's extreme with Spray closed (pan " + juce::String (pan, 3) + ")");

        if (flips.empty() || (pan > 0.0f) != (flips.back() > 0.0f))
            flips.push_back (pan);
    }

    std::printf ("  %d side flips over %u grains\n", static_cast<int> (flips.size()) - 1, seen);

    check (flips.size() > seen / 4,
           "grains are not alternating left/right with Spray closed - one side keeps repeating");
}

/** Spray open, Wide shut: Spray takes priority the moment it is off zero and
    draws exactly the random side/distance it always has, ignoring Wide
    entirely - Wide at 0 must not clamp or silence it. */
void testGrainerSprayOverridesWide()
{
    std::printf ("Grainer Spray overrides Wide:\n");

    std::mt19937 rng (11);
    std::uniform_real_distribution<float> noise (-0.7f, 0.7f);

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (60.0f);
    grainer.setDensityHz (20.0f);
    grainer.setTimeMs (400.0f);
    grainer.setFeedback (0.0f);
    grainer.setStereo (1.0f); // Spray wide open...
    grainer.setWidth (0.0f);  // ...and Wide shut, which should count for nothing here

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    for (int b = 0; b < static_cast<int> (kSampleRate * 4.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }

    const uint32_t count = grainer.grainEventCount();
    const uint32_t seen = std::min<uint32_t> (count, ee::dsp::Grainer::kGrainEventRing);
    check (seen >= 20, "not enough grains spawned to check Spray overriding Wide (" + juce::String (static_cast<int> (seen)) + ")");

    float maxAbsPan = 0.0f;
    bool sawLeft = false;
    bool sawRight = false;

    for (uint32_t i = count - seen; i < count; ++i)
    {
        const float pan = grainer.grainEventAt (i).pan;
        maxAbsPan = std::max (maxAbsPan, std::abs (pan));
        sawLeft |= pan < -0.3f;
        sawRight |= pan > 0.3f;
    }

    std::printf ("  max |pan| over %u grains: %.3f (Wide = 0, Spray = 1)\n", seen, static_cast<double> (maxAbsPan));

    check (maxAbsPan > 0.3f,
           "Spray should ignore Wide entirely rather than being clamped by it (max |pan| only "
               + juce::String (maxAbsPan, 3) + ")");
    check (sawLeft && sawRight, "Spray should still draw both sides with Wide shut");
}

void testGrainerTailStops()
{
    std::printf ("Grainer after the input stops:\n");

    std::mt19937 rng (7);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (500.0f);
    grainer.setDensityHz (40.0f);
    grainer.setTimeMs (2000.0f);
    grainer.setFeedback (0.0f); // with no feedback the cloud must fall to exactly silence
    grainer.setPitchMix (0.0f, 0.0f, 1.0f);

    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }

    // Silence in, for comfortably longer than the tail the engine advertises.
    std::fill (inL.begin(), inL.end(), 0.0f);
    std::fill (inR.begin(), inR.end(), 0.0f);

    // Only just past what the engine advertises - the recording buffer is
    // longer than this, so a pass means the grains really did stop rather than
    // running out of anything to read.
    const double settle = static_cast<double> (grainer.getTailSeconds()) + 0.25;
    for (int b = 0; b < static_cast<int> (kSampleRate * settle / kBlock); ++b)
        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

    float peak = 0.0f;
    for (int b = 0; b < static_cast<int> (kSampleRate * 1.0 / kBlock); ++b)
    {
        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            peak = juce::jmax (peak, std::abs (outL[static_cast<size_t> (i)]));
    }

    std::printf ("  peak %.1f s after the input stopped: %.3g\n", settle, peak);
    check (peak == 0.0f, "the grain cloud never stopped");
}

void testGrainerPitchIsActuallyApplied()
{
    std::printf ("Grainer pitch spread:\n");

    // A wound-up Pitch has to move the spectrum, not just reshuffle grains.
    // Measured as the zero-crossing rate of the output against a fixed tone,
    // which rises with an upward spread and falls with a downward one.
    const auto tone = [] (int n)
    { return 0.7f * std::sin (2.0 * juce::MathConstants<double>::pi * 400.0 * n / kSampleRate); };

    const auto crossingsAt = [&tone] (float pitch)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (200.0f);
        grainer.setDensityHz (20.0f);
        grainer.setTimeMs (200.0f);
        grainer.setFeedback (0.0f);
        grainer.setPitchMix (juce::jmax (0.0f, -pitch), 1.0f - std::abs (pitch), juce::jmax (0.0f, pitch));

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        int crossings = 0;
        float previous = 0.0f;
        int n = 0;

        for (int b = 0; b < static_cast<int> (kSampleRate * 4.0 / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = tone (n++);

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

            for (int i = 0; i < kBlock; ++i)
            {
                const float s = outL[static_cast<size_t> (i)];
                if ((s > 0.0f) != (previous > 0.0f))
                    ++crossings;
                previous = s;
            }
        }

        return crossings;
    };

    const int unison = crossingsAt (0.0f);
    const int up = crossingsAt (1.0f);
    const int down = crossingsAt (-1.0f);

    std::printf ("  zero crossings: down %d, unison %d, up %d\n", down, unison, up);

    check (up > unison, "an upward pitch spread did not raise the pitch");
    check (down < unison, "a downward pitch spread did not lower the pitch");
}

void testGrainerHighWithNoScaleIsAnOctaveUp()
{
    std::printf ("Grainer pitch: with the scale closed the High group is a plain octave\n");

    // The regression this guards: Pitch Mix used to crossfade High's *weight*
    // back towards unison, so closing it - which is what switching Scale off
    // does - meant the up group was never picked at all, and the only pitch
    // left audible was Low's octave down. Mix colours the interval now and
    // never the weight, so at 0 every up-grain is a clean +12.
    const auto tone = [] (int n)
    { return 0.7f * std::sin (2.0 * juce::MathConstants<double>::pi * 400.0 * n / kSampleRate); };

    const auto crossingsWith = [&tone] (float low, float unison, float high)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (200.0f);
        grainer.setDensityHz (20.0f);
        grainer.setTimeMs (200.0f);
        grainer.setFeedback (0.0f);
        // B major, in which the octave is deliberately *not* a scale member -
        // so a blend leaking the table in could not come back looking like an
        // octave by coincidence.
        grainer.setScale (0, 11);
        grainer.setScaleBlend (0.0f);
        grainer.setPitchMix (low, unison, high);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        int crossings = 0;
        float previous = 0.0f;
        int n = 0;

        for (int b = 0; b < static_cast<int> (kSampleRate * 4.0 / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = tone (n++);

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

            for (int i = 0; i < kBlock; ++i)
            {
                const float s = outL[static_cast<size_t> (i)];
                if ((s > 0.0f) != (previous > 0.0f))
                    ++crossings;
                previous = s;
            }
        }

        return crossings;
    };

    const int unison = crossingsWith (0.0f, 1.0f, 0.0f);
    const int high = crossingsWith (0.0f, 0.0f, 1.0f);
    const double ratio = unison > 0 ? static_cast<double> (high) / static_cast<double> (unison) : 0.0;

    std::printf ("  zero crossings: unison %d, High with the scale closed %d (%.2fx)\n", unison, high, ratio);

    // An octave is 2x; unison - the bug - is 1x. Half way between the two is
    // the widest gate that still tells them apart.
    check (ratio > 1.5, "High with the scale closed did not transpose: it read as unison");
}

void testGrainerCloudFilterClosesAndDefaultsOpen()
{
    std::printf ("Grainer cloud filter:\n");

    // Two things at once. That the knob darkens the cloud at all - and, the
    // one that actually bites, that an engine nobody has called
    // setCloudFilter() on starts *open*. The knob is a unipolar cutoff now, so
    // cloudFilterAmount's 0 means "fully shut" where under the old bipolar
    // sweep it meant "the resting pair"; a default left at 0 would ship every
    // fresh instance with the cloud closed and sound like a broken plugin.
    enum Mode
    {
        untouched, // never call setCloudFilter - whatever the member defaults to
        open,
        closed
    };

    const auto cloudRms = [] (Mode mode)
    {
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (200.0f);
        grainer.setDensityHz (30.0f);
        grainer.setTimeMs (200.0f);
        grainer.setFeedback (0.0f);

        if (mode == open)
            grainer.setCloudFilter (1.0f);
        else if (mode == closed)
            grainer.setCloudFilter (0.0f);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        double sum = 0.0;
        int counted = 0;

        const int blocks = static_cast<int> (kSampleRate * 3.0 / kBlock);
        const int settled = static_cast<int> (kSampleRate * 1.0 / kBlock);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

            // Only once the cloud has filled, so this measures the filter and
            // not how fast the buffer got going.
            if (b > settled)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const double s = outL[static_cast<size_t> (i)];
                    sum += s * s;
                    ++counted;
                }
            }
        }

        return counted > 0 ? std::sqrt (sum / counted) : 0.0;
    };

    const double untouchedRms = cloudRms (untouched);
    const double openRms = cloudRms (open);
    const double closedRms = cloudRms (closed);

    std::printf ("  cloud rms: untouched %.4f, open %.4f, closed %.4f\n", untouchedRms, openRms, closedRms);

    check (closedRms < openRms * 0.7, "closing the Filter knob did not darken the cloud");
    check (std::abs (untouchedRms - openRms) < openRms * 0.02,
           "an untouched engine did not start with its filter open");
}

void testGrainerSmoothChangeDoesNotJumpTheLevel()
{
    std::printf ("Grainer Smooth: moving the knob does not jump the level of grains already sounding:\n");

    // Smooth changes what window a grain is born with, and the energy that
    // window holds has to be compensated. If that compensation sat in the output
    // gain it would move the instant the knob did, and grains still ringing
    // (long ones last a second) would be re-scaled for a window they do not
    // have - a burst of loud, dirty sound for as long as they last. So: long
    // grains on a steady tone with every random element off (so the output
    // is one repeating pattern), Smooth moved from one end to the other, and
    // the loudest 50 ms after the change compared with the loudest 50 ms of
    // each steady state.
    const auto run = [] (float from, float to, double& steadyFromPeak, double& steadyToPeak, double& worstAfter)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (600.0f);
        grainer.setDensityHz (4.0f);
        grainer.setTimeMs (300.0f);
        grainer.setFeedback (0.0f);
        grainer.setScatter (0.0f);
        grainer.setReverse (0.0f);
        grainer.setStereo (0.0f);

        auto tuning = grainer.getTuning();
        tuning.attackShare = 0.0f;
        tuning.grainLevelJitter = 0.0f;
        tuning.windowJitter = 0.0f;
        tuning.filterSpray = 0.0f;
        tuning.sourceLevelling = 0.0f;
        tuning.densityFollow = 0.0f;
        tuning.bandSplit = 0.0f;
        grainer.setTuning (tuning);

        std::vector<float> in (kBlock), outL (kBlock), outR (kBlock);
        const int frame = static_cast<int> (kSampleRate * 0.05);

        std::vector<double> before, after;
        const int total = static_cast<int> (kSampleRate * 14.0);
        const int change = static_cast<int> (kSampleRate * 7.0);
        double acc = 0.0;
        int inFrame = 0;

        grainer.setSmooth (from);
        bool moved = false;
        for (int n = 0; n < total; n += kBlock)
        {
            if (! moved && n >= change)
            {
                grainer.setSmooth (to);
                moved = true;
            }

            for (int i = 0; i < kBlock; ++i)
                in[static_cast<size_t> (i)] = 0.1f
                                              * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                                              * 300.0 * (n + i) / kSampleRate));

            grainer.process (in.data(), in.data(), outL.data(), outR.data(), kBlock);

            for (int i = 0; i < kBlock; ++i)
            {
                acc += static_cast<double> (outL[static_cast<size_t> (i)]) * outL[static_cast<size_t> (i)];
                if (++inFrame == frame)
                {
                    (n + i < change ? before : after).push_back (std::sqrt (acc / frame));
                    acc = 0.0;
                    inFrame = 0;
                }
            }
        }

        const auto peakOf = [] (const std::vector<double>& v, size_t lo, size_t hi)
        {
            double peak = 0.0;
            for (size_t i = lo; i < hi; ++i)
                peak = std::max (peak, v[i]);
            return peak;
        };

        steadyFromPeak = peakOf (before, before.size() - 60, before.size());
        steadyToPeak = peakOf (after, after.size() - 60, after.size());
        worstAfter = peakOf (after, 0, 80); // the 4 s after the change
    };

    for (const auto& dir : { std::pair<float, float> { 1.0f, 0.0f }, std::pair<float, float> { 0.0f, 1.0f } })
    {
        double a = 0.0, b = 0.0, worst = 0.0;
        run (dir.first, dir.second, a, b, worst);
        const double ceiling = std::max (a, b);
        std::printf ("  Smooth %.0f -> %.0f: loudest 50 ms in each steady state %.4f and %.4f, after the change %.4f (%+.1f dB against the louder)\n",
                     dir.first, dir.second, a, b, worst, 20.0 * std::log10 (worst / juce::jmax (1.0e-9, ceiling)));
        // A steady tone and 600 ms grains is the worst case for this: the old
        // and new grains add coherently. The gain following Smooth instead of
        // being per-grain measured +9.6 dB here, and 1.15 x the louder steady
        // state is what a fixed, gliding Smooth manages with room to spare.
        check (worst < ceiling * 1.3, "moving Smooth made grains already sounding jump in level");
    }
}

void testGrainerShapeChangeDoesNotJumpTheLevel()
{
    std::printf ("Grainer Shape: moving the knob does not jump the level of grains already sounding:\n");

    // The same failure testGrainerSmoothChangeDoesNotJumpTheLevel guards, one
    // control over: Shape's own decay steepness changes how much energy a
    // grain's plain window holds (updateDerived's envelopeRms), compensated
    // the same way Smooth's extra swell energy is - each grain corrected at
    // its own birth (swellComp), not by an output gain that would rescale
    // grains already ringing for an envelope they do not have. Shape and
    // Smooth are one control on the face (Smooth = 1 - Shape), so a user
    // sweeping the knob moves both at once - this exercises Shape's own half
    // of that pair directly, with Smooth held at 0 throughout so nothing here
    // rides on the other test's own coverage.
    const auto run = [] (float from, float to, double& steadyFromPeak, double& steadyToPeak, double& worstAfter)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (600.0f);
        grainer.setDensityHz (4.0f);
        grainer.setTimeMs (300.0f);
        grainer.setFeedback (0.0f);
        grainer.setScatter (0.0f);
        grainer.setReverse (0.0f);
        grainer.setStereo (0.0f);
        grainer.setSmooth (0.0f);

        auto tuning = grainer.getTuning();
        tuning.attackShare = 0.0f;
        tuning.grainLevelJitter = 0.0f;
        tuning.windowJitter = 0.0f;
        tuning.filterSpray = 0.0f;
        tuning.sourceLevelling = 0.0f;
        tuning.densityFollow = 0.0f;
        tuning.bandSplit = 0.0f;
        grainer.setTuning (tuning);

        std::vector<float> in (kBlock), outL (kBlock), outR (kBlock);
        const int frame = static_cast<int> (kSampleRate * 0.05);

        std::vector<double> before, after;
        const int total = static_cast<int> (kSampleRate * 14.0);
        const int change = static_cast<int> (kSampleRate * 7.0);
        double acc = 0.0;
        int inFrame = 0;

        grainer.setShape (from);
        bool moved = false;
        for (int n = 0; n < total; n += kBlock)
        {
            if (! moved && n >= change)
            {
                grainer.setShape (to);
                moved = true;
            }

            for (int i = 0; i < kBlock; ++i)
                in[static_cast<size_t> (i)] = 0.1f
                                              * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                                              * 300.0 * (n + i) / kSampleRate));

            grainer.process (in.data(), in.data(), outL.data(), outR.data(), kBlock);

            for (int i = 0; i < kBlock; ++i)
            {
                acc += static_cast<double> (outL[static_cast<size_t> (i)]) * outL[static_cast<size_t> (i)];
                if (++inFrame == frame)
                {
                    (n + i < change ? before : after).push_back (std::sqrt (acc / frame));
                    acc = 0.0;
                    inFrame = 0;
                }
            }
        }

        const auto peakOf = [] (const std::vector<double>& v, size_t lo, size_t hi)
        {
            double peak = 0.0;
            for (size_t i = lo; i < hi; ++i)
                peak = std::max (peak, v[i]);
            return peak;
        };

        steadyFromPeak = peakOf (before, before.size() - 60, before.size());
        steadyToPeak = peakOf (after, after.size() - 60, after.size());
        worstAfter = peakOf (after, 0, 80); // the 4 s after the change
    };

    for (const auto& dir : { std::pair<float, float> { 1.0f, 0.0f }, std::pair<float, float> { 0.0f, 1.0f } })
    {
        double a = 0.0, b = 0.0, worst = 0.0;
        run (dir.first, dir.second, a, b, worst);
        const double ceiling = std::max (a, b);
        std::printf ("  Shape %.0f -> %.0f: loudest 50 ms in each steady state %.4f and %.4f, after the change %.4f (%+.1f dB against the louder)\n",
                     dir.first, dir.second, a, b, worst, 20.0 * std::log10 (worst / juce::jmax (1.0e-9, ceiling)));
        check (worst < ceiling * 1.3, "moving Shape made grains already sounding jump in level");
    }
}

void testGrainerLowGroupIsOctavesOnly()
{
    std::printf ("Grainer pitch: the Low group is octaves, whatever the scale\n");

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);

    bool allOctaves = true;

    for (int scaleIndex = 0; scaleIndex < ee::dsp::config::kNumScales; ++scaleIndex)
    {
        // A root that is nobody's tonic here, so a scale leaking into this
        // group would show up rather than coinciding with an octave.
        grainer.setScale (scaleIndex, 3 /* D# */);

        allOctaves = allOctaves && grainer.getDownCandidateCount() > 0;

        for (int i = 0; i < grainer.getDownCandidateCount(); ++i)
        {
            const int n = static_cast<int> (grainer.getDownCandidate (i));
            allOctaves = allOctaves && n < 0 && n % 12 == 0;
        }
    }

    std::printf ("  down candidates: %d\n", grainer.getDownCandidateCount());
    check (allOctaves, "the Low group landed on something that was not a whole octave down");
    check (grainer.getDownCandidateCount() == 1 && grainer.getDownCandidate (0) == -12.0f,
           "the Low group offered anything but the one octave down");
}

void testGrainerHighGroupIsTheScaleAnOctaveUp()
{
    std::printf ("Grainer pitch: the High group is the scale, an octave up\n");

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.setScale (0 /* Major */, 0 /* C */);

    const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    bool clearOfTheOctave = grainer.getUpCandidateCount() > 0;
    bool allInScale = true;

    for (int i = 0; i < grainer.getUpCandidateCount(); ++i)
    {
        const int n = static_cast<int> (grainer.getUpCandidate (i));
        clearOfTheOctave = clearOfTheOctave && n >= 12;

        bool found = false;
        for (int m : major)
            found = found || m == ((n % 12) + 12) % 12;
        allInScale = allInScale && found;
    }

    std::printf ("  up candidates: %d\n", grainer.getUpCandidateCount());
    check (clearOfTheOctave, "the High group landed below an octave up");
    check (allInScale, "the High group landed off the scale");
}

void testGrainerScaleRootRotatesThePattern()
{
    std::printf ("Grainer pitch: Root rotates which notes the High group can take\n");

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);

    const auto allows = [&grainer] (int scaleIndex, int root, int semitones)
    {
        grainer.setScale (scaleIndex, root);
        for (int i = 0; i < grainer.getUpCandidateCount(); ++i)
            if (static_cast<int> (grainer.getUpCandidate (i)) == semitones)
                return true;
        return false;
    };

    // At Root C the octave is the tonic, so +12 is in and +13 (a flat ninth)
    // is not. At Root F# the pattern has rotated under it: +12 is now the
    // tritone and out, +13 is the fifth and in.
    const bool twelveAtC = allows (0 /* Major */, 0 /* C */, 12);
    const bool thirteenAtC = allows (0, 0, 13);
    const bool twelveAtFSharp = allows (0, 6 /* F# */, 12);
    const bool thirteenAtFSharp = allows (0, 6, 13);

    std::printf ("  Root C: +12 %s, +13 %s | Root F#: +12 %s, +13 %s\n", twelveAtC ? "yes" : "no",
                 thirteenAtC ? "yes" : "no", twelveAtFSharp ? "yes" : "no", thirteenAtFSharp ? "yes" : "no");

    check (twelveAtC && ! thirteenAtC, "C major did not put the octave in and the flat ninth out");
    check (! twelveAtFSharp && thirteenAtFSharp, "Root did not rotate the pattern");
}

void testGrainerLevelJitterHoldsTheCloudLevel()
{
    std::printf ("Grainer per-grain level jitter holds the cloud's level:\n");

    const auto rmsAtJitter = [] (float jitter)
    {
        std::mt19937 rng (31);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();

        auto tuning = grainer.getTuning();
        tuning.grainLevelJitter = jitter;
        grainer.setTuning (tuning);

        grainer.setSizeMs (120.0f);
        grainer.setDensityHz (30.0f);
        grainer.setTimeMs (300.0f);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        double sumSquares = 0.0;
        long long counted = 0;

        for (int b = 0; b < static_cast<int> (kSampleRate * 4.0 / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = noise (rng);

            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

            for (int i = 0; i < kBlock; ++i)
            {
                const double s = outL[static_cast<size_t> (i)];
                sumSquares += s * s;
                ++counted;
            }
        }

        return static_cast<float> (std::sqrt (sumSquares / static_cast<double> (counted)));
    };

    const float flat = rmsAtJitter (0.0f);
    const float jittered = rmsAtJitter (0.35f);
    const float difference = juce::Decibels::gainToDecibels (jittered / juce::jmax (1.0e-9f, flat));

    std::printf ("  rms flat %.4f, jittered %.4f (%.2f dB)\n", flat, jittered, difference);

    // Not sample-exact by construction: the jittered run draws one extra
    // random per grain, so grain placement diverges between the two rather
    // than matching. The trim in updateDerived only has to hold the *level*.
    check (std::abs (difference) < 1.0f, "per-grain level jitter moved the cloud's level");
}

void testGrainerFeedbackStaysFinite()
{
    std::printf ("Grainer feedback swept against noise and DC:\n");

    std::mt19937 rng (55);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const float feedbacks[] = { 0.5f, 0.8f, 0.92f };

    bool finite = true;
    float worst = 0.0f;

    for (float fb : feedbacks)
    {
        const auto n = runGrainer (120.0f, 25.0f, 300.0f, 0.0f, 2.0,
                                   [&rng, &noise] (int) { return noise (rng); }, fb);
        const auto d = runGrainer (120.0f, 25.0f, 300.0f, 0.0f, 2.0, [] (int) { return 0.9f; }, fb);

        finite = finite && n.finite && d.finite;
        worst = juce::jmax (worst, juce::jmax (n.peak, d.peak));
    }

    std::printf ("  worst peak %.3g\n", worst);

    check (finite, "feedback produced NaN or Inf");
    check (worst < 4.0f, "feedback ran away (peak " + juce::String (worst, 2) + ")");
}

void testGrainerFreezeHoldsAndRetriggers()
{
    std::printf ("Grainer freeze holds the buffer and a loud note recaptures:\n");

    ee::dsp::Grainer grainer;
    grainer.prepare (kSampleRate);
    grainer.reset();
    grainer.setSizeMs (120.0f);
    grainer.setDensityHz (25.0f);
    grainer.setTimeMs (300.0f);
    grainer.setFeedback (0.0f);
    grainer.setStretch (1.0f);
    grainer.setReverse (0.0f); // keep the recaptured tone clean for the pitch estimate
    grainer.setScatter (0.0f);

    std::mt19937 rng (8);
    std::uniform_real_distribution<float> noise (-0.6f, 0.6f);
    std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);

    const auto pushBlocks = [&] (double seconds, auto&& sample)
    {
        int n = 0;
        for (int b = 0; b < static_cast<int> (kSampleRate * seconds / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = sample (n++);
            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        }
    };

    // Fill the buffer, then freeze and go silent.
    pushBlocks (1.0, [&] (int) { return noise (rng); });
    grainer.setFreeze (true);

    double squares = 0.0;
    long long counted = 0;
    for (int b = 0; b < static_cast<int> (kSampleRate * 4.0 / kBlock); ++b)
    {
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);
        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            squares += static_cast<double> (outL[static_cast<size_t> (i)]) * outL[static_cast<size_t> (i)];
            ++counted;
        }
    }

    const float frozenRms = static_cast<float> (std::sqrt (squares / static_cast<double> (counted)));
    std::printf ("  frozen cloud RMS over 4 s of silence: %.4f\n", frozenRms);
    check (frozenRms > 0.01f, "the frozen cloud fell silent");

    // A loud tone must retrigger the capture, so the cloud now carries it.
    pushBlocks (2.0, [] (int n)
                { return 0.9f * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * n / kSampleRate); });

    int crossings = 0;
    float previous = 0.0f;
    long long samples = 0;
    for (int b = 0; b < static_cast<int> (kSampleRate * 1.0 / kBlock); ++b)
    {
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);
        grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            const float s = outL[static_cast<size_t> (i)];
            if ((s > 0.0f) != (previous > 0.0f))
                ++crossings;
            previous = s;
            ++samples;
        }
    }

    const float hz = 0.5f * static_cast<float> (crossings) * static_cast<float> (kSampleRate)
                     / juce::jmax (1.0f, static_cast<float> (samples));
    std::printf ("  recaptured cloud pitch estimate: %.0f Hz (fed 300 Hz)\n", hz);
    check (hz > 150.0f && hz < 600.0f, "a loud note did not retrigger the capture");
}

void testGrainerStretchScrubsFrozenBuffer()
{
    std::printf ("Grainer stretch scans a frozen buffer without shifting pitch:\n");

    const auto tone = [] (int n)
    { return 0.7f * std::sin (2.0 * juce::MathConstants<double>::pi * 250.0 * n / kSampleRate); };

    const auto pitchAtStretch = [&tone] (float stretch)
    {
        ee::dsp::Grainer grainer;
        grainer.prepare (kSampleRate);
        grainer.reset();
        grainer.setSizeMs (150.0f);
        grainer.setDensityHz (25.0f);
        grainer.setTimeMs (300.0f);
        grainer.setFeedback (0.0f);
        grainer.setScatter (0.0f);

        std::vector<float> inL (kBlock), inR (kBlock), outL (kBlock), outR (kBlock);
        int n = 0;

        for (int b = 0; b < static_cast<int> (kSampleRate * 1.5 / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                inL[static_cast<size_t> (i)] = inR[static_cast<size_t> (i)] = tone (n++);
            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
        }

        grainer.setFreeze (true);
        grainer.setStretch (stretch);
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);

        for (int b = 0; b < static_cast<int> (kSampleRate * 0.5 / kBlock); ++b)
            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        int crossings = 0;
        float previous = 0.0f;
        long long samples = 0;
        bool finite = true;
        float peak = 0.0f;

        for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
        {
            grainer.process (inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = outL[static_cast<size_t> (i)];
                if (! std::isfinite (s))
                    finite = false;
                peak = juce::jmax (peak, std::abs (s));
                if ((s > 0.0f) != (previous > 0.0f))
                    ++crossings;
                previous = s;
                ++samples;
            }
        }

        check (finite, "stretch produced a non-finite sample");
        check (peak < 4.0f, "stretch ran away (peak " + juce::String (peak, 2) + ")");

        return 0.5f * static_cast<float> (crossings) * static_cast<float> (kSampleRate)
               / juce::jmax (1.0f, static_cast<float> (samples));
    };

    const float held = pitchAtStretch (0.0f);
    const float forward = pitchAtStretch (1.0f);
    const float backward = pitchAtStretch (-1.0f);

    std::printf ("  pitch: held %.0f Hz, +100%% %.0f Hz, -100%% %.0f Hz (source 250)\n", held, forward, backward);

    for (const float hz : { held, forward, backward })
        check (hz > 180.0f && hz < 340.0f, "stretch shifted the pitch of the frozen tone");
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

//==============================================================================
// Bit crusher (BitBit Artifact / BitBit Alpine's Bit Crush engine)

void testBitCrusherTransparentAtOff()
{
    std::printf ("Bit crusher: Bits 24, full rate, filter open -> bit-exact pass-through\n");

    const int total = static_cast<int> (kSampleRate);
    std::mt19937 rng (0x51ceu);
    std::normal_distribution<float> dist (0.0f, 0.2f);

    std::vector<float> srcL (total), srcR (total);
    for (int i = 0; i < total; ++i)
    {
        srcL[i] = std::tanh (dist (rng));
        srcR[i] = std::tanh (dist (rng));
    }

    ee::dsp::BitCrusher crusher;
    crusher.prepare (kSampleRate);
    crusher.setBits (ee::dsp::bitcrush::kBitsClean);
    crusher.setRateHz (static_cast<float> (kSampleRate));
    crusher.setLowpassHz (ee::dsp::bitcrush::kLpMaxHz);
    crusher.setJitter01 (1.0f); // must not matter while the hold stage is bypassed

    std::vector<float> l (srcL), r (srcR);
    for (int off = 0; off < total; off += kBlock)
        crusher.process (l.data() + off, r.data() + off, juce::jmin (kBlock, total - off));

    float worst = 0.0f;
    for (int i = 0; i < total; ++i)
        worst = juce::jmax (worst, std::abs (l[i] - srcL[i]), std::abs (r[i] - srcR[i]));

    std::printf ("  largest difference: %.2e\n", worst);
    check (worst == 0.0f, "bit crusher at rest is not bit exact");
}

void testBitCrusherSilence()
{
    std::printf ("Bit crusher: silence in -> silence out\n");

    ee::dsp::BitCrusher crusher;
    crusher.prepare (kSampleRate);
    crusher.setBits (3.0f);
    crusher.setRateHz (1500.0f);
    crusher.setLowpassHz (900.0f);
    crusher.setJitter01 (1.0f);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    float peak = 0.0f;

    for (int b = 0; b < static_cast<int> (kSampleRate * 2.0 / kBlock); ++b)
    {
        for (auto& v : l)
            v = 0.0f;
        for (auto& v : r)
            v = 0.0f;

        crusher.process (l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
    }

    std::printf ("  peak from silent input: %.2e\n", peak);
    check (peak == 0.0f, "bit crusher generates signal from silence");
}

void testBitCrusherQuantises()
{
    std::printf ("Bit crusher: Bits low -> output takes few distinct levels\n");

    const int total = static_cast<int> (kSampleRate / 4);
    std::vector<float> l (total), r (total);
    for (int i = 0; i < total; ++i)
    {
        const float s = 0.9f * std::sin (2.0f * 3.14159265f * 110.0f * static_cast<float> (i)
                                         / static_cast<float> (kSampleRate));
        l[i] = s;
        r[i] = s;
    }

    ee::dsp::BitCrusher crusher;
    crusher.prepare (kSampleRate);
    crusher.setBits (3.0f);                               // 8 levels
    crusher.setRateHz (static_cast<float> (kSampleRate)); // isolate the quantiser
    crusher.setLowpassHz (ee::dsp::bitcrush::kLpMaxHz);   // ... and skip the filter
    crusher.setJitter01 (0.0f);

    for (int off = 0; off < total; off += kBlock)
        crusher.process (l.data() + off, r.data() + off, juce::jmin (kBlock, total - off));

    std::set<int> levels;
    for (int i = 0; i < total; ++i)
        levels.insert (juce::roundToInt (l[i] * 100000.0f));

    std::printf ("  distinct output levels: %d\n", static_cast<int> (levels.size()));
    check (levels.size() <= 10, "3-bit quantiser produced far more than 8 levels");
    check (levels.size() >= 4, "quantiser collapsed the signal to nothing");
}

void testBitCrusherDecimates()
{
    std::printf ("Bit crusher: Rate low -> output is piecewise constant at that rate\n");

    const int total = static_cast<int> (kSampleRate / 4);
    std::vector<float> l (total), r (total);
    for (int i = 0; i < total; ++i)
    {
        const float s = std::sin (2.0f * 3.14159265f * 300.0f * static_cast<float> (i)
                                  / static_cast<float> (kSampleRate));
        l[i] = s;
        r[i] = s;
    }

    const float rateHz = 3000.0f;

    ee::dsp::BitCrusher crusher;
    crusher.prepare (kSampleRate);
    crusher.setBits (ee::dsp::bitcrush::kBitsClean); // isolate the hold
    crusher.setRateHz (rateHz);
    crusher.setLowpassHz (ee::dsp::bitcrush::kLpMaxHz);
    crusher.setJitter01 (0.0f);

    for (int off = 0; off < total; off += kBlock)
        crusher.process (l.data() + off, r.data() + off, juce::jmin (kBlock, total - off));

    int runs = 1;
    for (int i = 1; i < total; ++i)
        if (l[i] != l[i - 1])
            ++runs;

    const double meanRun = static_cast<double> (total) / runs;
    const double expected = kSampleRate / rateHz;

    std::printf ("  mean run length %.2f samples, expected ~%.2f\n", meanRun, expected);
    check (meanRun > expected * 0.6 && meanRun < expected * 1.6, "hold rate is not close to the Rate setting");
}

void testBitCrusherStability()
{
    std::printf ("Bit crusher: sustained noise, everything hot -> finite and bounded\n");

    ee::dsp::BitCrusher crusher;
    crusher.prepare (kSampleRate);
    crusher.setBits (1.0f);
    crusher.setDecimation (ee::dsp::bitcrush::kMaxDecimation);
    crusher.setLowpassHz (ee::dsp::bitcrush::kLpMinHz);
    crusher.setJitter01 (1.0f);

    std::mt19937 rng (0xd1ceu);
    std::normal_distribution<float> dist (0.0f, 0.35f);

    std::vector<float> l (kBlock), r (kBlock);
    bool finite = true;
    float peak = 0.0f;

    for (int b = 0; b < static_cast<int> (kSampleRate * 3.0 / kBlock); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            l[i] = std::tanh (dist (rng));
            r[i] = std::tanh (dist (rng));
        }

        crusher.process (l.data(), r.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
    }

    std::printf ("  peak %.3f\n", peak);
    check (finite, "bit crusher produced a non-finite sample");
    check (peak < 2.0f, "bit crusher ran away");
}

void testBitCrusherJitterIsReproducible()
{
    std::printf ("Bit crusher: Jitter renders identically on a second run\n");

    const int total = static_cast<int> (kSampleRate / 2);
    std::vector<float> src (total);
    for (int i = 0; i < total; ++i)
        src[i] = std::sin (2.0f * 3.14159265f * 180.0f * static_cast<float> (i) / static_cast<float> (kSampleRate));

    auto render = [&] (std::vector<float>& out)
    {
        ee::dsp::BitCrusher crusher;
        crusher.prepare (kSampleRate);
        crusher.setBits (8.0f);
        crusher.setRateHz (3500.0f);
        crusher.setLowpassHz (ee::dsp::bitcrush::kLpMaxHz);
        crusher.setJitter01 (0.8f);

        out = src;
        std::vector<float> r (out);
        for (int off = 0; off < total; off += kBlock)
        {
            const int n = juce::jmin (kBlock, total - off);
            crusher.process (out.data() + off, r.data() + off, n);
        }
    };

    std::vector<float> a, b;
    render (a);
    render (b);

    float worst = 0.0f;
    for (int i = 0; i < total; ++i)
        worst = juce::jmax (worst, std::abs (a[i] - b[i]));

    std::printf ("  largest difference between runs: %.2e\n", worst);
    check (worst == 0.0f, "jitter is not reproducible run to run");
}

// ---------------------------------------------------------------------------
// BitBitLimiter
// ---------------------------------------------------------------------------

void testBitBitLimiterSilence()
{
    std::printf ("Peak limiter: silence in -> silence out\n");

    ee::dsp::BitBitLimiter limiter;
    limiter.prepare (kSampleRate);
    limiter.setCeilingDb (ee::dsp::config::kLimiterCeilingDb);
    limiter.setAttackMs (ee::dsp::config::kLimiterAttackMs);
    limiter.setReleaseMs (ee::dsp::config::kLimiterReleaseMs);

    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    for (int b = 0; b < 10; ++b)
        limiter.process (l.data(), r.data(), kBlock);

    bool allZero = true;
    for (int i = 0; i < kBlock; ++i)
        allZero = allZero && l[i] == 0.0f && r[i] == 0.0f;

    check (allZero, "limiter produced sound from silence");
}

void testBitBitLimiterTransparentBelowCeiling()
{
    std::printf ("Peak limiter: leaves a signal under the ceiling untouched\n");

    ee::dsp::BitBitLimiter limiter;
    limiter.prepare (kSampleRate);
    limiter.setCeilingDb (ee::dsp::config::kLimiterCeilingDb);
    limiter.setAttackMs (ee::dsp::config::kLimiterAttackMs);
    limiter.setReleaseMs (ee::dsp::config::kLimiterReleaseMs);

    const int total = static_cast<int> (kSampleRate / 2);
    std::vector<float> l (total), r (total);
    for (int i = 0; i < total; ++i)
    {
        // A steady tone well under the ceiling (-6 dBFS peak).
        l[i] = r[i] = 0.5f * std::sin (2.0f * 3.14159265f * 440.0f * static_cast<float> (i) /
                                       static_cast<float> (kSampleRate));
    }

    std::vector<float> original = l;
    for (int off = 0; off < total; off += kBlock)
    {
        const int n = juce::jmin (kBlock, total - off);
        limiter.process (l.data() + off, r.data() + off, n);
    }

    float worst = 0.0f;
    for (int i = 0; i < total; ++i)
        worst = juce::jmax (worst, std::abs (l[i] - original[i]));

    std::printf ("  largest change below ceiling: %.2e\n", worst);
    check (worst < 1.0e-4f, "limiter touched a signal that never approached the ceiling");
}

void testBitBitLimiterCapsAStackedPeak()
{
    std::printf ("Peak limiter: caps a transient stacked over the ceiling\n");

    ee::dsp::BitBitLimiter limiter;
    limiter.prepare (kSampleRate);
    limiter.setCeilingDb (ee::dsp::config::kLimiterCeilingDb);
    limiter.setAttackMs (ee::dsp::config::kLimiterAttackMs);
    limiter.setReleaseMs (ee::dsp::config::kLimiterReleaseMs);

    // A burst well past the ceiling, as if a grain had landed back on the
    // transient that spawned it - the case this limiter exists for.
    const int total = static_cast<int> (kSampleRate * 0.05);
    std::vector<float> l (total), r (total);
    for (int i = 0; i < total; ++i)
        l[i] = r[i] = 1.8f * std::sin (2.0f * 3.14159265f * 200.0f * static_cast<float> (i) /
                                       static_cast<float> (kSampleRate));

    const float ceiling = juce::Decibels::decibelsToGain (ee::dsp::config::kLimiterCeilingDb);
    float worstOver = 0.0f;
    bool finite = true;
    for (int off = 0; off < total; off += kBlock)
    {
        const int n = juce::jmin (kBlock, total - off);
        limiter.process (l.data() + off, r.data() + off, n);
        for (int i = off; i < off + n; ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            worstOver = juce::jmax (worstOver, std::abs (l[i]) - ceiling);
        }
    }

    std::printf ("  worst excursion above ceiling: %.4f\n", worstOver);
    check (finite, "limiter produced a non-finite sample");
    // No lookahead, so a fast-rising peak can poke slightly over the ceiling
    // before the envelope catches it - allow a small margin, not zero.
    check (worstOver < 0.05f, "limiter let a stacked peak through uncontrolled");
}

// BitBit Grain's Mod tab LFO. sr=1000, period=1s gives a clean 0.001-per-sample
// phase increment, so a block of N samples lands on phase N/1000 exactly -
// every test below picks N to land on the phase it wants to check, rather
// than approximating.
ee::dsp::Tremolo::Transport unsyncedLfoTransport()
{
    ee::dsp::Tremolo::Transport t;
    t.synced = false;
    t.playing = false;
    return t;
}

void testBreakpointLfoLinearSegment()
{
    std::printf ("Breakpoint LFO: linear segment midpoint:\n");

    ee::dsp::BreakpointLfo lfo;
    lfo.prepare (1000.0);
    lfo.setPeriodSeconds (1.0f);
    lfo.setBreakpoints ({ { 0.0f, 0.0f, 0.0f, false }, { 0.5f, 1.0f, 0.0f, false } });

    lfo.advance (250, unsyncedLfoTransport()); // phase 0.25, halfway through 0 -> 0.5

    std::printf ("  value at phase 0.25: %.4f\n", lfo.currentValue());
    check (std::abs (lfo.currentValue() - 0.5f) < 1.0e-4f, "linear segment midpoint is not the arithmetic mean");
}

void testBreakpointLfoHoldSteps()
{
    std::printf ("Breakpoint LFO: hold segments step rather than interpolate:\n");

    ee::dsp::BreakpointLfo lfo;
    lfo.prepare (1000.0);
    lfo.setPeriodSeconds (1.0f);
    lfo.setBreakpoints ({ { 0.0f, 1.0f, 0.0f, true }, { 0.5f, -1.0f, 0.0f, true } });

    lfo.reset();
    lfo.advance (250, unsyncedLfoTransport()); // phase 0.25, inside the first hold
    check (std::abs (lfo.currentValue() - 1.0f) < 1.0e-4f, "hold segment drifted off its own value");

    lfo.reset();
    lfo.advance (750, unsyncedLfoTransport()); // phase 0.75, inside the second hold
    check (std::abs (lfo.currentValue() - (-1.0f)) < 1.0e-4f, "hold segment did not jump to the next point's value");
}

void testBreakpointLfoWraps()
{
    std::printf ("Breakpoint LFO: the wrap segment (last point back to the first):\n");

    ee::dsp::BreakpointLfo lfo;
    lfo.prepare (1000.0);
    lfo.setPeriodSeconds (1.0f);
    // First point at x=0.2 (not 0), so phase 0.1 falls in the wrap segment:
    // last (0.8, -1) back to first-plus-a-cycle (1.2, 1). t = (1.1-0.8)/0.4 = 0.75.
    lfo.setBreakpoints ({ { 0.2f, 1.0f, 0.0f, false }, { 0.8f, -1.0f, 0.0f, false } });

    lfo.advance (100, unsyncedLfoTransport()); // phase 0.1

    std::printf ("  value at phase 0.1 (in the wrap segment): %.4f\n", lfo.currentValue());
    check (std::abs (lfo.currentValue() - 0.5f) < 1.0e-4f, "wrap segment does not interpolate back to the first point");
}

void testBreakpointLfoPhaseWrapsOverManyBlocks()
{
    std::printf ("Breakpoint LFO: phase wraps to [0, 1) over many blocks:\n");

    ee::dsp::BreakpointLfo lfo;
    lfo.prepare (kSampleRate);
    lfo.setPeriodSeconds (0.037f); // an awkward period, deliberately not a clean divisor of the block
    lfo.setBreakpoints ({ { 0.0f, 0.0f, 0.0f, false }, { 0.5f, 1.0f, 0.0f, false } });

    bool finite = true;
    for (int b = 0; b < static_cast<int> (kSampleRate * 5.0 / kBlock); ++b)
    {
        lfo.advance (kBlock, unsyncedLfoTransport());
        finite = finite && std::isfinite (lfo.phase01()) && std::isfinite (lfo.currentValue());
    }

    const float phase = lfo.phase01();
    std::printf ("  phase after 5 s free-running: %.4f\n", phase);
    check (finite, "phase or value went non-finite over many blocks");
    check (phase >= 0.0f && phase < 1.0f, "phase escaped [0, 1)");
}

void testBreakpointLfoSelfHeals()
{
    std::printf ("Breakpoint LFO: recovers from a non-finite host ppq:\n");

    ee::dsp::BreakpointLfo lfo;
    lfo.prepare (1000.0);
    lfo.setPeriodSeconds (1.0f);
    lfo.setBreakpoints ({ { 0.0f, 0.0f, 0.0f, false }, { 0.5f, 1.0f, 0.0f, false } });

    ee::dsp::Tremolo::Transport poisoned;
    poisoned.synced = true;
    poisoned.playing = true;
    poisoned.ppqStart = std::numeric_limits<double>::quiet_NaN();
    poisoned.cyclesPerQuarter = 1.0;
    poisoned.ppqPerSample = 0.001;

    lfo.advance (100, poisoned); // first synced block is a hard "jump" - lands on the poisoned target

    check (std::isfinite (lfo.phase01()), "phase stayed non-finite after a poisoned host ppq");
    check (std::isfinite (lfo.currentValue()), "value stayed non-finite after a poisoned host ppq");
}

void testModRouterAssignments()
{
    std::printf ("Mod router: assignment lookup and the 8-slot cap:\n");

    ee::plugin::ModRouter router;
    check (! router.hasAssignments(), "a fresh router already has assignments");
    check (router.depthFor ("size") == 0.0f, "an unassigned parameter is not silently non-zero");

    router.setAssignments ({ { "size", 0.5f }, { "density", -0.25f } });
    check (router.hasAssignments(), "hasAssignments did not notice a real assignment");
    check (std::abs (router.depthFor ("size") - 0.5f) < 1.0e-6f, "depth for an assigned parameter is wrong");
    check (std::abs (router.depthFor ("density") - (-0.25f)) < 1.0e-6f, "a negative depth did not round-trip");
    check (router.depthFor ("window") == 0.0f, "a third, unassigned parameter is not zero");

    std::vector<ee::plugin::ModAssignment> tooMany;
    for (int i = 0; i < ee::plugin::ModRouter::kMaxAssignments + 3; ++i)
        tooMany.push_back ({ "p" + juce::String (i), 0.1f });
    router.setAssignments (tooMany);

    const auto current = router.currentAssignments();
    std::printf ("  assignments after handing it %d: %d\n", static_cast<int> (tooMany.size()),
                static_cast<int> (current.size()));
    check (static_cast<int> (current.size()) == ee::plugin::ModRouter::kMaxAssignments,
          "more assignments than the cap were not trimmed to it");

    router.setAssignments ({});
    check (! router.hasAssignments(), "clearing the assignment list left something behind");
}

} // namespace

int main()
{
    std::printf ("=== BitBit DSP tests ===\n\n");

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
    testShimmerReproducible();
    std::printf ("\n");
    testShimmerOctaves();
    std::printf ("\n");
    testStudioSizeSweepDoesNotClick();
    std::printf ("\n");
    testDelayTaps();
    std::printf ("\n");
    testDelayStability();
    std::printf ("\n");
    testDelayTimeChangeStaysOutOfTheLoop();
    std::printf ("\n");
    testTapeRouterDoesNotSplice();
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
    testTapeMachineLowLatency();
    std::printf ("\n");
    testModDelayLineWrapBoundary();
    testSafeParse();
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
    testAutoWahRetriggerSoftAfterLoud();
    std::printf ("\n");
    testAutoWahLowNoteTriggersOnce();
    std::printf ("\n");
    testAutoWahQuietNoteTriggers();
    std::printf ("\n");
    testAutoWahHoldsLevel();
    std::printf ("\n");
    testAutoWahMixRampIsSmooth();
    std::printf ("\n");
    testRingModulator();
    std::printf ("\n");
    testRust();
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
    std::printf ("\n");
    testGrainerSilence();
    std::printf ("\n");
    testGrainerFiniteUnderSweep();
    std::printf ("\n");
    testGrainerShapeFamiliesAreFiniteUnderSweep();
    std::printf ("\n");
    testGrainerShapeFamiliesCloseToZero();
    std::printf ("\n");
    testGrainerReadsStayBehindTheWriteHead();
    std::printf ("\n");
    testGrainerLevelHoldsAcrossDensity();
    std::printf ("\n");
    testGrainerLevelJitterHoldsTheCloudLevel();
    std::printf ("\n");
    testGrainerFeedbackLengthensTail();
    std::printf ("\n");
    testGrainerFeedbackStaysFinite();
    std::printf ("\n");
    testGrainerAttackCapture();
    std::printf ("\n");
    testGrainerGridFrozenCapturesEachBar();
    std::printf ("\n");
    testGrainerGridLiveTapsWholeSixteenths();
    testGrainerFollowDensityReadsOnTheGrid();
    testGrainerSmoothChangeDoesNotJumpTheLevel();
    testGrainerShapeChangeDoesNotJumpTheLevel();
    std::printf ("\n");
    testGrainerShapeFamilyChangeDoesNotJumpTheLevel();
    std::printf ("\n");
    testGrainerAttackLeadsWithOctaves();
    std::printf ("\n");
    testGrainerStereoIsBalanced();
    std::printf ("\n");
    testGrainerWideZeroIsMonoWhenSprayIsAlsoClosed();
    std::printf ("\n");
    testGrainerWideAlternatesSidesWhenSprayIsClosed();
    std::printf ("\n");
    testGrainerSprayOverridesWide();
    std::printf ("\n");
    testGrainerTailStops();
    std::printf ("\n");
    testGrainerPitchIsActuallyApplied();
    std::printf ("\n");
    testGrainerHighWithNoScaleIsAnOctaveUp();
    std::printf ("\n");
    testGrainerCloudFilterClosesAndDefaultsOpen();
    std::printf ("\n");
    testGrainerLowGroupIsOctavesOnly();
    std::printf ("\n");
    testGrainerHighGroupIsTheScaleAnOctaveUp();
    std::printf ("\n");
    testGrainerScaleRootRotatesThePattern();
    std::printf ("\n");
    testGrainerFreezeHoldsAndRetriggers();
    std::printf ("\n");
    testGrainerStretchScrubsFrozenBuffer();
    std::printf ("\n");
    testBitCrusherTransparentAtOff();
    std::printf ("\n");
    testBitCrusherSilence();
    std::printf ("\n");
    testBitCrusherQuantises();
    std::printf ("\n");
    testBitCrusherDecimates();
    std::printf ("\n");
    testBitCrusherStability();
    std::printf ("\n");
    testBitCrusherJitterIsReproducible();
    std::printf ("\n");
    testBitBitLimiterSilence();
    std::printf ("\n");
    testBitBitLimiterTransparentBelowCeiling();
    std::printf ("\n");
    testBitBitLimiterCapsAStackedPeak();
    std::printf ("\n");
    testBreakpointLfoLinearSegment();
    std::printf ("\n");
    testBreakpointLfoHoldSteps();
    std::printf ("\n");
    testBreakpointLfoWraps();
    std::printf ("\n");
    testBreakpointLfoPhaseWrapsOverManyBlocks();
    std::printf ("\n");
    testBreakpointLfoSelfHeals();
    std::printf ("\n");
    testModRouterAssignments();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
