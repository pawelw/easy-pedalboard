// Checks ee/dsp/Tuner.h the way a player would: feed the capture what a string
// sounds like, run the analyser on the editor's clock, and read the needle.
//
//   1. Accuracy. Every semitone from a five-string bass's low B (B0) to the
//      top of a guitar neck (E6), each at five offsets, three waveforms, three
//      host rates: the right note name, and the cents within +/-1.
//   2. No octave errors over a plucked note's whole life - loud attack to the
//      gate - on a low E whose fundamental is weaker than its second harmonic,
//      which is what a real low string gives a pickup.
//   3. A stiff string (partials stretched sharp, as every real one is) reads
//      among its partials the way other tuners do; the fundamental-only mode
//      still reads the fundamental.
//   4. Silence and noise read as no signal; the reading holds, then clears,
//      after a note stops.
//   5. A note change settles within a few frames.
//   6. An inactive capture writes nothing, so a plugin that carries one renders
//      exactly as it did without.
//
// Exits non-zero on any failure. Deterministic: no random numbers except a
// fixed-seed noise generator.
#include "ee/dsp/Tuner.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace
{
using namespace ee::dsp;

int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::printf ("  FAIL %s\n", what);
        ++failures;
    }
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kFrame = 1.0 / 45.0; // the editor's timer
constexpr int kBlock = 256;

enum class Wave
{
    sine,
    saw,
    string // weak fundamental, strong 2nd and 3rd - a low string on a pickup
};

float sampleOf (Wave wave, double phase) // phase in cycles
{
    const double p = 2.0 * kPi * phase;
    switch (wave)
    {
    case Wave::sine:
        return static_cast<float> (std::sin (p));
    case Wave::saw:
    {
        double s = 0.0;
        for (int h = 1; h <= 12; ++h)
            s += std::sin (h * p) / h;
        return static_cast<float> (0.55 * s);
    }
    default:
    {
        static const double amp[8] = { 0.35, 1.0, 0.7, 0.45, 0.3, 0.2, 0.12, 0.08 };
        double s = 0.0;
        for (int h = 1; h <= 8; ++h)
            s += amp[h - 1] * std::sin (h * p + 0.3 * h);
        return static_cast<float> (0.3 * s);
    }
    }
}

/** Drives a capture + analyser with `signal(t)` for `seconds`, calling `onFrame`
    with each reading on the editor's clock. */
void run (double sr, double seconds, const std::function<float (double)>& signal,
          const std::function<void (double, const TunerReading&)>& onFrame)
{
    TunerCapture capture;
    capture.prepare (sr);
    capture.setActive (true);
    TunerAnalyser analyser;
    analyser.reset();

    std::vector<float> block (kBlock);
    const auto total = static_cast<int64_t> (seconds * sr);
    double nextFrame = kFrame;

    for (int64_t n = 0; n < total; n += kBlock)
    {
        const int len = static_cast<int> (std::min<int64_t> (kBlock, total - n));
        for (int i = 0; i < len; ++i)
            block[static_cast<size_t> (i)] = signal (static_cast<double> (n + i) / sr);
        capture.process (block.data(), nullptr, len);

        const double t = static_cast<double> (n + len) / sr;
        while (t >= nextFrame)
        {
            onFrame (t, analyser.update (capture, kFrame));
            nextFrame += kFrame;
        }
    }
}

double hzFor (int midi, double cents) { return 440.0 * std::pow (2.0, (midi - 69 + cents / 100.0) / 12.0); }

