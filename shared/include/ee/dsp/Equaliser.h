#pragma once

#include "ee/dsp/EqualiserConfig.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ee::dsp
{

/** A parametric EQ: up to kMaxBands independent bands in series, each one of
 * the eq::FilterType shapes. BitBit Alpine's pre-EQ runs both of its faces on
 * one of these - the three Simple bands and the eight Advanced ones - and
 * switches between them by turning bands on and off.
 *
 * Every band glides. Freq, gain and Q follow their targets on a one-pole
 * (eq::kSmoothSeconds), with the coefficients recomputed every
 * eq::kControlInterval samples while anything is still moving. What cannot
 * glide - a band switching on or off, or changing shape - crossfades instead:
 * the band's output is `dry + mix * (filtered - dry)`, and mix ramps over
 * eq::kFadeSeconds. A shape change fades the band out, swaps it while it is
 * silent, and fades the new one in.
 *
 * **A band doing nothing is not in the path.** Off, or a shelf or bell within
 * eq::kNeutralDb of 0 dB, fades out and then is skipped outright - so an
 * Equaliser with every band flat returns its input sample for sample, and a
 * plugin that gains one changes no existing render until somebody reaches for
 * it. The coefficient maths is RBJ's cookbook in double precision, with double
 * state, because a 20 Hz corner at 96 kHz is where float biquads go wrong.
 */
class Equaliser
{
public:
    static constexpr int kMaxBands = 12;
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxStages = 4;

    struct BandSettings
    {
        eq::FilterType type = eq::FilterType::bell;
        float hz = 1000.0f;
        float gainDb = 0.0f;
        float q = eq::kDefaultQ;
        bool on = false;
    };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const double chunk = static_cast<double> (eq::kControlInterval);
        glide = 1.0 - std::exp (-chunk / (eq::kSmoothSeconds * sr));
        fadeStep = static_cast<float> (1.0 / (eq::kFadeSeconds * sr));
        reset();
    }

    /** Clears every band's state and jumps it to its target - no glide, no fade. */
    void reset() noexcept
    {
        for (auto& b : bands)
        {
            b.type = b.target.type;
            b.logHz = std::log (clampHz (b.target.hz));
            b.gainDb = b.target.gainDb;
            b.logQ = std::log (clampQ (b.target.q));
            b.mix = wanted (b) ? 1.0f : 0.0f;
            b.clearState();
            b.design (sr);
        }
    }

    /** Where band `index` should go. Cheap: a copy, nothing is computed here. */
    void setBand (int index, const BandSettings& settings) noexcept
    {
        if (index >= 0 && index < kMaxBands)
            bands[static_cast<size_t> (index)].target = settings;
    }

    /** Whether every band is out of the path - nothing to do, nothing fading. */
    bool isIdle() const noexcept
    {
        return std::all_of (bands.begin(), bands.end(), [] (const Band& b) { return b.mix <= 0.0f && ! wanted (b); });
    }

    /** In place, on up to kMaxChannels channels. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, kMaxChannels);

        for (int start = 0; start < numSamples; start += eq::kControlInterval)
        {
            const int n = std::min (eq::kControlInterval, numSamples - start);

            for (auto& b : bands)
            {
                if (! advance (b, n))
                    continue;

                for (int ch = 0; ch < numChannels; ++ch)
                    b.run (ch, channels[ch] + start, n, fadeStep);

                b.mix = b.mixEnd;
            }
        }
    }

private:
    struct Coeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct Band
    {
        BandSettings target;

        eq::FilterType type = eq::FilterType::bell;
        double logHz = std::log (1000.0);
        double gainDb = 0.0;
        double logQ = std::log (static_cast<double> (eq::kDefaultQ));

        float mix = 0.0f;    // this chunk's start
        float mixEnd = 0.0f; // and its end, ramped between per sample
        int numStages = 1;
        std::array<Coeffs, kMaxStages> stage {};
        double z[kMaxChannels][kMaxStages][2] {};

        void clearState() noexcept
        {
            for (auto& ch : z)
                for (auto& s : ch)
                    s[0] = s[1] = 0.0;
        }

        void design (double sampleRate) noexcept
        {
            using eq::FilterType;
            const double hz = std::exp (logHz);
            const double q = std::exp (logQ);

            switch (type)
            {
            case FilterType::lowCut48:
            case FilterType::highCut48:
                numStages = 4;
                for (int s = 0; s < 4; ++s)
                {
                    double sq = eq::kButterworth8Q[static_cast<size_t> (s)];
                    if (s == 3)
                        sq *= q / eq::kButterworthQ;
                    stage[static_cast<size_t> (s)] =
                        type == FilterType::lowCut48 ? highPass (sampleRate, hz, sq) : lowPass (sampleRate, hz, sq);
                }
                break;
            case FilterType::lowCut12:
                numStages = 1;
                stage[0] = highPass (sampleRate, hz, q);
                break;
            case FilterType::highCut12:
                numStages = 1;
                stage[0] = lowPass (sampleRate, hz, q);
                break;
            case FilterType::lowShelf:
                numStages = 1;
                stage[0] = shelf (sampleRate, hz, q, gainDb, false);
                break;
            case FilterType::highShelf:
                numStages = 1;
                stage[0] = shelf (sampleRate, hz, q, gainDb, true);
                break;
            case FilterType::notch:
                numStages = 1;
                stage[0] = notchFilter (sampleRate, hz, q);
                break;
            case FilterType::bell:
            default:
                numStages = 1;
                stage[0] = peak (sampleRate, hz, q, gainDb);
                break;
            }
        }

        void run (int ch, float* data, int n, float step) noexcept
        {
            float m = mix;
            const float dm = mixEnd > mix ? step : (mixEnd < mix ? -step : 0.0f);
            auto& state = z[ch];

            for (int i = 0; i < n; ++i)
            {
                const double x = data[i];
                double y = x;

                for (int s = 0; s < numStages; ++s)
                {
                    const auto& c = stage[static_cast<size_t> (s)];
                    const double out = c.b0 * y + state[s][0];
                    state[s][0] = c.b1 * y - c.a1 * out + state[s][1];
                    state[s][1] = c.b2 * y - c.a2 * out;
                    y = out;
                }

                m = std::clamp (m + dm, std::min (mix, mixEnd), std::max (mix, mixEnd));
                data[i] = static_cast<float> (x + static_cast<double> (m) * (y - x));
            }
        }
    };

    static bool wanted (const Band& b) noexcept
    {
        const auto& t = b.target;
        if (! t.on || t.type != b.type)
            return false;
        return ! eq::hasGain (t.type) || std::abs (t.gainDb) > eq::kNeutralDb;
    }

    /** Moves one band on by `n` samples' worth of glide and fade. False when it
        is out of the path for the whole chunk and can be skipped. */
    bool advance (Band& b, int n) noexcept
    {
        const bool want = wanted (b);

        if (b.mix <= 0.0f && ! want)
        {
            // Silent: this is where a new shape is swapped in and where every
            // control jumps to its target, so a band fading back in starts
            // from where it is going rather than gliding from where it was.
            b.type = b.target.type;
            b.logHz = std::log (clampHz (b.target.hz));
            b.gainDb = b.target.gainDb;
            b.logQ = std::log (clampQ (b.target.q));
            b.clearState();
            b.mix = b.mixEnd = 0.0f;

            if (! wanted (b))
                return false;

            b.design (sr);
        }

        const float fade = fadeStep * static_cast<float> (n);
        b.mixEnd = wanted (b) ? std::min (1.0f, b.mix + fade) : std::max (0.0f, b.mix - fade);

        const double hz = std::log (clampHz (b.target.hz));
        const double db = static_cast<double> (b.target.gainDb);
        const double q = std::log (clampQ (b.target.q));

        const bool moving = std::abs (hz - b.logHz) > 1.0e-4 || std::abs (db - b.gainDb) > 1.0e-3
                         || std::abs (q - b.logQ) > 1.0e-4;

        if (moving)
        {
            b.logHz += glide * (hz - b.logHz);
            b.gainDb += glide * (db - b.gainDb);
            b.logQ += glide * (q - b.logQ);
            b.design (sr);
        }
        else if (hz != b.logHz || db != b.gainDb || q != b.logQ)
        {
            // Close enough to stop gliding: land exactly, once.
            b.logHz = hz;
            b.gainDb = db;
            b.logQ = q;
            b.design (sr);
        }

        return true;
    }

    double clampHz (float hz) const noexcept
    {
        return std::clamp (static_cast<double> (hz), static_cast<double> (eq::kMinHz) * 0.5, sr * 0.45);
    }

    static double clampQ (float q) noexcept
    {
        return std::clamp (static_cast<double> (q), static_cast<double> (eq::kMinQ), static_cast<double> (eq::kMaxQ));
    }

    // ---------------------------------------------------- RBJ cookbook biquads
    static Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    static constexpr double kTwoPi = 6.283185307179586;

    static Coeffs lowPass (double fs, double hz, double q) noexcept
    {
        const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    static Coeffs highPass (double fs, double hz, double q) noexcept
    {
        const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    static Coeffs notchFilter (double fs, double hz, double q) noexcept
    {
        const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise (1.0, -2.0 * c, 1.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    static Coeffs peak (double fs, double hz, double q, double db) noexcept
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    static Coeffs shelf (double fs, double hz, double q, double db, bool high) noexcept
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double k = 2.0 * std::sqrt (A) * alpha;

        if (high)
            return normalise (A * ((A + 1.0) + (A - 1.0) * c + k), -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                              A * ((A + 1.0) + (A - 1.0) * c - k), (A + 1.0) - (A - 1.0) * c + k,
                              2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - k);

        return normalise (A * ((A + 1.0) - (A - 1.0) * c + k), 2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                          A * ((A + 1.0) - (A - 1.0) * c - k), (A + 1.0) + (A - 1.0) * c + k,
                          -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - k);
    }

    std::array<Band, kMaxBands> bands {};
    double sr = 44100.0;
    double glide = 1.0;
    float fadeStep = 1.0f;
};

} // namespace ee::dsp
