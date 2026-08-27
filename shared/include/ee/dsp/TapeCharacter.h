#pragma once

#include "ee/dsp/ModDelayLine.h"

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

        shelfCoeff = onePoleCoeff (kShelfCornerHz, sr);
        dcCoeff = onePoleCoeff (kDcCornerHz, sr);
        envCoeff = onePoleCoeff (kEnvelopeCornerHz, sr);
        noiseHpCoeff = onePoleCoeff (kNoiseHighpassHz, sr);
        noiseLpCoeff = onePoleCoeff (kNoiseLowpassHz, sr);
        depthCoeff = onePoleCoeff (12.0f, sr);

        flutterInc = kFlutterHz / static_cast<float> (sr);
        scrapeInc = kScrapeHz / static_cast<float> (sr);

        for (auto& c : channels)
            c.line.prepare (sr, kNominalDelaySeconds * 3.0f);

        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        flutterPhase = 0.0f;
        scrapePhase = 0.31f;
        smoothedDepth = 0.0f;

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
            flutterPhase += flutterInc;
            if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;

            scrapePhase += scrapeInc;
            if (scrapePhase >= 1.0f) scrapePhase -= 1.0f;

            smoothedDepth += depthCoeff * (amount - smoothedDepth);

            const float wobble = smoothedDepth
                                 * (flutterDepth * std::sin (kTwoPi * flutterPhase)
                                    + scrapeDepth * std::sin (kTwoPi * scrapePhase));

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
        uint32_t rngState = 1u;
    };

    static constexpr float kTwoPi = 6.28318530718f;

    // Enough headroom for the wobble either side, and short enough that the
    // reported latency is well under anything a player would notice.
    static constexpr float kNominalDelaySeconds = 0.0015f;

    static constexpr float kFlutterHz = 4.7f;
    static constexpr float kScrapeHz = 3.4f;
    static constexpr float kFlutterSeconds = 0.00005f;
    static constexpr float kScrapeSeconds = 0.000035f;

    static constexpr float kShelfCornerHz = 1600.0f;
    static constexpr float kDcCornerHz = 18.0f;
    static constexpr float kEnvelopeCornerHz = 6.0f;

    // Shapes the grit into a broad presence-band bump. Two poles on the way
    // down, because a single one leaves the top octave brighter than tape.
    static constexpr float kNoiseHighpassHz = 3000.0f;
    static constexpr float kNoiseLowpassHz = 3600.0f;

    static constexpr float kMaxDrive = 0.95f;
    static constexpr float kMaxBias = 0.015f;
    static constexpr float kMaxShelfLoss = 0.04f;
    static constexpr float kMaxNoise = 0.61f;
    static constexpr float kMaxTrim = 0.09f;

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

        y += ch.noiseLpState2 * ch.envState * noiseGain;

        // Without this the asymmetry would settle into a DC offset.
        ch.dcState += dcCoeff * (y - ch.dcState);

        return y - ch.dcState;
    }

    void updateCoefficients() noexcept
    {
        drive = 1.0f + amount * kMaxDrive;
        bias = amount * kMaxBias;
        biasOffset = std::tanh (bias);

        // Slope of the curve at the origin is drive * (1 - tanh(bias)^2). The
        // trim on top puts the level back where the reference machine left it.
        makeup = (1.0f + amount * kMaxTrim) / (drive * (1.0f - biasOffset * biasOffset));

        shelfGain = 1.0f - amount * kMaxShelfLoss;
        noiseGain = amount * kMaxNoise;

        flutterDepth = kFlutterSeconds * static_cast<float> (sr);
        scrapeDepth = kScrapeSeconds * static_cast<float> (sr);
    }

    double sr = 44100.0;
    float amount = 0.0f;
    float nominalSamples = 66.0f;

    std::array<Channel, 2> channels;

    float flutterPhase = 0.0f;
    float scrapePhase = 0.0f;
    float flutterInc = 0.0f;
    float scrapeInc = 0.0f;
    float flutterDepth = 0.0f;
    float scrapeDepth = 0.0f;
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
    float noiseHpCoeff = 0.0f;
    float noiseLpCoeff = 0.0f;

    float dcCoeff = 0.0f;
};

} // namespace ee::dsp
