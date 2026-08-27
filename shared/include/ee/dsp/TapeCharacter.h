#pragma once

#include "ee/dsp/ModDelayLine.h"
#include "ee/dsp/TapeTuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** A tape machine sitting in front of whatever comes next: flutter, drive, head
    loss, and grit that rides the programme.

    Deliberately not a bit crusher. Decimation and quantisation read as digital
    however they are dressed up. Measured against a reference machine, what tape
    actually does at full tilt is:

      - leave the midband alone, within about 0.2 dB from 50 Hz to 3 kHz;
      - hold the level, and take roughly a quarter of a dB off the crest;
      - lay a broad band of noise over the top that follows the signal instead
        of sitting under it, which is the difference between grit and hiss;
      - wobble the whole signal by about 0.17 ms peak to peak at 3-5 Hz.

    Both channels share one set of flutter oscillators - a capstan wobbles the
    whole machine, and giving each side its own turns the effect into a chorus.
    The grit is seeded per channel, so only that part is decorrelated.
*/
class TapeCharacter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;

        // Rounded to a whole sample so that at amount 0 the line reads a stored
        // sample rather than an interpolated one, and the stage is bit exact.
        nominalSamples = std::round (kNominalDelaySeconds * static_cast<float> (sr));

        for (auto& c : channels)
            c.line.prepare (sr, kNominalDelaySeconds * 3.0f);

        updateRates();
        updateCoefficients();
        reset();
    }

    const TapeTuning& getTuning() const noexcept { return tuning; }

    /** Swaps the voicing without disturbing the running state. */
    void setTuning (const TapeTuning& newTuning) noexcept
    {
        tuning = newTuning;
        updateRates();
        updateCoefficients();
    }

    void reset() noexcept
    {
        smoothedDepth = 0.0f;
        modRng = 0x2545f491u;

        for (auto& s : modState)
            s = 0.0f;

        uint32_t seed = 0x9e3779b9u;

        for (auto& c : channels)
        {
            c.line.reset();
            c.shelfState = 0.0f;
            c.dcState = 0.0f;
            c.envState = 0.0f;
            c.noiseHpState = 0.0f;
            c.noiseLpState = 0.0f;
            c.noiseLpState2 = 0.0f;
            c.hiCutZ1 = 0.0f;
            c.hiCutZ2 = 0.0f;
            c.rngState = seed;
            seed = seed * 1664525u + 1013904223u;
        }
    }

    void setAmount (float amount01) noexcept
    {
        const float next = std::clamp (amount01, 0.0f, 1.0f);
        if (next != amount)
        {
            amount = next;
            updateCoefficients();
        }
    }

    float getAmount() const noexcept { return amount; }

    /** Constant, so the host can compensate it whatever the knob is doing. */
    int getLatencySamples() const noexcept { return static_cast<int> (nominalSamples); }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedDepth += depthCoeff * (amount - smoothedDepth);

            float wobble = smoothedDepth * modDepth * modulator();
            wobble = std::clamp (wobble, -tuning.wobbleLimitSamples, tuning.wobbleLimitSamples);

            for (size_t c = 0; c < channels.size(); ++c)
            {
                auto& ch = channels[c];

                ch.line.write (io[c][i]);
                float y = ch.line.read (nominalSamples + wobble);
                ch.line.advance();

                if (amount > 0.0f)
                    y = colour (ch, y);

                io[c][i] = y;
            }
        }
    }