void accuracy()
{
    std::printf ("accuracy: B0..E6, 5 offsets, 3 waves, 3 rates\n");

    const double rates[3] = { 44100.0, 48000.0, 96000.0 };
    const double offsets[5] = { -37.0, -12.0, 0.0, 8.0, 23.0 };
    const Wave waves[3] = { Wave::sine, Wave::saw, Wave::string };
    const char* waveNames[3] = { "sine", "saw", "string" };

    float worst = 0.0f;
    int cases = 0;

    for (double sr : rates)
        for (int w = 0; w < 3; ++w)
            for (int midi = 23; midi <= 88; ++midi)
                for (double off : offsets)
                {
                    const double hz = hzFor (midi, off);
                    // The saw's 12th harmonic of a high note is past the low-pass
                    // anyway; nothing to skip. Just the level.
                    TunerReading last;
                    run (sr, 0.6, [&] (double t) { return 0.3f * sampleOf (waves[w], hz * t); },
                         [&] (double, const TunerReading& r) { last = r; });

                    ++cases;
                    const float err = static_cast<float> (last.cents - off);
                    worst = std::max (worst, std::abs (err));

                    if (! last.signal || last.midiNote != midi || std::abs (err) > 1.0f)
                    {
                        std::printf ("  FAIL %s %.0f Hz sr: midi %d %+.0f ct -> %s midi %d %+.2f ct\n", waveNames[w], sr,
                                     midi, off, last.signal ? "signal" : "none", last.midiNote, last.cents);
                        ++failures;
                    }
                }

    std::printf ("  %d cases, worst error %.3f ct\n", cases, worst);
}

void pluck()
{
    std::printf ("pluck: low E (weak fundamental), attack to the gate, every frame\n");

    for (int midi : { 28, 40, 45, 52 }) // bass E, guitar low E, A, and the E an octave up
    {
        const double hz = hzFor (midi, 0.0);
        int frames = 0, wrong = 0;
        run (48000.0, 6.0,
             [&] (double t) { return static_cast<float> (0.8 * std::exp (-t / 1.2)) * sampleOf (Wave::string, hz * t); },
             [&] (double, const TunerReading& r)
             {
                 if (r.signal && ! r.holding)
                 {
                     ++frames;
                     if (r.midiNote != midi || std::abs (r.cents) > 2.0f)
                         ++wrong;
                 }
             });

        std::printf ("  midi %d: %d frames with signal, %d wrong\n", midi, frames, wrong);
        check (frames > 100, "pluck held a reading for most of its life");
        check (wrong == 0, "pluck read the right note throughout");
    }
}

/** Real strings are stiff, so their partials run sharp of whole multiples:
    partial h sits at h f sqrt(1 + B h^2). The tuner reads the whole waveform's
    period, as Live's Tuner and hardware tuners do, which lands among the
    partials - sharp of the fundamental alone - and that is what a player's
    other tuners show (tuner::kFundamentalOnly, measured against Live's Tuner
    and a pedal tuner on a real low E: both read ~+2..+3.5 ct where the
    fundamental alone read -5). So: sharp of the fundamental, inside the spread
    of the partials. The fundamental-only mode is still checked, on the bare
    detector, so it stays a working switch. */
void stiffString()
{
    std::printf ("stiff string: partials stretched, B = 1e-4 and 5e-4\n");

    for (double b : { 1.0e-4, 5.0e-4 })
        for (int midi : { 28, 40, 45, 64 })
        {
            const double f = hzFor (midi, 0.0);
            const double fundamental = 1200.0 * std::log2 (std::sqrt (1.0 + b));
            const double topPartial = 1200.0 * std::log2 (std::sqrt (1.0 + b * 100.0));
            const auto signal = [&] (double t)
            {
                double s = 0.0;
                for (int h = 1; h <= 10; ++h)
                    s += (h == 1 ? 0.35 : 1.0 / h) * std::sin (2.0 * kPi * h * f * std::sqrt (1.0 + b * h * h) * t);
                return static_cast<float> (0.2 * s);
            };

            TunerReading last;
            run (48000.0, 0.8, signal, [&] (double, const TunerReading& r) { last = r; });

            // The bare detector in fundamental-only mode, on the same sound.
            TunerCapture capture;
            capture.prepare (48000.0);
            capture.setActive (true);
            std::vector<float> block (kBlock), window (tuner::kWindow), scratch, filtered;
            for (int n = 0; n < 48000; n += kBlock)
            {
                for (int i = 0; i < kBlock; ++i)
                    block[static_cast<size_t> (i)] = signal ((n + i) / 48000.0);
                capture.process (block.data(), nullptr, kBlock);
            }
            capture.copyLatest (window.data(), tuner::kWindow);
            const auto est = detectPitch (window.data(), tuner::kWindow, capture.sampleRate(), scratch, filtered, true);
            const double fundOnly = 1200.0 * std::log2 (est.hz / f);

            std::printf ("  B %.0e midi %d: %+.2f ct (fundamental %+.2f, top partial %+.2f; fundamental-only mode %+.2f)\n",
                         b, midi, last.cents, fundamental, topPartial, fundOnly);
            check (last.signal && last.midiNote == midi && last.cents > fundamental && last.cents < topPartial,
                   "a stiff string reads among its partials, sharp of the fundamental");
            check (std::abs (fundOnly - fundamental) < 0.75, "fundamental-only mode reads the fundamental");
        }
}

