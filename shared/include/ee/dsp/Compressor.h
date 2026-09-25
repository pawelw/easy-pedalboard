#pragma once

#include "ee/dsp/CompressorConfig.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** A Keeley-style pedal compressor - BitBit Artifact's Comp engine. The voicing
 *  and what each stage stands in for are in CompressorConfig.h.
 *
 *  Stereo-linked: one detector reads the louder channel and both channels get
 *  the same gain, so a compressed stereo image does not wander. Zero latency -
 *  no look-ahead, as on the pedal - which is also what lets the module's Mix be
 *  its Blend (parallel compression) without combing.
 *
 *  The chain, per sample:
 *
 *    x -> [sidechain high-pass, detector only] -> Sensitivity pre-gain
 *      -> |.| peak detector -> dB -> static curve
 *      -> attack / program-dependent release -> gain, plus the auto-level
 *      make-up -> OTA tanh
 *
 *  The make-up is computed from the curve for a typical guitar level, then
 *  trimmed by a slow measured match of output to input (see AUTO LEVEL in
 *  CompressorConfig.h), so the output sits at the input's level at any
 *  Sensitivity and any input gain - there is no Level control.
 *
 *  **The meter feed** is for the face's display and nothing else: every
 *  comp::kMeterPointMs the input peak, the output peak and the deepest gain
 *  reduction go into a ring, and `meterCount()` says how many have ever been
 *  written. An editor on the message thread remembers the last count it saw
 *  and reads the points since - the same arrangement as Grainer's grain
 *  events. The entries are relaxed atomics, so a reader that has fallen a whole
 *  ring behind gets stale points, never a torn one.
 */
class Compressor
{
public:
    struct MeterPoint
    {
        float inPeak = 0.0f;  // linear, the input's peak over the window
        float outPeak = 0.0f; // linear, the finished output
        float grDb = 0.0f;    // <= 0, the deepest reduction over the window
    };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? static_cast<float> (sampleRate) : 44100.0f;

        detRelease = coeffFor (comp::kDetectorReleaseMs);
        relFast = coeffFor (comp::kReleaseFastMs);
        slowAttack = coeffFor (comp::kSlowAttackMs);
        relSlow = coeffFor (comp::kReleaseSlowMs);
        attack = coeffFor (attackMs);
        smooth = coeffFor (kParamSmoothMs);
        match = coeffFor (comp::kMatchMs);
        trimSlew = coeffFor (comp::kTrimMs);

        // The sidechain high-pass - RBJ's Butterworth (Q = 1/sqrt 2), direct form.
        {
            const double w = 2.0 * 3.14159265358979323846 * comp::kSidechainHpfHz / static_cast<double> (sr);
            const double cw = std::cos (w);
            const double alpha = std::sin (w) / (2.0 * 0.70710678118654752);
            const double a0 = 1.0 + alpha;
            hpB0 = static_cast<float> ((1.0 + cw) * 0.5 / a0);
            hpB1 = static_cast<float> (-(1.0 + cw) / a0);
            hpB2 = hpB0;
            hpA1 = static_cast<float> (-2.0 * cw / a0);
            hpA2 = static_cast<float> ((1.0 - alpha) / a0);
        }

        meterPeriod = std::max (1, static_cast<int> (std::lround (sr * comp::kMeterPointMs * 0.001f)));

