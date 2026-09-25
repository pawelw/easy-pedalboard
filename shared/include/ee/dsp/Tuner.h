#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ee::dsp
{

/** A chromatic tuner in three pieces, split along the thread boundary.

    - `TunerCapture` runs on the audio thread. It low-passes the input, keeps
      every Nth sample (to ~11 kHz) and writes the result into a lock-free ring.
      It does nothing at all while inactive, so a plugin carrying one renders
      bit-identically whether or not its tuner exists.
    - `detectPitch` is YIN (de Cheveigne & Kawahara, 2002) on one window of that
      ring. Pure function, no state.
    - `TunerAnalyser` runs on the message thread, on the editor's timer. It pulls
      the latest window, detects, and turns a stream of raw estimates into
      something a needle can follow: each frame weighted by how periodic it
      is and how long since the last pick, estimates far from the needle
      ignored until several agree, a name that has to hold a few frames before
      it changes, and a short hold after the string stops.

    Standard pitch only: A4 = 440 Hz, twelve-tone equal temperament, sharps.

    Why decimate: guitar and bass fundamentals sit between ~30 Hz (a five-string
    bass's low B) and ~1.3 kHz, and YIN's cost is window x longest period. At
    the host rate that is ~10x more work for nothing - the top harmonics YIN
    would see are the ones most likely to pull it an octave up. At ~11 kHz the
    whole analysis is ~0.25 ms per frame on the editor's timer.
*/
namespace tuner
{
    constexpr double kTargetRate = 11025.0; // what the decimated stream runs at, roughly
    constexpr double kLowpassHz  = 1800.0;  // before decimation; also tames the upper harmonics
    constexpr float  kMinHz      = 30.0f;   // five-string bass low B is 30.87 Hz
    constexpr float  kMaxHz      = 1400.0f; // top of a guitar neck, with room
    constexpr int    kWindow     = 2048;    // samples at the decimated rate: ~185 ms, for the cents
    constexpr int    kCoarse     = 1024;    // the newest ~90 ms of it, for the note
    constexpr int    kRingSize   = 8192;    // ~0.7 s - many windows ahead of any reader
    constexpr float  kThreshold  = 0.15f;   // YIN's absolute threshold on the normalised difference
    constexpr float  kMaxAperiodicity = 0.35f; // above this the best dip is noise, not a note
    constexpr float  kGateDb     = -55.0f;  // window rms below this is "no signal"
    constexpr double kHoldSeconds  = 1.0;   // last reading stays up this long after the note dies
    constexpr double kSmoothSeconds = 0.2;  // the cents needle's time constant, at full confidence
    constexpr float  kOutlierCents = 15.0f; // further than this from the needle is suspect...
    constexpr int    kOutlierFrames = 4;    // ...until this many frames in a row agree on it (a retune)
    constexpr float  kAgreeCents = 8.0f;    // what "agree" means for those frames
    constexpr float  kHysteresis = 0.15f;   // semitones past the half-way line before the name flips
    constexpr int    kNameFrames = 3;       // frames the new name has to hold before it shows

    // What the cents are measured on: the fundamental alone, or the whole
    // waveform's period. They differ on a real string because its partials
    // run sharp - see detectPitch.
    constexpr bool   kFundamentalOnly = false;

    // Picking. A pick is a phase break and a pitch transient - the string
    // starts sharp and settles over tens of ms - so the audio right after one
    // says little about the note's pitch, however loud it is.
    constexpr double kOnsetBlockSeconds = 0.003; // energy measured in blocks this long
    constexpr float  kOnsetRatio = 3.0f;         // a block this many times the ones before it is a pick
    constexpr double kAttackSeconds = 0.03;      // skipped after a pick, for the cents
    constexpr double kTrustSeconds  = 0.12;      // after a pick, trust ramps up to full over this long
} // namespace tuner

// ---------------------------------------------------------------- capture

class TunerCapture
{
public:
    /** Audio setup thread. Picks the decimation factor and designs the filter. */
    void prepare (double sampleRate) noexcept
    {
        factor = std::max (1, static_cast<int> (std::lround (sampleRate / tuner::kTargetRate)));
        rate.store (sampleRate / factor, std::memory_order_relaxed);

        // Two cascaded RBJ low-passes at the Butterworth Qs: a 4th-order response.
        const double qs[2] = { 0.54119610, 1.30656296 };
        const double w0 = 2.0 * 3.14159265358979323846 * std::min (tuner::kLowpassHz, 0.45 * sampleRate / factor)
                        / sampleRate;
        for (int s = 0; s < 2; ++s)
        {
            const double alpha = std::sin (w0) / (2.0 * qs[s]);
            const double cosw  = std::cos (w0);
            const double a0    = 1.0 + alpha;
            auto& b = stage[static_cast<size_t> (s)];
            b.b0 = static_cast<float> ((1.0 - cosw) * 0.5 / a0);
            b.b1 = static_cast<float> ((1.0 - cosw) / a0);
            b.b2 = b.b0;
            b.a1 = static_cast<float> (-2.0 * cosw / a0);
            b.a2 = static_cast<float> ((1.0 - alpha) / a0);
            b.z1 = b.z2 = 0.0f;
        }
        phase = 0;
    }

    /** Message thread. Opening restarts the "enough fresh audio yet" count, so
        a reopened tuner never shows the note that was playing last time. */
    void setActive (bool shouldBeActive) noexcept
    {
        if (shouldBeActive && ! active.load (std::memory_order_relaxed))
            activatedAt.store (writePos.load (std::memory_order_acquire), std::memory_order_relaxed);
        active.store (shouldBeActive, std::memory_order_release);
    }

    bool isActive() const noexcept { return active.load (std::memory_order_acquire); }

    /** Audio thread. `right` may be null. Returns at once while inactive. */
    void process (const float* left, const float* right, int numSamples) noexcept
    {
        if (! active.load (std::memory_order_relaxed))
            return;

        uint32_t pos = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            float x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            for (auto& b : stage)
                x = b.process (x);

            if (++phase >= factor)
            {
                phase = 0;
                ring[pos % tuner::kRingSize].store (std::isfinite (x) ? x : 0.0f, std::memory_order_relaxed);
                ++pos;
            }
        }
        writePos.store (pos, std::memory_order_release);
    }

    /** Message thread. Copies the newest `n` decimated samples, oldest first.
        False until that many have arrived since the tuner was opened. */
    bool copyLatest (float* dst, int n) const noexcept
    {
        const uint32_t end = writePos.load (std::memory_order_acquire);
        if (n > tuner::kRingSize / 2 || end - activatedAt.load (std::memory_order_relaxed) < static_cast<uint32_t> (n))
            return false;

        const uint32_t start = end - static_cast<uint32_t> (n);
        for (int i = 0; i < n; ++i)
            dst[i] = ring[(start + static_cast<uint32_t> (i)) % tuner::kRingSize].load (std::memory_order_relaxed);
        return true;
    }

    /** The decimated stream's sample rate. */
    double sampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

    /** Total decimated samples written, for a reader that wants to know whether
        anything new has arrived. */
    uint32_t written() const noexcept { return writePos.load (std::memory_order_acquire); }

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    std::array<Biquad, 2> stage {};
    int factor = 4;
    int phase = 0;

    std::array<std::atomic<float>, tuner::kRingSize> ring {};
    std::atomic<uint32_t> writePos { 0 };
    std::atomic<uint32_t> activatedAt { 0 };
    std::atomic<bool> active { false };
    std::atomic<double> rate { 11025.0 };
};

