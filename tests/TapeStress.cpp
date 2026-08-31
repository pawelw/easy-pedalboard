// Sweeps the tape machine over every knob combination and adverse input,
// watching for a non-finite or runaway output. The engine carries a recursive
// replay-EQ filter whose coefficients are derived from the Saturation knob, and
// feeds a tape stage of its own behind that - a combination that solves to an
// unstable pole would only show up at a setting nobody happened to try by hand.
#include "ee/dsp/TapeMachine.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
    struct Result { float peak = 0.0f; bool nonFinite = false; };

    Result runCase (double sampleRate, int blockSize, float wear, float flutter,
                    float tone, float stereo, float noise, float saturation,
                    int inputKind, int blocks)
    {
        ee::dsp::TapeMachine machine;
        machine.prepare (sampleRate);
        machine.reset();
        machine.setWear01 (wear);
        machine.setFlutter01 (flutter);
        machine.setTone (tone);
        machine.setStereo01 (stereo);
        machine.setNoise01 (noise);
        machine.setSaturation01 (saturation);

        std::vector<float> l (static_cast<size_t> (blockSize));
        std::vector<float> r (static_cast<size_t> (blockSize));

        Result result;
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
                        // The machine must recover, not roar forever.
                        if (b == 1)
                            s = (i % 2 == 0) ? std::numeric_limits<float>::quiet_NaN()
                                             : std::numeric_limits<float>::infinity();
                        else
                            s = 0.6f * (float) std::sin (phase);
                        break;
                    default:
                        // A lone NaN dropped in once the filters are established.
                        s = (b == blocks / 2 && i == 3) ? std::numeric_limits<float>::quiet_NaN()
                                                        : 0.5f * (float) std::sin (phase);
                        break;
                }

                phase += inc;
                l[static_cast<size_t> (i)] = s;
                r[static_cast<size_t> (i)] = s;
            }

            machine.process (l.data(), r.data(), blockSize);

            // The blocks the garbage lands in are allowed to carry it through;
            // everything after them is not.
            const bool scoring = ! ((inputKind == 5 && b <= 2)
                                    || (inputKind == 6 && b >= blocks / 2 && b <= blocks / 2 + 1));

            if (! scoring)
                continue;

            for (int i = 0; i < blockSize; ++i)
            {
                const float a = std::fabs (l[static_cast<size_t> (i)]);
                const float bb = std::fabs (r[static_cast<size_t> (i)]);

                if (! std::isfinite (a) || ! std::isfinite (bb))
                    result.nonFinite = true;
                else
                    result.peak = std::fmax (result.peak, std::fmax (a, bb));
            }
        }

        return result;
    }
}

int main()
{
    std::printf ("=== tape machine stress ===\n");

    int cases = 0, bad = 0;
    float worstPeak = 0.0f;

    const double rates[]   = { 44100.0, 96000.0 };
    const int    blocks[]  = { 64, 512 };
    const float  amounts[] = { 0.0f, 0.5f, 1.0f };
    const float  tones[]   = { -1.0f, 0.0f, 1.0f };
    const float  stereos[] = { 0.0f, 1.0f };

    for (double sr : rates)
      for (int blk : blocks)
        for (float wear : amounts)
          for (float flutter : amounts)
            for (float tone : tones)
              for (float stereo : stereos)
                for (float sat : amounts)
                  for (int in = 0; in < 7; ++in)
                {
                    // Noise cannot destabilise anything on its own - it is
                    // added, not fed back - so it rides along at full rather
                    // than multiplying the sweep out.
                    const auto r = runCase (sr, blk, wear, flutter, tone, stereo, 1.0f, sat, in, 40);
                    ++cases;
                    worstPeak = std::fmax (worstPeak, r.peak);

                    // The hot-spike case feeds +/-4, and with Saturation at 0
                    // nothing in the chain limits: Tone fully bright alone is a
                    // 1.9x lift on the high band. Anything past 16 is a runaway,
                    // not arithmetic.
                    if (r.nonFinite || r.peak > 16.0f)
                    {
                        ++bad;
                        std::printf ("  !!! peak=%.3f nonFinite=%d  (sr=%.0f blk=%d wear=%.1f flutter=%.1f tone=%+.1f stereo=%.0f sat=%.1f in=%d)\n",
                                     r.peak, (int) r.nonFinite, sr, blk, wear, flutter, tone, stereo, sat, in);
                    }
                }

    std::printf ("\n%d cases, worst peak %.3f, %d flagged\n", cases, worstPeak, bad);
    std::printf ("%s\n", bad == 0 ? "OK - nothing exploded" : "FAIL - see flagged cases above");
    return bad == 0 ? 0 : 1;
}
