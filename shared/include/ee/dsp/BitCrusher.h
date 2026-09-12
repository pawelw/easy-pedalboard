#pragma once

#include "ee/dsp/BitCrusherConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** A bit crusher: integer sample-and-hold downsampling, bit-depth quantisation,
    a 2-pole post low-pass, and jitter on the sample-and-hold clock.

    Voiced from the JHS 3 Series Bit Crusher and Arturia's Bitcrusher, but tuned
    to stay playable on guitar:

      - the hold rate is an integer division of the host rate (hold N samples),
        so the aliasing stays phase-locked and harmonically related instead of
        beating and drifting in pitch;
      - a tracking anti-alias low-pass runs *before* the hold, cornered near the
        Nyquist of the decimated stream, so the images fold cleanly and the
        result reads as lost bandwidth rather than ring-mod hash;
      - Bits stops at 4, not 1 - below that a note is gated fuzz.

    Chain per channel: anti-alias low-pass -> sample & hold -> quantise -> post
    low-pass.

    With Bits at kBitsClean, N == 1 and the post low-pass bypassed, process() is
    a bit-exact pass-through - see the fast path - so selecting this engine with
    the knobs at rest is identical to not running it at all.

    Jitter's randomness is a per-channel xorshift re-seeded to a fixed value in
    reset(), so a render is reproducible and can be checksummed. The two channels
    seed differently, so heavy jitter decorrelates L/R rather than moving them
    together.
*/
class BitCrusher
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        baseN = 1;
        targetLpHz = bitcrush::kLpMaxHz;
        targetAaHz = 0.49f * static_cast<float> (sr);

        // One-pole glide on the two corners, evaluated once per control block.
        smoothCoeff =
            onePoleCoeff (1000.0f / bitcrush::kLpSmoothMs, sr / static_cast<double> (bitcrush::kControlBlock));

        reset();
    }

    void reset() noexcept
    {
        uint32_t seed = 0x1f2e3d4cu;

        for (auto& ch : channels)
        {
            ch.hold = 0;
            ch.held = 0.0f;
            ch.aaZ1 = 0.0f;
            ch.aaZ2 = 0.0f;
            ch.lpZ1 = 0.0f;
            ch.lpZ2 = 0.0f;
            ch.rng = seed;
            seed = seed * 1664525u + 1013904223u;
        }

        blockCounter = 0;
        currentLpHz = targetLpHz;
        currentAaHz = targetAaHz;
        updateCoeffs();
    }

    // -------------------------------------------------------------- the knobs

    /** Word length, clamped to a defensive 1..24. The musical range is the
        config's (kBitsCrushed..kBitsClean); this only guards against nonsense. */
    void setBits (float newBits) noexcept
    {
        bits = std::clamp (newBits, 1.0f, 24.0f);
        quantStep = 2.0f / std::pow (2.0f, bits);
    }

    /** Hold N input samples per output sample. N == 1 passes every sample. */
    void setDecimation (int n) noexcept
    {
        baseN = std::clamp (n, 1, 512);
        targetAaHz = std::clamp (bitcrush::kAntiAliasFrac * static_cast<float> (sr) / static_cast<float> (baseN), 20.0f,
                                 0.49f * static_cast<float> (sr));
    }

    /** Convenience for callers that think in Hz (tests, tuning tools): the
        nearest integer decimation to host / hz. */
    void setRateHz (float hz) noexcept { setDecimation (static_cast<int> (std::lround (sr / std::max (1.0f, hz)))); }

    /** Post low-pass corner in Hz. At or above kLpBypassHz the filter is
        skipped. */
    void setLowpassHz (float hz) noexcept { targetLpHz = std::clamp (hz, bitcrush::kLpMinHz, bitcrush::kLpMaxHz); }

    /** 0 = a fixed hold length, 1 = it swings up to kJitterDepth of N either
        side. */
    void setJitter01 (float j) noexcept { jitter = std::clamp (j, 0.0f, 1.0f); }

    /** Whether the tracking anti-alias low-pass runs ahead of the hold. On by
        default, which is what makes the Bit Crush engine read as lost bandwidth
        rather than hash. Peak Artifact's Amp turns it off: the reference that
        engine is measured against holds the raw signal, images and all, and a
        pre-filter there moves the result off the target by more than it tidies
        up. */
    void setAntiAlias (bool shouldFilter) noexcept { antiAlias = shouldFilter; }

    // -------------------------------------------------------------- the audio

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };

        const bool bypassHold = baseN <= 1;
        const bool bypassQuant = bits >= bitcrush::kBitsClean;
        const bool bypassLp = targetLpHz >= bitcrush::kLpBypassHz && currentLpHz >= bitcrush::kLpBypassHz;

        if (bypassHold && bypassQuant && bypassLp)
            return; // bit-exact pass-through

        for (int i = 0; i < numSamples; ++i)
        {
            if (blockCounter == 0)
            {
                currentLpHz += smoothCoeff * (targetLpHz - currentLpHz);
                currentAaHz += smoothCoeff * (targetAaHz - currentAaHz);
                updateCoeffs();
            }
            if (++blockCounter >= bitcrush::kControlBlock)
                blockCounter = 0;

            for (size_t c = 0; c < channels.size(); ++c)
            {
                auto& ch = channels[c];
                float x = io[c][i];

                if (! bypassHold)
                {
                    // Anti-alias before the hold - band-limit to the decimated
                    // stream's Nyquist so the images fold cleanly.
                    const float aa = antiAlias ? svfLowpass (ch.aaZ1, ch.aaZ2, aa1, aa2, aa3, x) : x;

                    if (ch.hold <= 0)
                    {
                        ch.held = aa;
                        ch.hold = nextHoldLength (ch);
                    }

                    x = ch.held;
                    --ch.hold;
                }

                float y = x;

                if (! bypassQuant)
                    y = quantStep * std::floor (y / quantStep + 0.5f);

                if (! bypassLp)
                    y = svfLowpass (ch.lpZ1, ch.lpZ2, lp1, lp2, lp3, y);

                io[c][i] = y;
            }
        }
    }