// ---------------------------------------------------------------- detection

struct PitchEstimate
{
    float hz = 0.0f;           // 0 when nothing periodic was found
    float aperiodicity = 1.0f; // YIN's normalised difference at the chosen lag: 0 is a perfect period
    float rms = 0.0f;
    double sinceOnset = 1.0e9; // seconds from the last pick in the window to its end; huge if none
    bool refined = false;      // true when the cents came off the fundamental-only second pass
    float wholeHz = 0.0f;      // the first pass's figure: the whole waveform's period, all partials
};

/** YIN on the newest `tuner::kCoarse` of `n` samples at `rate` for the note,
    then a finer look at all `n` for the cents - see the second half. `scratch`
    and `filtered` are resized as needed, so a caller on a timer keeps them and
    never allocates after the first call. */
inline PitchEstimate detectPitch (const float* window, int n, double rate, std::vector<float>& scratch,
                                  std::vector<float>& filtered, bool fundamentalOnly = tuner::kFundamentalOnly,
                                  float minHz = tuner::kMinHz, float maxHz = tuner::kMaxHz)
{
    PitchEstimate out;

    // The note comes off the newest part only, so the name follows the
    // playing without waiting for the whole window to turn over.
    const int coarse = std::min (n, tuner::kCoarse);
    const float* x = window + (n - coarse);

    double sumSq = 0.0;
    for (int i = 0; i < coarse; ++i)
        sumSq += static_cast<double> (x[i]) * x[i];
    out.rms = static_cast<float> (std::sqrt (sumSq / std::max (1, coarse)));

    const int tauMin = std::max (2, static_cast<int> (std::floor (rate / maxHz)));
    const int tauMax = static_cast<int> (std::ceil (rate / minHz));
    const int w = coarse - tauMax - 1;
    if (w < tauMax || out.rms <= 0.0f)
        return out;

    // d(tau), then the cumulative-mean-normalised d'(tau). Stored side by side:
    // the parabola below wants the raw one, the threshold the normalised one.
    scratch.assign (static_cast<size_t> (2 * (tauMax + 2)), 0.0f);
    float* d   = scratch.data();
    float* cmn = d + tauMax + 2;

    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        double acc = 0.0;
        for (int j = 0; j < w; ++j)
        {
            const double diff = static_cast<double> (x[j]) - x[j + tau];
            acc += diff * diff;
        }
        d[tau] = static_cast<float> (acc);
    }

    cmn[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        running += d[tau];
        cmn[tau] = running > 0.0 ? static_cast<float> (d[tau] * tau / running) : 1.0f;
    }

    // The first dip under the threshold, walked down to its floor - the
    // shortest lag that is a period, which is what keeps YIN off the
    // subharmonics. Failing that, the deepest dip anywhere in range.
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cmn[tau] < tuner::kThreshold)
        {
            while (tau + 1 <= tauMax && cmn[tau + 1] < cmn[tau])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin + 1; tau <= tauMax; ++tau)
            if (cmn[tau] < cmn[best])
                best = tau;
    }

    out.aperiodicity = cmn[best];

    // Parabola through the raw difference at the three lags round the dip: the
    // sub-sample lag. d is close to quadratic at its minimum for a band-
    // limited signal, which is why this and not d'.
    const auto parabola = [] (double a, double b, double c)
    {
        const double denom = a - 2.0 * b + c;
        return denom > 0.0 ? std::clamp (0.5 * (a - c) / denom, -1.0, 1.0) : 0.0;
    };

    double period = best;
    if (best > 1 && best < tauMax + 1)
        period += parabola (d[best - 1], d[best], d[best + 1]);

    out.hz = static_cast<float> (rate / period);
    out.wholeHz = out.hz;

    // The last pick in the window: a short block well above the few before it.
    // At least a period long, or a low note's own waveform - near zero for a
    // few ms, then at its peak - reads as a pick every cycle.
    int onset = -1;
    {
        const int block = std::max ({ 8, static_cast<int> (tuner::kOnsetBlockSeconds * rate),
                                      static_cast<int> (std::ceil (period)) });
        const int blocks = n / block;
        const float floorEnergy = std::pow (10.0f, tuner::kGateDb / 10.0f) * block;
        std::vector<float>& e = filtered; // borrowed; the second pass below refills it
        e.assign (static_cast<size_t> (blocks), 0.0f);
        for (int b = 0; b < blocks; ++b)
        {
            double acc = 0.0;
            for (int i = b * block; i < (b + 1) * block; ++i)
                acc += static_cast<double> (window[i]) * window[i];
            e[static_cast<size_t> (b)] = static_cast<float> (acc);
        }
        for (int b = blocks - 1; b >= 4 && onset < 0; --b)
        {
            const float before = 0.25f * (e[size_t (b - 1)] + e[size_t (b - 2)] + e[size_t (b - 3)] + e[size_t (b - 4)]);
            if (e[size_t (b)] > floorEnergy && e[size_t (b)] > tuner::kOnsetRatio * before)
                onset = b * block;
        }
        if (onset >= 0)
            out.sinceOnset = (n - onset) / rate;
    }

    // That is the note. The cents are measured again on the whole `n` rather
    // than the last `coarse`, at a longer lag: at ~11 kHz the top E's period
    // is eight samples, and a tenth of a sample is 20 ct - so the period is
    // measured again at the dip m periods along, where the same sub-sample
    // error is m times smaller. m doubles each time, so the dip being looked
    // for is always within a sample of where the last estimate puts it.
    //
    // `fundamentalOnly` first low-passes the window just above the estimate
    // (8th order, the second partial ~25 dB down). A real string's partials
    // run sharp of whole multiples, and d weights each by its harmonic number
    // squared, so the whole waveform reads sharp of the fundamental - a wound
    // low E ~3 ct at the ordinary B ~ 1e-4. Off by default
    // (tuner::kFundamentalOnly): the whole waveform is what Live's Tuner and a
    // pedal tuner both read, measured on a real low E, and a tuner that
    // disagrees with the player's others by 5-8 ct reads as wrong however
    // principled its reason.
    //
    // All of it only on what came after the last pick (and its attack): across a
    // pick the waveform breaks phase, and the dip it leaves is somewhere else.
    const int from = onset >= 0 ? std::min (n, onset + static_cast<int> (tuner::kAttackSeconds * rate)) : 0;
    const double fc = std::min (1.4 * rate / period, 0.4 * rate);
    const int settle = fundamentalOnly ? static_cast<int> (std::ceil (4.0 * rate / fc)) : 0;
    const int len = n - from - settle;
    if (len < 2 * static_cast<int> (std::ceil (period)) + 4)
        return out;

    filtered.resize (static_cast<size_t> (n - from));
    {
        static constexpr double qs[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        for (int i = from; i < n; ++i)
            filtered[static_cast<size_t> (i - from)] = window[i];
        const double w0 = 2.0 * 3.14159265358979323846 * fc / rate;
        const double cosw = std::cos (w0);
        for (int k = 0; k < (fundamentalOnly ? 4 : 0); ++k)
        {
            const double alpha = std::sin (w0) / (2.0 * qs[k]);
            const double a0 = 1.0 + alpha;
            const double b0 = (1.0 - cosw) * 0.5 / a0, b1 = (1.0 - cosw) / a0, b2 = b0;
            const double a1 = -2.0 * cosw / a0, a2 = (1.0 - alpha) / a0;
            double z1 = 0.0, z2 = 0.0;
            for (auto& v : filtered)
            {
                const double y = b0 * v + z1;
                z1 = b1 * v - a1 * y + z2;
                z2 = b2 * v - a2 * y;
                v = static_cast<float> (y);
            }
        }
    }

    const float* y = filtered.data() + settle;
    const auto dAt = [&] (int lag, int span)
    {
        double acc = 0.0;
        for (int j = 0; j < span; ++j)
        {
            const double diff = static_cast<double> (y[j]) - y[j + lag];
            acc += diff * diff;
        }
        return acc;
    };

    const auto remeasure = [&] (int m)
    {
        int at = static_cast<int> (std::lround (m * period));
        const int span = len - at - 2;
        if (at < 2 || span < at)
            return;

        double a = dAt (at - 1, span), b = dAt (at, span), c = dAt (at + 1, span);
        // One step toward the lower neighbour, if the dip is not where the last
        // estimate put it.
        if (a < b)
        {
            --at;
            c = b;
            b = a;
            a = dAt (at - 1, span);
        }
        else if (c < b)
        {
            ++at;
            a = b;
            b = c;
            c = dAt (at + 1, span);
        }
        period = (at + parabola (a, b, c)) / m;
    };

    remeasure (1);
    for (int m = 2; 2 * m * period + 4 <= len; m *= 2)
        remeasure (m);
    remeasure (static_cast<int> ((len - 4) / (2.0 * period)));
    out.refined = true;

    out.hz = static_cast<float> (rate / period);
    return out;
}

