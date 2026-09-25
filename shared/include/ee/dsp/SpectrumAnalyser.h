#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace ee::dsp
{

namespace spectrum
{
inline constexpr int kFftOrder = 12;              // 4096 points: ~12 Hz a bin at 48 kHz
inline constexpr int kFftSize = 1 << kFftOrder;
inline constexpr int kRingSize = kFftSize * 2;    // room for the audio thread to run ahead of a frame
inline constexpr int kPoints = 160;               // log-spaced output points, 20 Hz .. 20 kHz
inline constexpr float kMinHz = 20.0f;
inline constexpr float kMaxHz = 20000.0f;
inline constexpr float kFloorDb = -100.0f;
inline constexpr float kTiltDbPerOctave = 4.5f;   // pivoting at 1 kHz: music reads roughly level, as on Pro-Q
inline constexpr float kReleaseDbPerSecond = 36.0f; // rises at once, falls at this rate
} // namespace spectrum

/** The audio-thread half of a spectrum display: a lock-free ring of the mono
 *  sum. Does nothing while inactive - the owner turns it on only while a
 *  display is open, so the audio is untouched and nothing is copied otherwise.
 *  Same single-writer / single-reader arrangement as TunerCapture.
 */
class SpectrumCapture
{
public:
    void prepare (double sampleRate) noexcept { rate.store (sampleRate, std::memory_order_relaxed); }

    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive, std::memory_order_release); }
    bool isActive() const noexcept { return active.load (std::memory_order_acquire); }

    /** Audio thread. `right` may be null. */
    void process (const float* left, const float* right, int numSamples) noexcept
    {
        if (! active.load (std::memory_order_relaxed))
            return;

        uint32_t pos = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i, ++pos)
        {
            const float x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            ring[pos % spectrum::kRingSize].store (std::isfinite (x) ? x : 0.0f, std::memory_order_relaxed);
        }
        writePos.store (pos, std::memory_order_release);
    }

    /** Message thread: the newest kFftSize samples, oldest first. */
    void readLatest (float* dest) const noexcept
    {
        const uint32_t end = writePos.load (std::memory_order_acquire);
        const uint32_t start = end - static_cast<uint32_t> (spectrum::kFftSize);
        for (int i = 0; i < spectrum::kFftSize; ++i)
            dest[i] = ring[(start + static_cast<uint32_t> (i)) % spectrum::kRingSize].load (std::memory_order_relaxed);
    }

    double sampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>, spectrum::kRingSize> ring {};
    std::atomic<uint32_t> writePos { 0 };
    std::atomic<bool> active { false };
    std::atomic<double> rate { 44100.0 };
};

/** The message-thread half: a Hann-windowed FFT of the capture's newest block,
 *  resampled onto kPoints log-spaced frequencies, tilted, and held with a slow
 *  release so it reads as a level rather than as flicker. Levels are dBFS for a
 *  full-scale sine at the tilt's 1 kHz pivot.
 */
class SpectrumAnalyser
{
public:
    using Frame = std::array<float, spectrum::kPoints>;

    SpectrumAnalyser()
    {
        window.resize (spectrum::kFftSize);
        for (int i = 0; i < spectrum::kFftSize; ++i)
            window[static_cast<size_t> (i)] =
                static_cast<float> (0.5 - 0.5 * std::cos (2.0 * kPi * i / (spectrum::kFftSize - 1)));
        buffer.resize (spectrum::kFftSize);
        samples.resize (spectrum::kFftSize);
        mags.resize (spectrum::kFftSize / 2 + 1);
        reset();
    }

    void reset() noexcept { held.fill (spectrum::kFloorDb); }

    /** Frequency of output point `i`. */
    static float frequencyAt (int i) noexcept
    {
        const double lo = std::log (spectrum::kMinHz), hi = std::log (spectrum::kMaxHz);
        return static_cast<float> (std::exp (lo + (hi - lo) * i / (spectrum::kPoints - 1)));
    }

    /** One frame, `dt` seconds after the last. */
    const Frame& update (const SpectrumCapture& capture, double dt)
    {
        capture.readLatest (samples.data());
        return analyse (samples.data(), capture.sampleRate(), dt);
    }

    /** The same, on a block the caller already has (kFftSize samples). */
    const Frame& analyse (const float* block, double sampleRate, double dt)
    {
        for (int i = 0; i < spectrum::kFftSize; ++i)
            buffer[static_cast<size_t> (i)] = { block[i] * window[static_cast<size_t> (i)], 0.0f };

        fft (buffer);

        // Hann's coherent gain is 0.5, so a full-scale sine lands at N/4.
        const float norm = 4.0f / spectrum::kFftSize;
        for (size_t k = 0; k < mags.size(); ++k)
            mags[k] = std::abs (buffer[k]) * norm;

        const double binHz = sampleRate / spectrum::kFftSize;
        const float fall = static_cast<float> (spectrum::kReleaseDbPerSecond * std::max (0.0, dt));

        for (int p = 0; p < spectrum::kPoints; ++p)
        {
            // Each point covers the half-way marks to its neighbours. Up high
            // that is many bins - the loudest one stands for them; down low it
            // is less than one, so the level is read between the two nearest.
            const double f = frequencyAt (p);
            const double fLo = frequencyAt (std::max (0, p - 1)), fHi = frequencyAt (std::min (spectrum::kPoints - 1, p + 1));
            const double a = std::sqrt (fLo * f) / binHz, b = std::sqrt (f * fHi) / binHz;
            const int last = static_cast<int> (mags.size()) - 1;

            float m;
            if (b - a >= 1.0)
            {
                m = 0.0f;
                for (int k = std::max (1, static_cast<int> (std::ceil (a))); k <= std::min (last, static_cast<int> (b)); ++k)
                    m = std::max (m, mags[static_cast<size_t> (k)]);
            }
            else
            {
                const double x = std::clamp (f / binHz, 1.0, static_cast<double> (last));
                const int k = std::min (last - 1, static_cast<int> (x));
                const double t = x - k;
                m = static_cast<float> (mags[static_cast<size_t> (k)] * (1.0 - t) + mags[static_cast<size_t> (k + 1)] * t);
            }

            float db = 20.0f * std::log10 (std::max (m, 1.0e-9f));
            db += spectrum::kTiltDbPerOctave * static_cast<float> (std::log2 (f / 1000.0));
            db = std::max (db, spectrum::kFloorDb);

            auto& h = held[static_cast<size_t> (p)];
            h = std::max (db, h - fall);
        }

        return held;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    /** In place, radix 2. 4096 points ~45 times a second is nothing on the
        message thread; not worth a dependency. */
    static void fft (std::vector<std::complex<float>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * kPi / static_cast<double> (len);
            const std::complex<float> wl (static_cast<float> (std::cos (ang)), static_cast<float> (std::sin (ang)));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    std::vector<float> window, samples, mags;
    std::vector<std::complex<float>> buffer;
    Frame held {};
};

} // namespace ee::dsp