private:
    struct Channel
    {
        int hold = 0;
        float held = 0.0f;
        float aaZ1 = 0.0f, aaZ2 = 0.0f; // anti-alias pre-filter state
        float lpZ1 = 0.0f, lpZ2 = 0.0f; // post low-pass state
        uint32_t rng = 1u;
    };

    static constexpr float kPi = 3.14159265359f;
    static constexpr float kQ = 0.70710678f; // Butterworth

    static float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = 2.0f * kPi * cornerHz / static_cast<float> (sampleRate);
        return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
    }

    /** Uniform in [0, 1). xorshift32 - the same generator TapeCharacter uses. */
    static float nextUniform (uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state) * 2.3283064365386963e-10f;
    }

    int nextHoldLength (Channel& ch) noexcept
    {
        if (jitter <= 0.0f)
            return baseN;

        const float u = nextUniform (ch.rng) * 2.0f - 1.0f;
        const int delta =
            static_cast<int> (std::lround (jitter * bitcrush::kJitterDepth * static_cast<float> (baseN) * u));
        return std::max (1, baseN + delta);
    }

    void svfCoeffs (float hz, float& c1, float& c2, float& c3) const noexcept
    {
        const float nyq = 0.49f * static_cast<float> (sr);
        const float fc = std::clamp (hz, 10.0f, nyq);
        const float g = std::tan (kPi * fc / static_cast<float> (sr));
        const float k = 1.0f / kQ;

        c1 = 1.0f / (1.0f + g * (g + k));
        c2 = g * c1;
        c3 = g * c2;
    }

    void updateCoeffs() noexcept
    {
        svfCoeffs (currentLpHz, lp1, lp2, lp3);
        svfCoeffs (currentAaHz, aa1, aa2, aa3);
    }

    static float svfLowpass (float& z1, float& z2, float a1, float a2, float a3, float v0) noexcept
    {
        const float v3 = v0 - z2;
        const float v1 = a1 * z1 + a2 * v3;
        const float v2 = z2 + a2 * z1 + a3 * v3;
        z1 = 2.0f * v1 - z1;
        z2 = 2.0f * v2 - z2;
        return v2;
    }

    double sr = 44100.0;

    float bits = bitcrush::kBitsClean;
    float quantStep = 2.0f / 65536.0f; // 2 / 2^16
    int baseN = 1;
    float jitter = 0.0f;
    bool antiAlias = true;

    float targetLpHz = bitcrush::kLpMaxHz;
    float currentLpHz = bitcrush::kLpMaxHz;
    float targetAaHz = 20000.0f;
    float currentAaHz = 20000.0f;
    float smoothCoeff = 1.0f;

    float lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f;
    float aa1 = 0.0f, aa2 = 0.0f, aa3 = 0.0f;

    int blockCounter = 0;

    std::array<Channel, 2> channels;
};

} // namespace ee::dsp
