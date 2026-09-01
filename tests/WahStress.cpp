// Hunts for a discontinuity in Peak Wah's output at a note onset.
//
// The pedal retriggers on every pluck: the transient detector snaps the LFO
// phase to 0 and, at low Decay, slams the one-shot gate open. Both move the
// cutoff, and the tank is a Wave Digital Filter whose capacitance is stepped
// every kControlBlock samples - so a fast sweep steps it hard. This drives
// silence-then-note through the engine over a spread of settings and reports
// the largest sample-to-sample jump it can provoke, relative to the signal
// either side of it.
//
// A click is a step: one sample that moves far more than its neighbours are
// moving. `worstRatio` below is exactly that - the biggest single-sample delta
// over the median delta around it.
#include "ee/dsp/AutoWah.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 64;

/** A plucked note: a decaying two-partial tone with a hard start, which is what
    the transient detector is built to catch. */
std::vector<float> pluck (int numSamples, int startSample, float amplitude, double freqHz)
{
    std::vector<float> out (static_cast<size_t> (numSamples), 0.0f);

    for (int i = startSample; i < numSamples; ++i)
    {
        const double t = static_cast<double> (i - startSample) / kSampleRate;
        const double env = std::exp (-t * 3.0);
        const double s = std::sin (2.0 * M_PI * freqHz * t)
                         + 0.4 * std::sin (2.0 * M_PI * freqHz * 2.0 * t);
        out[static_cast<size_t> (i)] = static_cast<float> (amplitude * env * s * 0.6);
    }

    return out;
}

struct Result
{
    float worstRatio = 0.0f;   // biggest single-sample step over the local median step
    int worstSample = 0;
    float worstLevelDb = 0.0f; // how far down on the loudest part of the note it sits
    float inputRatio = 0.0f;   // the same statistic for the dry input, as a control
    float peak = 0.0f;         // output peak - near 1/kGritDrive means the limiter is in
    bool nonFinite = false;
};

/** The largest single-sample delta measured against the median delta of the 256
    samples that FOLLOW it.

    A trailing window rather than a centred one on purpose: the note begins in
    silence, and a centred window at the onset is half silence, which makes the
    median meaningless and scores the note's own attack as a click. Looking only
    forward, every sample is compared against the signal actually running at the
    time - so a smooth waveform scores near 1 however loud, a real step scores
    high however quiet, and the note starting is not itself an event.
*/
float stepScore (const std::vector<float>& x, int from, int* worstAt, float* worstLevelDb,
                 bool* nonFinite)
{
    constexpr int kWindow = 256;

    // A step buried 30 dB under the note is not the click anyone hears - and
    // the deep end of a wah sweep is exactly such a place, quiet enough that
    // ordinary ripple scores high against its own neighbours. Only steps
    // within kFloorDb of the loudest part of the signal count.
    constexpr float kFloorDb = -30.0f;

    float peak = 0.0f;
    for (size_t i = static_cast<size_t> (std::max (from, 0)); i < x.size(); ++i)
        peak = std::max (peak, std::abs (x[i]));

    if (peak <= 0.0f)
        return 0.0f;

    const float floorLevel = peak * std::pow (10.0f, kFloorDb / 20.0f);

    std::vector<float> deltas (x.size(), 0.0f);
    for (size_t i = 1; i < x.size(); ++i)
    {
        if (nonFinite != nullptr && ! std::isfinite (x[i]))
            *nonFinite = true;
        deltas[i] = std::abs (x[i] - x[i - 1]);
    }

    float worst = 0.0f;

    for (int i = std::max (from, 1); i < static_cast<int> (x.size()) - kWindow; ++i)
    {
        std::vector<float> ahead (deltas.begin() + i + 1, deltas.begin() + i + 1 + kWindow);
        std::nth_element (ahead.begin(), ahead.begin() + kWindow / 2, ahead.end());
        const float median = ahead[static_cast<size_t> (kWindow / 2)];

        // Below this the signal is essentially silent and the ratio is noise.
        if (median < 1.0e-6f)
            continue;

        float local = 0.0f;
        for (int j = i; j < i + kWindow; ++j)
            local = std::max (local, std::abs (x[static_cast<size_t> (j)]));

        if (local < floorLevel)
            continue;

        const float ratio = deltas[static_cast<size_t> (i)] / median;
        if (ratio > worst)
        {
            worst = ratio;
            if (worstAt != nullptr)
                *worstAt = i;
            if (worstLevelDb != nullptr)
                *worstLevelDb = 20.0f * std::log10 (std::max (local, 1.0e-9f) / peak);
        }
    }

    return worst;
}

