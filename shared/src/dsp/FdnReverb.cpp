#include "ee/dsp/FdnReverb.h"

#include <cmath>

namespace ee::dsp
{
namespace
{
    // Mutually non-harmonic lengths, so the modes of the network do not pile up.
    constexpr std::array<float, FdnReverb::kLines> kBaseDelayMs
        { 23.13f, 29.41f, 37.11f, 43.67f, 51.29f, 59.51f, 67.13f, 79.31f };

    constexpr std::array<float, FdnReverb::kLines> kLfoHz
        { 0.13f, 0.19f, 0.27f, 0.34f, 0.43f, 0.55f, 0.68f, 0.81f };

    constexpr std::array<float, 4> kDiffuserMs { 6.9f, 10.3f, 15.7f, 22.1f };

    constexpr float kMaxPredelayMs = 60.0f;
    constexpr float kDelayRampSeconds = 0.30f;

    // A lossless FDN retains more energy the longer it rings, so wet level would
    // otherwise rise ~9 dB across the decay sweep. Measured gain follows
    // (decay ^ 0.315) closely; normalising against the midpoint keeps the wet
    // signal put while the decay knob moves.
    constexpr float kGainExponent = 0.315f;
    constexpr float kGainReferenceSeconds = 2.0f;

    /** In-place 8-point Walsh-Hadamard transform, normalised to be lossless. */
    inline void hadamard8 (float* v) noexcept
    {
        for (int stride = 1; stride < 8; stride <<= 1)
        {
            for (int i = 0; i < 8; i += stride * 2)
            {
                for (int j = i; j < i + stride; ++j)
                {
                    const float a = v[j];
                    const float b = v[j + stride];
                    v[j] = a + b;
                    v[j + stride] = a - b;
                }
            }
        }

        constexpr float norm = 0.35355339059f; // 1 / sqrt(8)
        for (int i = 0; i < 8; ++i)
            v[i] *= norm;
    }
}

void FdnReverb::prepare (double sampleRate)
{
    sr = sampleRate;

    const float maxLineSeconds = (kBaseDelayMs.back() + 20.0f) * 0.001f;

    for (int i = 0; i < kLines; ++i)
    {
        lines[static_cast<size_t> (i)].prepare (sampleRate, maxLineSeconds);
        delaySmooth[static_cast<size_t> (i)].reset (sampleRate, kDelayRampSeconds);
        lfoPhase[static_cast<size_t> (i)] = static_cast<float> (i) / static_cast<float> (kLines);
    }

    for (size_t i = 0; i < diffusers.size(); ++i)
    {
        diffusers[i].prepare (sampleRate, (kDiffuserMs[i] + 5.0f) * 0.001f);
        // Held constant across the decay sweep; retuning these mid-sweep clicks.
        diffusers[i].setDelaySamples (static_cast<float> (kDiffuserMs[i] * 0.001 * sampleRate));
        diffusers[i].setCoefficient (0.62f);
    }

    predelayLine.prepare (sampleRate, (kMaxPredelayMs + 20.0f) * 0.001f);
    predelaySmooth.reset (sampleRate, kDelayRampSeconds);
    outputScale.reset (sampleRate, kDelayRampSeconds);

    dirty = true;
    updateDerived();
    outputScale.setCurrentAndTargetValue (outputScale.getTargetValue());

    for (int i = 0; i < kLines; ++i)
        delaySmooth[static_cast<size_t> (i)].setCurrentAndTargetValue (delaySmooth[static_cast<size_t> (i)].getTargetValue());

    predelaySmooth.setCurrentAndTargetValue (predelaySmooth.getTargetValue());
}

void FdnReverb::reset()
{
    for (auto& l : lines)     l.reset();
    for (auto& d : dampers)   d.reset();
    for (auto& a : diffusers) a.reset();
    predelayLine.reset();
}

void FdnReverb::setDecayTime (float seconds) noexcept
{
    seconds = juce::jlimit (kMinDecay, kMaxDecay, seconds);
    if (! juce::approximatelyEqual (seconds, decaySeconds))
    {
        decaySeconds = seconds;
        dirty = true;
    }
}

void FdnReverb::setModulation (float amount01) noexcept
{
    amount01 = juce::jlimit (0.0f, 1.0f, amount01);
    if (! juce::approximatelyEqual (amount01, modAmount))
    {
        modAmount = amount01;
        dirty = true;
    }
}

void FdnReverb::setDecayTilt (float lowMultiplier, float highMultiplier) noexcept
{
    lowMult = juce::jlimit (0.1f, 4.0f, lowMultiplier);
    highMult = juce::jlimit (0.1f, 4.0f, highMultiplier);
    dirty = true;
}

void FdnReverb::updateDerived() noexcept
{
    dirty = false;

    const float norm = juce::jlimit (0.0f, 1.0f, (decaySeconds - kMinDecay) / (kMaxDecay - kMinDecay));

    // The two hidden parameters the decay knob drives.
    const float roomSize   = 0.30f + 0.70f * std::sqrt (norm);
    const float predelayMs = 8.0f + (kMaxPredelayMs - 8.0f) * std::pow (norm, 0.7f);

    predelaySmooth.setTargetValue (static_cast<float> (predelayMs * 0.001 * sr));
    outputScale.setTargetValue (std::pow (kGainReferenceSeconds / decaySeconds, kGainExponent));

    // Scaled off 44.1k so the modulation depth is the same musical amount at any rate.
    modDepthSamples = static_cast<float> ((0.8 + 11.0 * modAmount) * sr / 44100.0);

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const float samples = static_cast<float> (kBaseDelayMs[idx] * roomSize * 0.001 * sr);

        delaySmooth[idx].setTargetValue (samples);
        lfoInc[idx] = static_cast<float> (kLfoHz[idx] / sr);

        const float t = samples / static_cast<float> (sr);
        const float gLow  = std::pow (10.0f, -3.0f * t / (decaySeconds * lowMult));
        const float gHigh = std::pow (10.0f, -3.0f * t / (decaySeconds * highMult));

        dampers[idx].set (juce::jmin (gLow, 0.9995f), gHigh / juce::jmax (gLow, 1.0e-6f));
    }
}

void FdnReverb::process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept
{
    if (dirty)
        updateDerived();

    for (int s = 0; s < numSamples; ++s)
    {
        predelayLine.write (monoIn[s]);
        const float predelayed = predelayLine.read (predelaySmooth.getNextValue());
        predelayLine.advance();

        float diffused = predelayed;
        for (auto& ap : diffusers)
            diffused = ap.process (diffused);

        std::array<float, kLines> v {};

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const float mod = std::sin (lfoPhase[idx] * juce::MathConstants<float>::twoPi) * modDepthSamples;
            lfoPhase[idx] += lfoInc[idx];
            if (lfoPhase[idx] >= 1.0f)
                lfoPhase[idx] -= 1.0f;

            v[idx] = dampers[idx].process (lines[idx].read (delaySmooth[idx].getNextValue() + mod));
        }

        // Decorrelated taps: each side reads a different subset of the network.
        const float scale = outputScale.getNextValue() * 0.5f;
        outL[s] = (v[0] + v[2] - v[5] - v[7]) * scale;
        outR[s] = (v[1] + v[3] - v[4] - v[6]) * scale;

        hadamard8 (v.data());

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);
            const float inject = (i % 2 == 0 ? 0.5f : -0.5f) * diffused;
            lines[idx].write (v[idx] + inject);
            lines[idx].advance();
        }
    }
}

} // namespace ee::dsp