        reset();
    }

    void reset() noexcept
    {
        det = 0.0f;
        fast = 0.0f;
        slow = 0.0f;
        preDb = targetPreDb;
        inPow = 0.0f;
        rawPow = 0.0f;
        trimDb = 0.0f;
        hp.fill ({});

        meterPhase = 0;
        meterIn = 0.0f;
        meterOut = 0.0f;
        meterGr = 0.0f;
    }

    /** Sensitivity, 0..1 - the pre-gain, comp::sensitivityDbFor. */
    void setSensitivity01 (float v) noexcept { targetPreDb = comp::sensitivityDbFor (v); }

    /** Attack in ms, clamped to the knob's range. Also sets how far an onset
        may beat the gain (comp::maxOvershootDbFor). */
    void setAttackMs (float ms) noexcept
    {
        attackMs = std::clamp (ms, comp::kAttackMinMs, comp::kAttackMaxMs);
        attack = coeffFor (attackMs);
        maxOvershootDb = comp::maxOvershootDbFor (attackMs);
    }

    /** The sidechain high-pass (comp::kSidechainHpfHz) on the detector. The
        filter runs either way, so switching it never meets a cold state. */
    void setSidechainFilter (bool on) noexcept { sidechainFilter = on; }

    /** In place, both channels. `left` and `right` may not alias. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        const float h = comp::kOtaHeadroom;

        for (int i = 0; i < numSamples; ++i)
        {
            preDb += smooth * (targetPreDb - preDb);

            const float l = left[i];
            const float r = right[i];
            const float peak = std::max (std::abs (l), std::abs (r));

            // What the detector hears: the input, or the input less its lows.
            const float hl = highPass (hp[0], l);
            const float hr = highPass (hp[1], r);
            const float heard = sidechainFilter ? std::max (std::abs (hl), std::abs (hr)) : peak;

            // Detector: instant up, a short bleed down - see kDetectorReleaseMs.
            const float driven = heard * dbToGain (preDb);
            if (driven > det)
                det = driven;
            else
                det += detRelease * (driven - det);

            if (! (det > kSilence)) // also catches a NaN
                det = 0.0f;

            const float detDb = 20.0f * std::log10 (det + kSilence);
            // The ratio follows Sensitivity (see kRatioMin / kRatioMax), off
            // the smoothed pre-gain; worked out again only while it moves.
            if (preDb != ratioPreDb)
            {
                ratioPreDb = preDb;
                ratio = comp::ratioForPreGainDb (preDb);
            }

            const float target = comp::gainReductionDbFor (detDb, ratio);

            // The input's own envelope (the detector's, less the pre-gain) -
            // what gates the measured match below.
            const float inEnvDb = detDb - preDb;

            // Ballistics on the reduction itself (more negative = more).
            fast += (target < fast ? attack : relFast) * (target - fast);
            slow += (target < slow ? slowAttack : relSlow) * (target - slow);

            if (! std::isfinite (fast) || ! std::isfinite (slow))
                fast = slow = 0.0f;

            // The attack may let an onset through, but by no more than
            // maxOvershootDb (which grows with Attack) over where the curve
            // wants it.
            fast = std::min (fast, target + maxOvershootDb);

            const float gr = std::min (fast, slow);
            const float g0 = dbToGain (preDb + gr + comp::makeupDbFor (preDb));

            // The measured match: input power against the compressed power,
            // averaged over seconds, only while there is something to hear.
            if (inEnvDb > comp::kGateDb)
            {
                const float p = 0.5f * (l * l + r * r);
                inPow += match * (p - inPow);
                rawPow += match * (p * g0 * g0 - rawPow);
            }
            if (! std::isfinite (inPow) || ! std::isfinite (rawPow))
                inPow = rawPow = 0.0f;

            if (inPow > kSilence && rawPow > kSilence)
            {
                const float target = std::clamp (10.0f * std::log10 (inPow / rawPow), -comp::kMatchMaxDb,
                                                 comp::kMatchMaxDb);
                trimDb += trimSlew * (target - trimDb);
            }
            if (! std::isfinite (trimDb))
                trimDb = 0.0f;

            const float g = g0 * dbToGain (trimDb);

            const float yl = h * std::tanh (l * g / h);
            const float yr = h * std::tanh (r * g / h);

            left[i] = yl;
            right[i] = yr;

            meterIn = std::max (meterIn, peak);
            meterOut = std::max (meterOut, std::max (std::abs (yl), std::abs (yr)));
            meterGr = std::min (meterGr, gr);

            if (++meterPhase >= meterPeriod)
                pushMeterPoint();
        }
    }

    // ------------------------------------------------------------ the meter

    /** The measured match's current trim, dB - for tests. */
    float levelMatchDb() const noexcept { return trimDb; }

    uint32_t meterCount() const noexcept { return written.load (std::memory_order_acquire); }

    /** Point `index` (a running count, not a ring slot). Only meaningful for
        the last comp::kMeterRing counts. */
    MeterPoint meterPointAt (uint32_t index) const noexcept
    {
        const auto& slot = ring[index % static_cast<uint32_t> (comp::kMeterRing)];
        return { slot.inPeak.load (std::memory_order_relaxed), slot.outPeak.load (std::memory_order_relaxed),
                 slot.grDb.load (std::memory_order_relaxed) };
    }

    /** How often a point is written, in seconds - for a display that wants to
        pace its scroll by the audio rather than by its own timer. */
    float meterPeriodSeconds() const noexcept { return static_cast<float> (meterPeriod) / sr; }

