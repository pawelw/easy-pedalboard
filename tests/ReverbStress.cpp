// Hammers the FDN reverb with adverse input and extreme settings, watching for
// a non-finite or runaway wet output - the "exploding tail" bug. The reverb is
// a feedback network, so a single NaN/Inf that gets in is stored in the delay
// lines and roars until a reset; this guards the scrub-and-recover path that
// stops that.
#include "ee/dsp/FdnReverb.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
    struct Result { float peak = 0.0f; bool nonFinite = false; };

    Result runCase (double sampleRate, int blockSize, float decay, float resonance,
                    float shimmer, float lowCut, int inputKind, int blocks)
    {
        ee::dsp::FdnReverb reverb;
        reverb.prepare (sampleRate);
        reverb.setDecayTime (decay);
        reverb.setResonance (resonance);
        reverb.setShimmer (shimmer);
        reverb.setLowCut (lowCut);

        std::vector<float> mono (static_cast<size_t> (blockSize));
        std::vector<float> wetL (static_cast<size_t> (blockSize));
        std::vector<float> wetR (static_cast<size_t> (blockSize));

        Result r;
        double phase = 0.0;
        const double inc = 3.14159265358979 * 2.0 * 220.0 / sampleRate;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                float s = 0.0f;
                switch (inputKind)
                {
                    case 0: s = 0.9f * (float) std::sin (phase); break;                       // loud sine
                    case 1: s = (i == 0 && b == 0) ? 1.0f : 0.0f; break;                      // one impulse
                    case 2: s = 1.0f; break;                                                  // full-scale DC
                    case 3: s = (b % 5 == 0) ? 4.0f : 0.2f * (float) std::sin (phase); break; // hot spikes
                    case 4: s = 0.0f; break;                                                  // silence
                    case 5:
                        // A burst of non-finite garbage early, then a clean sine.
                        // The tail must recover, not roar forever.
                        if (b == 1)
                            s = (i % 2 == 0) ? std::numeric_limits<float>::quiet_NaN()
                                             : std::numeric_limits<float>::infinity();
                        else
                            s = 0.6f * (float) std::sin (phase);
                        break;
                    case 6:
                        // A lone NaN dropped in much later, once the tail is
                        // established - the case that actually bit in the DAW.
                        s = (b == blocks / 2 && i == 3) ? std::numeric_limits<float>::quiet_NaN()
                                                        : 0.5f * (float) std::sin (phase);
                        break;
                }
                mono[static_cast<size_t> (i)] = s;
                phase += inc;
            }

            // Move the knobs mid-run: parameter changes re-derive the loop gains.
            if (b == blocks / 3)      reverb.setShimmer (shimmer > 0.5f ? 0.0f : 1.0f);
            if (b == blocks / 2)      reverb.setResonance (resonance > 0.5f ? 0.0f : 1.0f);
            if (b == (2 * blocks) / 3) reverb.setDecayTime (ee::dsp::FdnReverb::kMaxDecay);

            reverb.process (mono.data(), wetL.data(), wetR.data(), blockSize);

            // A single bad input block may glitch that block; the tail must be
            // fully recovered by the end.
            const bool tail = b >= (3 * blocks) / 4;
            if (! tail)
                continue;

            for (int i = 0; i < blockSize; ++i)
                for (const float v : { wetL[static_cast<size_t> (i)], wetR[static_cast<size_t> (i)] })
                {
                    if (! std::isfinite (v)) r.nonFinite = true;
                    r.peak = std::fmax (r.peak, std::fabs (v));
                }
        }

        return r;
    }

    // Excite the network, then feed it silence for minutes. A linear reverb fed
    // silence is stone dead many RT60s later; if it is still making sound well
    // past that, the round-trip gain has crept over unity somewhere - the
    // "roars after half an hour" bug, which the 40-block sweep above is far too
    // short to see. `settledPeak` is the loudest sample in the last fifth of the
    // run (>> 6*decay of silence by then); on a healthy reverb it is basically
    // the denormal floor. `peak` is the loudest sample anywhere in the tail.
    struct GrowthResult { float settledPeak = 0.0f; float peak = 0.0f; bool nonFinite = false; };

    GrowthResult runDecayGrowth (double sampleRate, int blockSize, float decay, float resonance,
                                 float exciteSeconds, float tailSeconds)
    {
        ee::dsp::FdnReverb reverb;
        reverb.prepare (sampleRate);
        reverb.setDecayTime (decay);
        reverb.setResonance (resonance);
        reverb.setShimmer (0.0f);
        reverb.setLowCut (ee::dsp::FdnReverb::kMinLowCutHz);

        std::vector<float> mono (static_cast<size_t> (blockSize));
        std::vector<float> wetL (static_cast<size_t> (blockSize));
        std::vector<float> wetR (static_cast<size_t> (blockSize));

        const int exciteBlocks = static_cast<int> (exciteSeconds * sampleRate / blockSize);
        const int tailBlocks   = static_cast<int> (tailSeconds   * sampleRate / blockSize);
        const int settledFrom  = exciteBlocks + (4 * tailBlocks) / 5;

        GrowthResult g;
        double phase = 0.0;
        const double inc = 3.14159265358979 * 2.0 * 180.0 / sampleRate;

        for (int b = 0; b < exciteBlocks + tailBlocks; ++b)
        {
            const bool exciting = b < exciteBlocks;
            for (int i = 0; i < blockSize; ++i)
            {
                mono[static_cast<size_t> (i)] = exciting ? 0.5f * (float) std::sin (phase) : 0.0f;
                phase += inc;
            }

            reverb.process (mono.data(), wetL.data(), wetR.data(), blockSize);

            if (exciting)
                continue;

            for (int i = 0; i < blockSize; ++i)
                for (const float v : { wetL[static_cast<size_t> (i)], wetR[static_cast<size_t> (i)] })
                {
                    if (! std::isfinite (v)) g.nonFinite = true;
                    const float a = std::fabs (v);
                    g.peak = std::fmax (g.peak, a);
                    if (b >= settledFrom)
                        g.settledPeak = std::fmax (g.settledPeak, a);
                }
        }

        return g;
    }
}