void silenceNoiseHold()
{
    std::printf ("silence, noise, hold\n");

    bool anySignal = false;
    run (48000.0, 1.0, [] (double) { return 0.0f; },
         [&] (double, const TunerReading& r) { anySignal |= r.signal; });
    check (! anySignal, "silence reads as no signal");

    uint32_t seed = 12345;
    int noisy = 0, frames = 0;
    run (48000.0, 2.0,
         [&] (double)
         {
             seed = seed * 1664525u + 1013904223u;
             return 0.2f * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f);
         },
         [&] (double, const TunerReading& r)
         {
             ++frames;
             noisy += r.signal ? 1 : 0;
         });
    std::printf ("  white noise: %d / %d frames claimed a note\n", noisy, frames);
    check (noisy < frames / 10, "white noise mostly reads as no signal");

    // A2 for a second, then nothing: holding for kHoldSeconds, then clear.
    double heldUntil = 0.0;
    bool clearedAfter = false;
    const double a2 = hzFor (45, 0.0);
    run (48000.0, 3.0, [&] (double t) { return t < 1.0 ? 0.3f * sampleOf (Wave::saw, a2 * t) : 0.0f; },
         [&] (double t, const TunerReading& r)
         {
             if (t > 1.0 && r.signal && r.holding && r.midiNote == 45)
                 heldUntil = t;
             if (t > 2.5)
                 clearedAfter = ! r.signal;
         });
    std::printf ("  held the last note until %.2f s (note stopped at 1.00 s)\n", heldUntil);
    check (heldUntil > 1.5 && heldUntil < 2.3, "the last note holds about a second");
    check (clearedAfter, "the reading clears after the hold");
}

void noteChange()
{
    std::printf ("note change: A2 -> E2\n");

    const double a2 = hzFor (45, 0.0), e2 = hzFor (40, 0.0);
    double namedAt = -1.0, settledAt = -1.0;
    run (48000.0, 2.0, [&] (double t) { return 0.3f * sampleOf (Wave::string, (t < 1.0 ? a2 : e2) * t); },
         [&] (double t, const TunerReading& r)
         {
             if (t > 1.0 && namedAt < 0.0 && r.signal && r.midiNote == 40)
                 namedAt = t;
             if (t > 1.0 && settledAt < 0.0 && r.signal && r.midiNote == 40 && std::abs (r.cents) < 1.0f)
                 settledAt = t;
         });
    // The name comes off the newest ~90 ms and has to hold three frames; the
    // cents are smoothed over ~0.2 s on purpose (steadiness under picking over
    // speed).
    std::printf ("  named E2 %.0f ms after the change, within 1 ct %.0f ms after\n", (namedAt - 1.0) * 1000.0,
                 (settledAt - 1.0) * 1000.0);
    check (namedAt > 0.0 && namedAt - 1.0 < 0.25, "a new note is named within 250 ms");
    check (settledAt > 0.0 && settledAt - 1.0 < 0.4, "a new note settles within 400 ms");
}

void inactive()
{
    std::printf ("inactive capture\n");

    TunerCapture capture;
    capture.prepare (48000.0);
    std::vector<float> block (kBlock, 0.5f);
    capture.process (block.data(), block.data(), kBlock);
    check (capture.written() == 0, "an inactive capture writes nothing");

    capture.setActive (true);
    capture.process (block.data(), block.data(), kBlock);
    check (capture.written() > 0, "an active capture writes");

    std::vector<float> window (tuner::kWindow);
    check (! capture.copyLatest (window.data(), tuner::kWindow), "no window until enough fresh audio has arrived");
}
} // namespace

int main()
{
    accuracy();
    pluck();
    stiffString();
    silenceNoiseHold();
    noteChange();
    inactive();

    std::printf (failures == 0 ? "OK\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
