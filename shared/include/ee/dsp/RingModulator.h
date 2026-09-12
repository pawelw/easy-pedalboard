#pragma once

#include "ee/dsp/RingModulatorConfig.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ee::dsp
{

/** A ring modulator: the input multiplied by a sine carrier (classic DSB-SC),
    with a post 2-pole low-pass and two voicings picked by Mode.

    Voiced from the JHS 3 Series Ring Modulator (Frequency, Tweak, Mode; its
    Blend is the owning module's Mix), plus a low-pass and a Rectify control the
    JHS does not have:

      - Earworm (Mode 0), after the Way Huge Ringworm: a straight carrier
        multiply. Tweak runs a fixed-rate sine LFO on the carrier frequency
        (kTweakLfoHz / kTweakDepth) so a static tone wobbles and comes alive.
      - Green Lantern (Mode 1), after the Green Ringer: the carrier steady, and
        Tweak crossfades in a full-wave-rectified octave-up (DC-blocked, with a
        little make-up gain) on top of the ring mod.

    Rectify (bipolar, resting at 0) folds one half of the carrier onto the
    other before the multiply. That puts DC in the carrier, and a carrier with
    DC passes the dry note through, so the pedal walks from ring modulation
    toward amplitude modulation and the player's pitch comes back under the
    ring - see kDefaultRectifyPct in the config for the full reasoning. It
    applies in both modes: it is a property of the carrier, not of the voicing.

    Chain per channel: carrier multiply (+ optional octave blend) -> post
    low-pass. With the Filter knob fully up the low-pass stage is skipped.

    Everything here is pure float arithmetic - there is no RNG anywhere, so a
    render is fully reproducible and can be checksummed, and the carrier phase
    is instance state so two instances on different threads do not interfere.
    Unlike BitCrusher there is no bit-exact rest state: a ring modulator always
    multiplies by the carrier, so selecting this engine always changes the
    signal. The owning module's engine crossfade covers the switch.
*/
class RingModulator
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        targetFreqHz = ringmod::freqHzFor (0.0f);
        targetLpHz = ringmod::kLpMaxHz;

        // One-pole glide on the carrier frequency and the low-pass corner,
        // evaluated once per control block.
        smoothCoeff = onePoleCoeff (1000.0f / ringmod::kSmoothMs, sr / static_cast<double> (ringmod::kControlBlock));

        reset();
    }

    void reset() noexcept
    {
        carrierPhase = 0.0f;
        lfoPhase = 0.0f;

        for (auto& ch : channels)
        {
            ch.lpZ1 = 0.0f;
            ch.lpZ2 = 0.0f;
            ch.dcX1 = 0.0f;
            ch.dcY1 = 0.0f;
        }

        blockCounter = 0;
        currentFreqHz = targetFreqHz;
        currentLpHz = targetLpHz;
        currentRectify = targetRectify;
        updateCoeffs();
    }

    // -------------------------------------------------------------- the knobs

    /** Carrier frequency in Hz, clamped to the config's musical range. */
    void setFrequencyHz (float hz) noexcept
    {
        targetFreqHz = std::clamp (hz, ringmod::kFreqMinHz, ringmod::kFreqMaxHz);
    }

    /** Convenience for callers that think in knob positions. */
    void setFrequency01 (float knob01) noexcept { setFrequencyHz (ringmod::freqHzFor (knob01)); }

    void setTweak01 (float t) noexcept { tweak = std::clamp (t, 0.0f, 1.0f); }

    /** Carrier rectification, -1..+1. 0 is a plain bipolar sine and changes
        nothing; +1 folds the negative half up, -1 the positive half down. */
    void setRectify (float r) noexcept { targetRectify = std::clamp (r, -1.0f, 1.0f); }

    /** 0 = Earworm (Tweak wobbles the carrier), 1 = Green Lantern (Tweak
        crossfades in a rectified octave-up). Anything else is treated as
        Earworm. */
    void setMode (int m) noexcept
    {
        mode = (m == ringmod::kModeGreenLantern) ? ringmod::kModeGreenLantern : ringmod::kModeEarworm;
    }

    /** Post low-pass corner in Hz. At or above kLpBypassHz the filter is
        skipped. */
    void setLowpassHz (float hz) noexcept { targetLpHz = std::clamp (hz, ringmod::kLpMinHz, ringmod::kLpMaxHz); }

    void setLowpass01 (float knob01) noexcept { setLowpassHz (ringmod::lpHzFor (knob01)); }

    // -------------------------------------------------------------- the audio

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };

        const float lfoInc = ringmod::kTweakLfoHz / static_cast<float> (sr);
        const bool greenLantern = mode == ringmod::kModeGreenLantern;
        const bool bypassLp = targetLpHz >= ringmod::kLpBypassHz && currentLpHz >= ringmod::kLpBypassHz;

        for (int i = 0; i < numSamples; ++i)
        {
            if (blockCounter == 0)
            {
                currentFreqHz += smoothCoeff * (targetFreqHz - currentFreqHz);
                currentLpHz += smoothCoeff * (targetLpHz - currentLpHz);
                currentRectify += smoothCoeff * (targetRectify - currentRectify);
                updateCoeffs();
            }
            if (++blockCounter >= ringmod::kControlBlock)
                blockCounter = 0;

            // Earworm wobbles the carrier frequency with a slow LFO; Green
            // Lantern leaves it steady and spends Tweak on the octave instead.
            float fc = currentFreqHz;
            if (! greenLantern)
            {
                const float lfo = std::sin (lfoPhase * kTwoPi);
                fc = currentFreqHz * (1.0f + tweak * ringmod::kTweakDepth * lfo);
            }
            fc = std::clamp (fc, 0.0f, 0.49f * static_cast<float> (sr));

            float carrier = std::sin (carrierPhase * kTwoPi);

            if (currentRectify != 0.0f)
            {
                const float a = std::fabs (currentRectify);
                carrier = (1.0f - a) * carrier + currentRectify * std::fabs (carrier);
            }

            carrierPhase += fc / static_cast<float> (sr);
            carrierPhase -= std::floor (carrierPhase);

            lfoPhase += lfoInc;
            lfoPhase -= std::floor (lfoPhase);

            for (size_t c = 0; c < channels.size(); ++c)
            {
                auto& ch = channels[c];
                const float x = io[c][i];

                float y = x * carrier;

                if (greenLantern)
                {
                    // Full-wave rectify -> octave up, DC-blocked so the
                    // rectifier's offset does not pump the mix.
                    const float rect = std::fabs (x);
                    const float oct = rect - ch.dcX1 + ringmod::kDcBlockR * ch.dcY1;
                    ch.dcX1 = rect;
                    ch.dcY1 = oct;

                    const float octave = oct * ringmod::kOctaveMakeupGain;
                    y += tweak * (octave - y);
                }

                if (! bypassLp)
                    y = svfLowpass (ch.lpZ1, ch.lpZ2, lp1, lp2, lp3, y);

                io[c][i] = y;
            }
        }
    }