Result runOnce (float decay01, float range01, float freq01, float q01, float type01,
                float shape01, float periodSeconds, float amplitude)
{
    ee::dsp::AutoWah wah;
    wah.prepare (kSampleRate);
    wah.setDecay01 (decay01);
    wah.setRange01 (range01);
    wah.setFreq01 (freq01);
    wah.setQ01 (q01);
    wah.setTypeMorph01 (type01);
    wah.setShape01 (shape01);
    wah.setMix01 (1.0f);          // wet only: the dry path would mask a step
    wah.setPeriodSeconds (periodSeconds);
    wah.setStereo (false);

    // Half a second of silence, then the note - the pedal sees exactly what it
    // sees when you stop and start playing.
    const int total = static_cast<int> (kSampleRate * 1.5);
    const int onset = static_cast<int> (kSampleRate * 0.5);
    const auto dry = pluck (total, onset, amplitude, 196.0);
    auto buffer = dry;

    for (int i = 0; i < total; i += kBlock)
    {
        const int n = std::min (kBlock, total - i);
        wah.process (buffer.data() + i, nullptr, n);
    }

    Result r;
    r.worstRatio = stepScore (buffer, onset, &r.worstSample, &r.worstLevelDb, &r.nonFinite);
    r.inputRatio = stepScore (dry, onset, nullptr, nullptr, nullptr);
    for (float v : buffer)
        r.peak = std::max (r.peak, std::abs (v));
    return r;
}

} // namespace

int main()
{
    // A click is inaudible under about 8x the local slope and unmistakable
    // above about 20x; the threshold sits between them.
    constexpr float kFailRatio = 12.0f;

    // Above this the output stage's tanh peak catcher is squaring the waveform
    // off, and its saturation edges score on the step metric exactly the way a
    // click does - the two are not distinguishable from the output alone. Those
    // cases still print, so the limiting stays visible, but they are not
    // counted: the limiter working is not the bug this hunts.
    constexpr float kLimiterPeak = 1.05f;

    struct Case
    {
        const char* name;
        float decay01, range01, freq01, q01, type01, shape01, period, amplitude;
    };

    const Case cases[] = {
        { "one-shot, default voicing", 0.03f, 0.61f, 0.487f, 0.58f, 0.50f, 0.60f, 0.40f, 0.5f },
        { "one-shot, full range",      0.03f, 1.00f, 0.487f, 0.58f, 0.50f, 0.60f, 0.40f, 0.5f },
        { "one-shot, high Q",          0.03f, 1.00f, 0.300f, 1.00f, 0.50f, 0.60f, 0.40f, 0.5f },
        { "one-shot, low-pass tap",    0.03f, 1.00f, 0.487f, 0.80f, 0.00f, 0.60f, 0.40f, 0.5f },
        { "one-shot, high-pass tap",   0.03f, 1.00f, 0.487f, 0.80f, 1.00f, 0.60f, 0.40f, 0.5f },
        { "one-shot, square LFO",      0.03f, 1.00f, 0.487f, 0.58f, 0.50f, 1.00f, 0.40f, 0.5f },
        { "one-shot, fast LFO",        0.03f, 1.00f, 0.487f, 0.58f, 0.50f, 0.60f, 0.08f, 0.5f },
        { "follower, mid decay",       0.50f, 0.80f, 0.487f, 0.70f, 0.50f, 0.60f, 0.40f, 0.5f },
        { "latched, decay full up",    1.00f, 0.80f, 0.487f, 0.70f, 0.50f, 0.60f, 0.40f, 0.5f },
        { "quiet note",                0.03f, 1.00f, 0.487f, 0.58f, 0.50f, 0.60f, 0.40f, 0.08f },
        { "hard hit",                  0.03f, 1.00f, 0.487f, 0.90f, 0.50f, 0.60f, 0.40f, 1.0f },
    };

    int failures = 0;

    for (const auto& c : cases)
    {
        const auto r = runOnce (c.decay01, c.range01, c.freq01, c.q01, c.type01,
                                c.shape01, c.period, c.amplitude);

        const bool limiting = r.peak > kLimiterPeak;
        const bool bad = r.nonFinite || (r.worstRatio > kFailRatio && ! limiting);
        if (bad)
            ++failures;

        std::printf ("  %-28s out %6.1fx  (dry %4.1fx)  at sample %6d, %5.1f dB, peak %5.2f%s%s\n",
                     c.name, r.worstRatio, r.inputRatio, r.worstSample, r.worstLevelDb, r.peak,
                     r.nonFinite ? "  NON-FINITE" : (limiting ? "  limiter in" : ""),
                     bad ? "   FAIL" : "");
    }

    std::printf ("%s: %d of %d onset cases stepped\n"
                 "  (a \"limiter in\" case is saturating, not clicking - see kLimiterPeak)\n",
                 failures == 0 ? "PASS" : "FAIL",
                 failures, static_cast<int> (std::size (cases)));

    return failures == 0 ? 0 : 1;
}