private:
    static constexpr float kSilence = 1.0e-9f; // -180 dB
    static constexpr float kParamSmoothMs = 20.0f;

    struct AtomicPoint
    {
        std::atomic<float> inPeak { 0.0f };
        std::atomic<float> outPeak { 0.0f };
        std::atomic<float> grDb { 0.0f };
    };

    static float dbToGain (float db) noexcept { return std::exp (db * 0.11512925465f); } // ln(10)/20

    float coeffFor (float ms) const noexcept
    {
        return 1.0f - std::exp (-1.0f / (std::max (0.01f, ms) * 0.001f * sr));
    }

    struct Biquad
    {
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };

    float highPass (Biquad& s, float x) const noexcept
    {
        float y = hpB0 * x + hpB1 * s.x1 + hpB2 * s.x2 - hpA1 * s.y1 - hpA2 * s.y2;
        if (! std::isfinite (y) || std::abs (y) < 1.0e-20f)
            y = 0.0f;
        s.x2 = s.x1;
        s.x1 = x;
        s.y2 = s.y1;
        s.y1 = y;
        return y;
    }

    void pushMeterPoint() noexcept
    {
        const uint32_t n = written.load (std::memory_order_relaxed);
        auto& slot = ring[n % static_cast<uint32_t> (comp::kMeterRing)];
        slot.inPeak.store (meterIn, std::memory_order_relaxed);
        slot.outPeak.store (meterOut, std::memory_order_relaxed);
        slot.grDb.store (meterGr, std::memory_order_relaxed);
        written.store (n + 1, std::memory_order_release);

        meterPhase = 0;
        meterIn = 0.0f;
        meterOut = 0.0f;
        meterGr = 0.0f;
    }

    float sr = 44100.0f;

    float attackMs = comp::kDefaultAttackMs;
    float maxOvershootDb = comp::maxOvershootDbFor (comp::kDefaultAttackMs);
    bool sidechainFilter = true;

    float hpB0 = 1.0f, hpB1 = 0.0f, hpB2 = 0.0f, hpA1 = 0.0f, hpA2 = 0.0f;
    std::array<Biquad, 2> hp {};
    float targetPreDb = comp::sensitivityDbFor (comp::kDefaultSensitivityPct * 0.01f);
    float preDb = targetPreDb;
    float ratioPreDb = -1.0f; // the pre-gain `ratio` was last worked out at
    float ratio = comp::kRatioMin;

    float attack = 0.0f;
    float detRelease = 0.0f;
    float relFast = 0.0f;
    float slowAttack = 0.0f;
    float relSlow = 0.0f;
    float smooth = 0.0f;
    float match = 0.0f;
    float inPow = 0.0f;  // the input's mean square, over kMatchMs
    float rawPow = 0.0f; // the same after the computed gain, before the trim
    float trimSlew = 0.0f;
    float trimDb = 0.0f; // the measured match, slewed over kTrimMs

    float det = 0.0f;
    float fast = 0.0f;
    float slow = 0.0f;

    int meterPeriod = 220;
    int meterPhase = 0;
    float meterIn = 0.0f;
    float meterOut = 0.0f;
    float meterGr = 0.0f;

    std::array<AtomicPoint, comp::kMeterRing> ring {};
    std::atomic<uint32_t> written { 0 };
};

} // namespace ee::dsp