private:
    struct Channel
    {
        float lpZ1 = 0.0f, lpZ2 = 0.0f; // post low-pass state
        float dcX1 = 0.0f, dcY1 = 0.0f; // DC blocker on the rectified octave
    };

    static constexpr float kPi = 3.14159265359f;
    static constexpr float kTwoPi = 6.28318530718f;
    static constexpr float kQ = 0.70710678f; // Butterworth

    static float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = 2.0f * kPi * cornerHz / static_cast<float> (sampleRate);
        return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
    }

    void updateCoeffs() noexcept
    {
        const float nyq = 0.49f * static_cast<float> (sr);
        const float fc = std::clamp (currentLpHz, 10.0f, nyq);
        const float g = std::tan (kPi * fc / static_cast<float> (sr));
        const float k = 1.0f / kQ;

        lp1 = 1.0f / (1.0f + g * (g + k));
        lp2 = g * lp1;
        lp3 = g * lp2;
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

    float targetFreqHz = 125.0f;
    float currentFreqHz = 125.0f;
    float targetLpHz = ringmod::kLpMaxHz;
    float currentLpHz = ringmod::kLpMaxHz;
    float smoothCoeff = 1.0f;

    float targetRectify = 0.0f;
    float currentRectify = 0.0f;

    float tweak = 0.0f;
    int mode = ringmod::kModeEarworm;

    float carrierPhase = 0.0f; // 0..1
    float lfoPhase = 0.0f;     // 0..1

    float lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f;

    int blockCounter = 0;

    std::array<Channel, 2> channels;
};

} // namespace ee::dsp