private:
    struct Channel
    {
        ModDelayLine line;
        float shelfState = 0.0f;
        float dcState = 0.0f;
        float envState = 0.0f;
        float noiseHpState = 0.0f;
        float noiseLpState = 0.0f;
        float noiseLpState2 = 0.0f;
        float hiCutZ1 = 0.0f;
        float hiCutZ2 = 0.0f;
        uint32_t rngState = 1u;
    };

    static constexpr float kTwoPi = 6.28318530718f;

    // Enough headroom for the wobble either side, and short enough that the
    // reported latency is well under anything a player would notice. Not
    // tunable: it sets the buffer size and the reported latency.
    static constexpr float kNominalDelaySeconds = 0.0015f;

    static float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = kTwoPi * cornerHz / static_cast<float> (sampleRate);
        return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
    }

    static float whiteNoise (uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * 4.6566129e-10f;
    }

    float colour (Channel& ch, float x) noexcept
    {
        // The bias is what puts even harmonics in; subtracting tanh(bias) keeps
        // the curve through the origin so quiet passages stay quiet.
        float y = (std::tanh (drive * x + bias) - biasOffset) * makeup;

        ch.shelfState += shelfCoeff * (y - ch.shelfState);
        y = shelfGain * y + (1.0f - shelfGain) * ch.shelfState;

        ch.envState += envCoeff * (std::abs (y) - ch.envState);

        float n = whiteNoise (ch.rngState);
        ch.noiseHpState += noiseHpCoeff * (n - ch.noiseHpState);
        n -= ch.noiseHpState;
        ch.noiseLpState += noiseLpCoeff * (n - ch.noiseLpState);
        ch.noiseLpState2 += noiseLpCoeff * (ch.noiseLpState - ch.noiseLpState2);

        // Grit rises faster than the signal does, so it bites on the loud parts
        // and gets out of the way as they die instead of hissing over the tail.
        const float tilt = std::min (tuning.maxTilt, std::sqrt (ch.envState * envReferenceInv));

        y += ch.noiseLpState2 * ch.envState * tilt * noiseGain;

        // Without this the asymmetry would settle into a DC offset.
        ch.dcState += dcCoeff * (y - ch.dcState);
        y -= ch.dcState;

        // Gap loss. Mixed in rather than switched, so it arrives with the knob
        // and never touches a signal the stage is not already colouring.
        const float lp = hiCutB0 * y + ch.hiCutZ1;
        ch.hiCutZ1 = hiCutB1 * y - hiCutA1 * lp + ch.hiCutZ2;
        ch.hiCutZ2 = hiCutB2 * y - hiCutA2 * lp;

        return y + (lp - y) * amount;
    }

    float modulator() noexcept
    {
        float sum = 0.0f;

        for (int k = 0; k < 3; ++k)
        {
            modState[k] += modCoeff[k] * (whiteNoise (modRng) - modState[k]);
            sum += modState[k] * modNorm[k];
        }

        return sum * 0.57735027f; // three independent unit-variance terms
    }

    void updateRates() noexcept
    {
        shelfCoeff = onePoleCoeff (tuning.shelfCornerHz, sr);
        dcCoeff = onePoleCoeff (tuning.dcCornerHz, sr);
        envCoeff = onePoleCoeff (tuning.envelopeCornerHz, sr);
        noiseHpCoeff = onePoleCoeff (tuning.noiseHighpassHz, sr);
        noiseLpCoeff = onePoleCoeff (tuning.noiseLowpassHz, sr);
        depthCoeff = onePoleCoeff (12.0f, sr);

        modDepth = tuning.modRmsSeconds * static_cast<float> (sr);

        const float corners[3] = { tuning.modCornerLowHz, tuning.modCornerMidHz,
                                   tuning.modCornerHighHz };

        for (int k = 0; k < 3; ++k)
        {
            modCoeff[k] = onePoleCoeff (std::max (0.05f, corners[k]), sr);
            // A one-pole on white noise has variance c / (2 - c); undo it so the
            // depth setting means what it says.
            modNorm[k] = std::sqrt ((2.0f - modCoeff[k]) / modCoeff[k]);
        }

        envReferenceInv = 1.0f / std::max (1.0e-5f, tuning.envReference);

        updateHiCut();
    }

    void updateHiCut() noexcept
    {
        const float nyquist = 0.45f * static_cast<float> (sr);
        const float f0 = std::clamp (tuning.hiCutHz, 200.0f, nyquist);
        const float q = std::max (0.1f, tuning.hiCutQ);

        const float w0 = kTwoPi * f0 / static_cast<float> (sr);
        const float cosw = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * q);

        const float b = (1.0f - cosw) * 0.5f;
        const float a0 = 1.0f + alpha;

        hiCutB0 = b / a0;
        hiCutB1 = (1.0f - cosw) / a0;
        hiCutB2 = b / a0;
        hiCutA1 = (-2.0f * cosw) / a0;
        hiCutA2 = (1.0f - alpha) / a0;
    }

    void updateCoefficients() noexcept
    {
        drive = 1.0f + amount * tuning.maxDrive;
        bias = amount * tuning.maxBias;
        biasOffset = std::tanh (bias);

        // Slope of the curve at the origin is drive * (1 - tanh(bias)^2). The
        // trim on top puts the level back where the reference machine left it.
        makeup = (1.0f + amount * tuning.maxTrim) / (drive * (1.0f - biasOffset * biasOffset));

        shelfGain = 1.0f - amount * tuning.maxShelfLoss;
        noiseGain = amount * tuning.maxNoise;
    }

    TapeTuning tuning;

    double sr = 44100.0;
    float amount = 0.0f;
    float nominalSamples = 66.0f;

    std::array<Channel, 2> channels;

    float modState[3] = { 0.0f, 0.0f, 0.0f };
    float modCoeff[3] = { 0.0f, 0.0f, 0.0f };
    float modNorm[3] = { 1.0f, 1.0f, 1.0f };
    float modDepth = 0.0f;
    uint32_t modRng = 0x2545f491u;

    float smoothedDepth = 0.0f;
    float depthCoeff = 0.0f;
    float drive = 1.0f;
    float bias = 0.0f;
    float biasOffset = 0.0f;
    float makeup = 1.0f;

    float shelfGain = 1.0f;
    float shelfCoeff = 0.0f;

    float noiseGain = 0.0f;
    float envCoeff = 0.0f;
    float envReferenceInv = 50.0f;
    float noiseHpCoeff = 0.0f;
    float noiseLpCoeff = 0.0f;

    float dcCoeff = 0.0f;

    float hiCutB0 = 1.0f;
    float hiCutB1 = 0.0f;
    float hiCutB2 = 0.0f;
    float hiCutA1 = 0.0f;
    float hiCutA2 = 0.0f;
};

} // namespace ee::dsp
