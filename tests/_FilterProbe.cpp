#include "ee/dsp/AutoWah.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
struct R { double inRms, outRms, outPeak; };
static R run (int kind, float q01, float range01, float freq01, float period)
{
    const double sr = 48000.0;
    ee::dsp::AutoWah w; w.prepare (sr);
    w.setDecay01 (1.0f); w.setTypeMorph01 (0.0f); w.setMix01 (1.0f);
    w.setFreq01 (freq01); w.setQ01 (q01); w.setRange01 (range01); w.setShape01 (0.5f);
    w.setPeriodSeconds (period); w.setStereo (false);
    std::mt19937 rng (1); std::normal_distribution<float> nd;
    std::vector<float> l (512), r (512);
    double ph = 0, si = 0, so = 0, pk = 0, b0 = 0; long n = 0, cnt = 0;
    for (int b = 0; b < (int) (sr * 8 / 512); ++b)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            float x;
            if (kind == 0) { ph += 110.0 / sr; ph -= std::floor (ph); x = 0.4f * (float) (2 * ph - 1); }   // saw, guitar-ish, -1..
            else { b0 = 0.97 * b0 + nd (rng) * 0.1; x = (float) b0 * 0.6f; }                                // dark noise
            l[i] = r[i] = x;
        }
        std::vector<float> inCopy = l;
        w.process (l.data(), r.data(), 512);
        if (b > 40) for (int i = 0; i < 512; ++i) { si += inCopy[i] * inCopy[i]; so += l[i] * l[i]; pk = std::max (pk, (double) std::abs (l[i])); ++cnt; }
    }
    return { std::sqrt (si / cnt), std::sqrt (so / cnt), pk };
}
int main()
{
    std::printf ("Filter engine (LP tap, as in the Modulation module). gain = out rms / in rms (dB), peak = out peak dBFS\n");
    for (int kind : { 0, 1 })
    {
        std::printf ("\n%s input\n", kind == 0 ? "110 Hz saw (0.4)" : "dark noise");
        std::printf ("  Range   Freq |");
        for (float q : { 0.f, 0.25f, 0.5f, 0.75f, 1.f }) std::printf ("   Q %3.0f%%      ", q * 100);
        std::printf ("\n");
        for (float range : { 0.f, 0.6f, 1.f })
            for (float freq : { 0.2f, 0.5f, 0.8f })
            {
                std::printf ("  %4.0f%%  %4.0f%% |", range * 100, freq * 100);
                for (float q : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
                {
                    auto r = run (kind, q, range, freq, 0.6f);
                    std::printf ("  %+5.1f / %+5.1f", 20 * std::log10 (r.outRms / r.inRms), 20 * std::log10 (r.outPeak + 1e-9));
                }
                std::printf ("\n");
            }
    }
}
