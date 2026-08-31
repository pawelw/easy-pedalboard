#pragma once

#include "PhaserConfig.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace ee::dsp
{

/** Classic analog-style stereo phaser.

    A cascade of first-order all-pass sections whose corner frequency is swept
    by an LFO. The all-pass output is summed with the dry signal (see
    phaser::kWetMix), and where the two are out of phase the sum notches - a set
    of moving nulls sliding up and down the spectrum. A slice of the last
    stage's output is fed back into the input (phaser::kFeedback), which pulls
    the notches into resonant peaks and gives the sweep its liquid edge.

    The right channel runs its own cascade with the LFO offset by
    phaser::kStereoOffsetCycles, so the notches move out of step across the
    image and it opens up from a mono source.

    Two knobs reach this engine:
      setRateHz  - LFO speed
      setDepth01 - how wide the corner frequency sweeps, shrinking symmetrically
                   about the geometric centre of the configured range, so Depth
                   0 still colours the tone rather than switching the effect off

    Everything else is fixed in ee/dsp/PhaserConfig.h.

    Pure DSP: no JUCE audio-processor types, so it unit-tests headless the same
    way Chorus and FdnReverb do.
*/
class Phaser
{
public:
    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        logSweepMin = std::log (phaser::kSweepMinHz);
        logSweepMax = std::log (phaser::kSweepMaxHz);
        logSweepMid = 0.5f * (logSweepMin + logSweepMax);
        logSweepHalfSpan = 0.5f * (logSweepMax - logSweepMin);

        hpCoeff = onePoleCoeff (phaser::kWetHighPassHz);

        reset();
    }

    void reset()
    {
        for (auto& ch : apY) ch.fill (0.0f);
        for (auto& ch : apX) ch.fill (0.0f);
        fbState.fill (0.0f);
        hpZ.fill (0.0f);
        lfoPhase = 0.0;
    }

    void setRateHz (float hz) noexcept
    {
        rateHz = juce::jlimit (phaser::kRateMinHz, phaser::kRateMaxHz, hz);
    }

    void setDepth01 (float depth01) noexcept
    {
        depth = juce::jlimit (0.0f, 1.0f, depth01);
    }

    /** Process one block. `inR` may be null for a mono source. The in and out
        pointers may alias (each input sample is read before its output is
        written). */
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples) noexcept
    {
        if (! std::isfinite (lfoPhase))
            lfoPhase = 0.0;

        const int stages = juce::jlimit (1, phaser::kMaxStages, phaser::kStages);
        const double phaseInc = static_cast<double> (rateHz) / sampleRate;
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        const float dryGain = 1.0f - phaser::kWetMix;
        const float wetGain = phaser::kWetMix;

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = inL[n];
            const float dryR = inR != nullptr ? inR[n] : inL[n];

            const float phase = static_cast<float> (lfoPhase);
            const float modL = std::sin (phase * twoPi);
            const float modR = std::sin ((phase + phaser::kStereoOffsetCycles) * twoPi);

            const float wetL = runChannel (0, stages, dryL, modL);
            const float wetR = runChannel (1, stages, dryR, modR);

            outL[n] = dryL * dryGain + wetL * wetGain;
            outR[n] = dryR * dryGain + wetR * wetGain;

            lfoPhase += phaseInc;
            if (lfoPhase >= 1.0)
                lfoPhase -= std::floor (lfoPhase);
        }
    }

private:
    float onePoleCoeff (float hz) const noexcept
    {
        return 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                * hz / static_cast<float> (sampleRate));
    }

    /** One channel's all-pass cascade for a single sample. `mod` is the LFO
        value in [-1, 1]; `chan` selects the state bank. */
    float runChannel (int chan, int stages, float dry, float mod) noexcept
    {
        // LFO -> log-frequency -> all-pass coefficient. Depth scales the swing
        // about the geometric centre of the configured range.
        const float logF = logSweepMid + depth * logSweepHalfSpan * mod;
        float fc = std::exp (logF);
        fc = juce::jlimit (20.0f, 0.45f * static_cast<float> (sampleRate), fc);

        const float t = std::tan (juce::MathConstants<float>::pi
                                  * fc / static_cast<float> (sampleRate));
        const float a = (t - 1.0f) / (t + 1.0f);

        float& fb = fbState[static_cast<size_t> (chan)];
        float& z = hpZ[static_cast<size_t> (chan)];
        if (! std::isfinite (fb) || ! std::isfinite (z))
        {
            fb = 0.0f;
            z = 0.0f;
        }

        float x = dry + phaser::kFeedback * fb;

        auto& y = apY[static_cast<size_t> (chan)];
        auto& xz = apX[static_cast<size_t> (chan)];

        // First-order all-pass per stage: H(z) = (a + z^-1) / (1 + a z^-1).
        for (int s = 0; s < stages; ++s)
        {
            const size_t si = static_cast<size_t> (s);
            const float in = x;
            const float out = a * in + xz[si] - a * y[si];
            xz[si] = in;
            y[si] = out;
            x = out;
        }

        // High-pass the signal that re-enters the loop so the feedback cannot
        // pump the low end as the sweep crosses the bass.
        z += hpCoeff * (x - z);
        fb = x - z;

        return x;
    }

    double sampleRate = 44100.0;
    double lfoPhase = 0.0;

    float rateHz = phaser::kDefaultRateHz;
    float depth  = phaser::kDefaultDepthPct * 0.01f;

    float logSweepMin = 0.0f;
    float logSweepMax = 0.0f;
    float logSweepMid = 0.0f;
    float logSweepHalfSpan = 0.0f;

    float hpCoeff = 0.01f;
    std::array<float, 2> hpZ { { 0.0f, 0.0f } };
    std::array<float, 2> fbState { { 0.0f, 0.0f } };

    // [channel][stage]
    std::array<std::array<float, phaser::kMaxStages>, 2> apY {};
    std::array<std::array<float, phaser::kMaxStages>, 2> apX {};
};

} // namespace ee::dsp