int main()
{
    std::printf ("=== FDN reverb stress ===\n");

    int cases = 0, bad = 0;
    float worstPeak = 0.0f;

    const double rates[]  = { 48000.0, 96000.0 };
    const int    blocks[] = { 64, 512 };
    const float  decays[] = { ee::dsp::FdnReverb::kMinDecay, 2.0f, ee::dsp::FdnReverb::kMaxDecay };
    const float  amounts[] = { 0.0f, 0.5f, 1.0f };
    const float  lowCuts[] = { ee::dsp::FdnReverb::kMinLowCutHz, 200.0f };

    for (double sr : rates)
      for (int blk : blocks)
        for (float decay : decays)
          for (float res : amounts)
            for (float shim : amounts)
              for (float lc : lowCuts)
                for (int in = 0; in < 7; ++in)
                {
                    const auto r = runCase (sr, blk, decay, res, shim, lc, in, 40);
                    ++cases;
                    worstPeak = std::fmax (worstPeak, r.peak);
                    if (r.nonFinite || r.peak > 16.0f)
                    {
                        ++bad;
                        std::printf ("  !!! peak=%.3f nonFinite=%d  (sr=%.0f blk=%d decay=%.2f res=%.2f shim=%.2f lowcut=%.0f in=%d)\n",
                                     r.peak, (int) r.nonFinite, sr, blk, decay, res, shim, lc, in);
                    }
                }

    std::printf ("\n%d cases, worst tail peak %.3f, %d flagged\n", cases, worstPeak, bad);

    // ---- long-duration decay: the "roars after minutes" bug -----------------
    std::printf ("\n=== long-duration decay (silence tail must not grow) ===\n");

    // The incident settings clustered at long decay / mid resonance (stiller
    // lines); kMaxDecay and reso 1.0 are the highest-loop-gain corners on top.
    const float longDecays[] = { 3.3f, 4.0f, ee::dsp::FdnReverb::kMaxDecay };
    const float longResos[]  = { 0.50f, 0.56f, 1.0f };

    for (float decay : longDecays)
        for (float res : longResos)
        {
            const auto g = runDecayGrowth (48000.0, 512, decay, res, 0.5f, 180.0f);
            ++cases;
            worstPeak = std::fmax (worstPeak, g.peak);

            // 180 s of silence is >> 6*decay even at the 8 s max, so a healthy
            // tail is long gone. Anything still audible then is a runaway.
            const bool alive = g.settledPeak > 1.0e-4f;
            if (g.nonFinite || alive || g.peak > 16.0f)
            {
                ++bad;
                std::printf ("  !!! decay=%.2f res=%.2f  settledPeak=%.3e peak=%.3f nonFinite=%d\n",
                             decay, res, g.settledPeak, g.peak, (int) g.nonFinite);
            }
            else
            {
                std::printf ("  ok   decay=%.2f res=%.2f  settledPeak=%.3e peak=%.3f\n",
                             decay, res, g.settledPeak, g.peak);
            }
        }

    std::printf ("\n%d cases, worst tail peak %.3f, %d flagged\n", cases, worstPeak, bad);
    std::printf ("%s\n", bad == 0 ? "OK - nothing exploded" : "FAIL - see flagged cases above");
    return bad == 0 ? 0 : 1;
}
