// Hammers the Peak Trem & Pan processor across every parameter combination and
// a batch of adverse inputs, watching for a non-finite or runaway output - the
// "exploding noise" class of bug.
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    void setParam (juce::AudioProcessorValueTreeState& state, const char* id, float value01)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value01));
    }

    struct Result { float peak = 0.0f; bool nonFinite = false; };

    Result runCase (const char* label, double sampleRate, int blockSize,
                    float amount, float rate, float shape, float bias,
                    bool panning, bool sync, bool on,
                    int inputKind, int blocks)
    {
        PeakTremPanProcessor proc;
        proc.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        setParam (proc.apvts, "amount", amount);
        setParam (proc.apvts, "rate", rate);
        setParam (proc.apvts, "shape", shape);
        setParam (proc.apvts, "bias", bias);
        setParam (proc.apvts, "mode", panning ? 1.0f : 0.0f);
        setParam (proc.apvts, "sync", sync ? 1.0f : 0.0f);
        setParam (proc.apvts, "on", on ? 1.0f : 0.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;

        Result r;
        double phase = 0.0;
        const double inc = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                float s = 0.0f;
                switch (inputKind)
                {
                    case 0: s = 0.9f * (float) std::sin (phase); break;            // loud sine
                    case 1: s = (i == 0 && b == 0) ? 1.0f : 0.0f; break;           // single impulse
                    case 2: s = 1.0f; break;                                       // full-scale DC
                    case 3: s = (b % 7 == 0) ? 4.0f : 0.2f * (float) std::sin (phase); break; // occasional hot spike
                    case 4: s = 0.0f; break;                                       // silence
                    case 5:
                        // One block of non-finite garbage early on, then a clean
                        // sine. The processor must recover, not roar forever.
                        if (b == 1)
                            s = (i % 2 == 0) ? std::numeric_limits<float>::quiet_NaN()
                                             : std::numeric_limits<float>::infinity();
                        else
                            s = 0.6f * (float) std::sin (phase);
                        break;
                }
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
                phase += inc;
            }

            // Flip the big switches mid-run: mode + sync toggles were the source
            // of the last roar.
            if (b == blocks / 3)      setParam (proc.apvts, "mode", panning ? 0.0f : 1.0f);
            if (b == blocks / 2)      setParam (proc.apvts, "sync", sync ? 0.0f : 1.0f);
            if (b == (2 * blocks) / 3) setParam (proc.apvts, "bias", 1.0f - bias);

            proc.processBlock (buf, midi);

            // Only judge the tail: a single bad input block is allowed to glitch
            // that block, but the processor must have fully recovered by the end.
            const bool tail = b >= (3 * blocks) / 4;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (tail)
                    {
                        if (! std::isfinite (v)) r.nonFinite = true;
                        r.peak = juce::jmax (r.peak, std::abs (v));
                    }
                }
        }

        if (r.nonFinite || r.peak > 8.0f)
            std::printf ("  !!! %-28s peak=%.3f nonFinite=%d  (amt=%.2f rate=%.2f shape=%.2f bias=%.2f pan=%d sync=%d on=%d in=%d sr=%.0f blk=%d)\n",
                         label, r.peak, (int) r.nonFinite, amount, rate, shape, bias,
                         (int) panning, (int) sync, (int) on, inputKind, sampleRate, blockSize);
        return r;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("=== Peak Trem & Pan stress ===\n");

    int cases = 0, bad = 0;
    float worstPeak = 0.0f;

    // Degenerate prepare: a host probing with prepareToPlay(0, 0) must not wedge
    // the audio thread (the old free-run wrap spun forever on the resulting inf
    // phase increment). If this returns at all, there is no hang.
    {
        PeakTremPanProcessor proc;
        proc.setPlayConfigDetails (2, 2, 0.0, 0);
        proc.prepareToPlay (0.0, 0);
        setParam (proc.apvts, "bias", 1.0f);
        setParam (proc.apvts, "amount", 1.0f);
        juce::AudioBuffer<float> buf (2, 128);
        juce::MidiBuffer midi;
        bool ok = true;
        for (int b = 0; b < 8; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    buf.setSample (ch, i, 0.5f);
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    ok &= std::isfinite (buf.getSample (ch, i));
        }
        std::printf ("degenerate prepare(0,0): %s\n", ok ? "returned, output finite" : "returned, OUTPUT NON-FINITE");
        if (! ok) ++bad;
    }

    const double rates[] = { 48000.0, 96000.0 };
    const int    blocks[] = { 32, 256 };
    const float  levels[] = { 0.0f, 0.5f, 1.0f };

    for (double sr : rates)
      for (int blk : blocks)
        for (float amount : levels)
          for (float rate : levels)
            for (float shape : levels)
              for (float bias : levels)
                for (int pan = 0; pan < 2; ++pan)
                  for (int sync = 0; sync < 2; ++sync)
                    for (int in = 0; in < 6; ++in)
                    {
                        const auto r = runCase ("case", sr, blk, amount, rate, shape, bias,
                                                pan != 0, sync != 0, true, in, 24);
                        ++cases;
                        worstPeak = juce::jmax (worstPeak, r.peak);
                        if (r.nonFinite || r.peak > 8.0f) ++bad;
                    }

    std::printf ("\n%d cases, worst peak %.3f, %d flagged\n", cases, worstPeak, bad);
    std::printf ("%s\n", bad == 0 ? "OK - nothing exploded" : "FAIL - see flagged cases above");
    return bad == 0 ? 0 : 1;
}
