// Hammers ee::dsp::ResonatorBank with adverse input and extreme settings,
// watching for a non-finite or runaway output. Sixteen near-unity feedback
// loops plus a coupling matrix is exactly the shape that grows a tail without
// ever going non-finite, so level is checked as hard as finiteness. The
// failure mode the design most invites: Coupling and Decay both at maximum,
// Freeze on.
#include "ee/dsp/ResonatorBank.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
using Mode = ee::dsp::sympathy::TuningMode;

struct Result
{
    float peak = 0.0f;
    bool nonFinite = false;
};

Result runCase (double sr, int blockSize, Mode mode, int octave, float decay01, float damping01,
                float coupling01, float spread01, float bloom01, bool freeze, int inputKind, int blocks)
{
    ee::dsp::ResonatorBank bank;
    bank.prepare (sr);
    bank.reset();
    bank.setTuning (mode, 9);
    bank.setOctave (octave);
    bank.setDecay01 (decay01);
    bank.setDamping01 (damping01);
    bank.setCoupling01 (coupling01);
    bank.setSpread01 (spread01);
    bank.setBloom01 (bloom01);
    bank.setSensitivity01 (0.75f);
    bank.setFreeze (freeze);

    std::vector<float> in (static_cast<size_t> (blockSize));
    std::vector<float> wetL (static_cast<size_t> (blockSize));
    std::vector<float> wetR (static_cast<size_t> (blockSize));

    Result r;
    double phase = 0.0;
    const double inc = 2.0 * 3.14159265358979 * 196.0 / sr;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            float s = 0.0f;
            switch (inputKind)
            {
                case 0: s = 0.9f * static_cast<float> (std::sin (phase)); break;
                case 1: s = (i == 0 && b == 0) ? 1.0f : 0.0f; break;                       // one impulse
                case 2: s = 1.0f; break;                                                  // full-scale DC
                case 3: s = (b % 4 == 0 && i < 32) ? 6.0f : 0.15f * static_cast<float> (std::sin (phase)); break;
                case 4: s = 0.0f; break;                                                  // silence
                case 5:
                    if (b == 1)
                        s = (i % 2) ? std::numeric_limits<float>::quiet_NaN()
                                    : std::numeric_limits<float>::infinity();
                    else
                        s = 0.6f * static_cast<float> (std::sin (phase));
                    break;
                case 6:
                    s = (b == blocks / 2 && i == 5) ? std::numeric_limits<float>::quiet_NaN()
                                                    : 0.5f * static_cast<float> (std::sin (phase));
                    break;
            }
            in[static_cast<size_t> (i)] = s;
            phase += inc;
        }

        // Move the knobs mid-run - every setter re-derives coefficients.
        if (b == blocks / 3) bank.setCoupling01 (coupling01 > 0.5f ? 0.0f : 1.0f);
        if (b == blocks / 2) bank.setDamping01 (damping01 > 0.5f ? 0.0f : 1.0f);
        if (b == (2 * blocks) / 3) bank.setDecay01 (1.0f);

        bank.updateBlock (blockSize);
        bank.render (in.data(), in.data(), wetL.data(), wetR.data(), nullptr, blockSize);

        const bool tail = b >= (3 * blocks) / 4;
        if (! tail)
            continue;

        for (int i = 0; i < blockSize; ++i)
            for (const float v : { wetL[static_cast<size_t> (i)], wetR[static_cast<size_t> (i)] })
            {
                if (! std::isfinite (v))
                    r.nonFinite = true;
                r.peak = std::fmax (r.peak, std::fabs (v));
            }
    }

    return r;
}

// Excite hard, then feed silence for minutes at the highest-loop-gain corner.
// A healthy bank fed silence is gone a few RT60s later; still audible after
// three minutes means the coupling loop has crept over unity.
struct GrowthResult
{
    float settledPeak = 0.0f;
    float peak = 0.0f;
    bool nonFinite = false;
};