// ---------------------------------------------------------------- the needle

struct TunerReading
{
    bool signal = false; // false: nothing to show, draw the idle meter
    bool holding = false; // true: the note has stopped, this is the last one, fading
    int midiNote = 0;     // 40 is the low E string
    float cents = 0.0f;   // -50..+50 from that note
    float hz = 0.0f;

    static const char* noteName (int midi) noexcept
    {
        static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midi % 12) + 12) % 12];
    }

    static int octave (int midi) noexcept { return midi / 12 - 1; }
};

class TunerAnalyser
{
public:
    /** Forget everything - call when the tuner opens. */
    void reset() noexcept
    {
        reading = {};
        haveNote = false;
        candidate = -1;
        candidateFrames = 0;
        sinceSignal = 1.0e9;
        outlierFrames = 0;
        driftFrames = 0;
    }

    /** One frame, every `dt` seconds. */
    TunerReading update (const TunerCapture& capture, double dt)
    {
        window.resize (tuner::kWindow);
        if (! capture.copyLatest (window.data(), tuner::kWindow))
            return lose (dt);

        const PitchEstimate est = detectPitch (window.data(), tuner::kWindow, capture.sampleRate(), scratch, filtered);
        return feed (est, dt);
    }

    /** The same, from an estimate already made - what the offline test drives. */
    TunerReading feed (const PitchEstimate& est, double dt)
    {
        const float gate = std::pow (10.0f, tuner::kGateDb / 20.0f);
        if (est.hz <= 0.0f || est.rms < gate || est.aperiodicity > tuner::kMaxAperiodicity)
            return lose (dt);

        sinceSignal = 0.0;

        // How much this frame is worth. A clean period is worth more than a
        // rough one, and right after a pick is worth nothing - the string is
        // still sharp from the attack and the window straddles a phase break.
        // Fast picking therefore moves the needle only on the settled middle
        // of each note, and never at the picks, which is what keeps it still.
        // (Up to 0.05 counts as clean: YIN's normalised dip reads ~0.1 on a
        // perfectly periodic high note, whose lag is only a few samples.)
        float trust = std::clamp (1.0f - (est.aperiodicity - 0.05f) / 0.2f, 0.0f, 1.0f);
        if (est.sinceOnset < tuner::kAttackSeconds)
            trust = 0.0f;
        else if (est.sinceOnset < tuner::kTrustSeconds)
            trust *= static_cast<float> ((est.sinceOnset - tuner::kAttackSeconds)
                                         / (tuner::kTrustSeconds - tuner::kAttackSeconds));
        if (! est.refined)
            trust *= 0.5f;
        const bool usable = trust > 0.25f;

        const float midi = 69.0f + 12.0f * std::log2 (est.hz / 440.0f);
        const int nearest = static_cast<int> (std::lround (midi));

        // The first reading waits for a frame worth showing, rather than
        // opening on the attack.
        if (! haveNote)
        {
            if (! usable)
                return reading;
            reading.midiNote = nearest;
            haveNote = true;
            smoothed = (midi - static_cast<float> (nearest)) * 100.0f;
        }
        // The name flips only once the pitch is well past the line between two
        // notes, and has stayed there for a few trustworthy frames - otherwise
        // a string that sits a quarter-tone out flickers between names.
        else if (std::abs (midi - static_cast<float> (reading.midiNote)) > 0.5f + tuner::kHysteresis)
        {
            if (usable)
            {
                candidateFrames = nearest == candidate ? candidateFrames + 1 : 1;
                candidate = nearest;
            }

            if (candidateFrames >= tuner::kNameFrames)
            {
                reading.midiNote = nearest;
                smoothed = (midi - static_cast<float> (nearest)) * 100.0f;
                candidate = -1;
                candidateFrames = 0;
                outlierFrames = 0;
                driftFrames = 0;
            }
        }
        else if (usable)
        {
            candidate = -1;
            candidateFrames = 0;
        }

        const float target = (midi - static_cast<float> (reading.midiNote)) * 100.0f;

        // Far from the needle is a glitch until it keeps happening: only a run
        // of frames that agree with each other - a peg being turned, a bend
        // held - moves it there, and then in one step.
        // And a slow average of every usable frame, as a way out for a needle
        // that has settled somewhere wrong while the frames scatter too much to
        // agree with each other: if the average stays far from it, it goes.
        if (usable)
        {
            drift = driftFrames == 0 ? target : drift + 0.15f * (target - drift);
            driftFrames = std::abs (drift - smoothed) > tuner::kOutlierCents ? driftFrames + 1 : 1;
            if (driftFrames > 2 * tuner::kOutlierFrames)
            {
                smoothed = drift;
                driftFrames = 1;
                outlierFrames = 0;
            }
        }

        if (std::abs (target - smoothed) > tuner::kOutlierCents)
        {
            if (usable)
            {
                if (outlierFrames > 0 && std::abs (target - outlierMean) < tuner::kAgreeCents)
                {
                    outlierMean += (target - outlierMean) / static_cast<float> (outlierFrames + 1);
                    ++outlierFrames;
                }
                else
                {
                    outlierMean = target;
                    outlierFrames = 1;
                }

                if (outlierFrames >= tuner::kOutlierFrames)
                {
                    smoothed = outlierMean;
                    outlierFrames = 0;
                }
            }
        }
        else
        {
            outlierFrames = 0;
            smoothed += trust * static_cast<float> (1.0 - std::exp (-dt / tuner::kSmoothSeconds)) * (target - smoothed);
        }

        reading.signal = true;
        reading.holding = false;
        reading.cents = std::clamp (smoothed, -50.0f, 50.0f);
        reading.hz = static_cast<float> (440.0 * std::pow (2.0, (reading.midiNote - 69 + smoothed / 100.0) / 12.0));
        return reading;
    }

private:
    TunerReading lose (double dt)
    {
        sinceSignal += dt;
        candidate = -1;
        candidateFrames = 0;
        outlierFrames = 0;
        driftFrames = 0;

        if (haveNote && sinceSignal < tuner::kHoldSeconds)
        {
            reading.holding = true;
            return reading;
        }

        haveNote = false;
        reading = {};
        return reading;
    }

    TunerReading reading;
    bool haveNote = false;
    int candidate = -1;
    int candidateFrames = 0;
    float smoothed = 0.0f;
    double sinceSignal = 1.0e9;
    float outlierMean = 0.0f;
    int outlierFrames = 0;
    float drift = 0.0f;
    int driftFrames = 0;

    std::vector<float> window;
    std::vector<float> scratch;
    std::vector<float> filtered;
};

} // namespace ee::dsp