GrowthResult runGrowth (double sr, int blockSize, Mode mode, float coupling01, bool freeze,
                        float exciteSeconds, float tailSeconds)
{
    ee::dsp::ResonatorBank bank;
    bank.prepare (sr);
    bank.reset();
    bank.setTuning (mode, 9);
    bank.setDecay01 (1.0f);
    bank.setDamping01 (0.65f);
    bank.setCoupling01 (coupling01);
    bank.setSpread01 (0.4f);
    bank.setBloom01 (0.0f);
    bank.setSensitivity01 (0.85f);
    bank.setFreeze (freeze);

    std::vector<float> in (static_cast<size_t> (blockSize));
    std::vector<float> wetL (static_cast<size_t> (blockSize));
    std::vector<float> wetR (static_cast<size_t> (blockSize));

    const int exciteBlocks = static_cast<int> (exciteSeconds * sr / blockSize);
    const int tailBlocks = static_cast<int> (tailSeconds * sr / blockSize);
    const int settledFrom = exciteBlocks + (4 * tailBlocks) / 5;

    GrowthResult g;
    double phase = 0.0;
    const double inc = 2.0 * 3.14159265358979 * 165.0 / sr;

    for (int b = 0; b < exciteBlocks + tailBlocks; ++b)
    {
        const bool exciting = b < exciteBlocks;
        for (int i = 0; i < blockSize; ++i)
        {
            in[static_cast<size_t> (i)] = exciting ? 0.5f * static_cast<float> (std::sin (phase)) : 0.0f;
            phase += inc;
        }

        bank.updateBlock (blockSize);
        bank.render (in.data(), in.data(), wetL.data(), wetR.data(), nullptr, blockSize);

        if (exciting)
            continue;

        for (int i = 0; i < blockSize; ++i)
            for (const float v : { wetL[static_cast<size_t> (i)], wetR[static_cast<size_t> (i)] })
            {
                if (! std::isfinite (v))
                    g.nonFinite = true;
                const float a = std::fabs (v);
                g.peak = std::fmax (g.peak, a);
                if (b >= settledFrom)
                    g.settledPeak = std::fmax (g.settledPeak, a);
            }
    }

    return g;
}
} // namespace

int main()
{
    std::printf ("=== resonator bank stress ===\n");

    int cases = 0, bad = 0;
    float worstPeak = 0.0f;

    const double rates[] = { 44100.0, 96000.0 };
    const int blocks[] = { 32, 512 };
    const Mode modes[] = { Mode::octaves, Mode::harmonic, Mode::majorJI, Mode::chroma };
    const float decays[] = { 0.0f, 0.5f, 1.0f };
    const float couplings[] = { 0.0f, 0.5f, 1.0f };

    for (double sr : rates)
        for (int blk : blocks)
            for (Mode mode : modes)
                for (float decay : decays)
                    for (float coup : couplings)
                        for (int freeze = 0; freeze < 2; ++freeze)
                            for (int in = 0; in < 7; ++in)
                            {
                                const auto r = runCase (sr, blk, mode, freeze ? 1 : 0, decay, 0.55f, coup,
                                                        0.35f, in == 0 ? 0.6f : 0.0f, freeze != 0, in, 48);
                                ++cases;
                                worstPeak = std::fmax (worstPeak, r.peak);
                                if (r.nonFinite || r.peak > 8.0f)
                                {
                                    ++bad;
                                    std::printf ("  !!! peak=%.3f nonFinite=%d  (sr=%.0f blk=%d mode=%d decay=%.1f "
                                                 "coupling=%.1f freeze=%d in=%d)\n",
                                                 r.peak, (int) r.nonFinite, sr, blk, (int) mode, decay, coup, freeze, in);
                                }
                            }

    std::printf ("\n%d cases, worst tail peak %.3f, %d flagged\n", cases, worstPeak, bad);

    std::printf ("\n=== long-duration growth (silence tail must not grow) ===\n");
    for (Mode mode : { Mode::octaves, Mode::harmonic, Mode::majorJI })
        for (float coup : { 0.6f, 1.0f })
            for (int freeze = 0; freeze < 2; ++freeze)
            {
                const auto g = runGrowth (48000.0, 512, mode, coup, freeze != 0, 0.8f, 180.0f);
                ++cases;
                worstPeak = std::fmax (worstPeak, g.peak);

                // Freeze holds energy on purpose, so a frozen bank is *meant* to
                // still be ringing - only check it stays bounded. A live bank
                // must be gone.
                const bool aliveFail = ! freeze && g.settledPeak > 1.0e-3f;
                if (g.nonFinite || aliveFail || g.peak > 12.0f)
                {
                    ++bad;
                    std::printf ("  !!! mode=%d coupling=%.1f freeze=%d  settledPeak=%.3e peak=%.3f nonFinite=%d\n",
                                 (int) mode, coup, freeze, g.settledPeak, g.peak, (int) g.nonFinite);
                }
                else
                {
                    std::printf ("  ok   mode=%d coupling=%.1f freeze=%d  settledPeak=%.3e peak=%.3f\n",
                                 (int) mode, coup, freeze, g.settledPeak, g.peak);
                }
            }

    std::printf ("\n%d cases, worst tail peak %.3f, %d flagged\n", cases, worstPeak, bad);
    std::printf ("%s\n", bad == 0 ? "OK - nothing exploded" : "FAIL - see flagged cases above");
    return bad == 0 ? 0 : 1;
}
