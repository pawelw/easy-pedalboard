#include "ee/dsp/FdnReverb.h"

#include <cmath>

namespace ee::dsp
{
namespace
{
    // Mutually non-harmonic lengths, so the modes of the network do not pile up.
    constexpr std::array<float, FdnReverb::kLines> kBaseDelayMs
        { 10.37f, 12.89f, 15.61f, 18.43f, 21.29f, 24.17f, 27.31f, 30.53f,
          33.79f, 37.21f, 40.87f, 44.63f, 48.59f, 53.17f, 58.31f, 64.27f };

    constexpr std::array<float, FdnReverb::kLines> kLfoHz
        { 0.09f, 0.13f, 0.17f, 0.21f, 0.26f, 0.31f, 0.37f, 0.43f,
          0.49f, 0.55f, 0.61f, 0.67f, 0.73f, 0.79f, 0.85f, 0.91f };

    // One allpass per line, sitting inside the feedback loop. Lengths are kept
    // clear of the line lengths so the pair does not lock into a common period.
    constexpr std::array<float, FdnReverb::kLines> kTankMs
        { 3.11f, 3.89f, 4.51f, 5.23f, 5.87f, 6.53f, 7.19f, 7.83f,
          8.41f, 9.07f, 9.73f, 10.31f, 10.97f, 11.59f, 12.23f, 12.89f };

    // Dattorro-style input ladder: short pairs first to build density fast, then
    // longer pairs to stretch it out. Coefficients taper hard towards the end,
    // because the ladder sits outside the feedback loop and its own ring is what
    // stretches the shortest decay settings past where the knob says.
    constexpr std::array<float, FdnReverb::kDiffusers> kDiffuserMs
        { 2.31f, 3.59f, 5.77f, 7.11f, 8.93f, 10.71f, 12.29f, 13.97f };
    constexpr std::array<float, FdnReverb::kDiffusers> kDiffuserWeight
        { 1.06f, 1.06f, 1.00f, 1.00f, 0.92f, 0.92f, 0.84f, 0.84f };

    // Outside the feedback loop, so these change phase and stereo width without
    // touching the decay at all. Kept moderate: the cross-feed below sets the
    // correlation at every frequency, so these only need to add phase variety,
    // and long ones ring on past the network at short decay settings.
    constexpr std::array<float, FdnReverb::kDecorrelators> kSpreadLeftMs  { 9.31f, 15.73f, 23.11f };
    constexpr std::array<float, FdnReverb::kDecorrelators> kSpreadRightMs { 11.47f, 19.31f, 28.73f };

    constexpr float kDelayRampSeconds = 0.30f;

    // Injecting the source into every line at +/-c puts kLines * c^2 units of
    // energy into the network; hold that constant regardless of line count.
    const float kInjectGain = std::sqrt (2.0f / static_cast<float> (FdnReverb::kLines));

    // Summing kLines uncorrelated taps grows the amplitude by sqrt(kLines).
    constexpr float kTapNorm = 1.0f / 4.0f;

    /** Cross-feed that turns two orthogonal channels into a given correlation.
        corr = 2k / (1 + k^2), solved for k. Negative correlations are allowed
        and are what the wet path actually wants; see ReverbConfig.
    */
    inline float crossFeedFor (float correlation) noexcept
    {
        const float c = juce::jlimit (-0.9f, 0.9f, correlation);
        if (std::abs (c) < 1.0e-4f)
            return 0.0f;
        return (1.0f - std::sqrt (1.0f - c * c)) / c;
    }

    inline float highCutCoeffFor (float hz, double sampleRate) noexcept
    {
        if (hz >= FdnReverb::kMaxHighCutHz)
            return 1.0f; // exact pass-through, so "off" really is off
        const float w = 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float> (sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    // Damping the low and high bands takes broadband energy out of the tail, so
    // a -60 dB measurement lands short of the nominal time. Empirical fit against
    // the offline harness; retune it there if the voicing above changes a lot.
    constexpr float kDecayCompensation = 1.40f;

    // A lossless FDN retains more energy the longer it rings, so wet level would
    // otherwise rise ~9 dB across the decay sweep. Measured gain follows
    // (decay ^ kGainExponent) closely; normalising against the midpoint keeps
    // the wet signal put while the decay knob moves.
    constexpr float kGainExponent = 0.41f;
    constexpr float kGainReferenceSeconds = 2.0f;

    /** In-place 16-point Walsh-Hadamard transform, normalised to be lossless. */
    inline void hadamard16 (float* v) noexcept
    {
        for (int stride = 1; stride < 16; stride <<= 1)
        {
            for (int i = 0; i < 16; i += stride * 2)
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

        constexpr float norm = 0.25f; // 1 / sqrt(16)
        for (int i = 0; i < 16; ++i)
            v[i] *= norm;
    }

    /** Walsh rows 5 and 10: orthogonal, so the two output taps are uncorrelated. */
    constexpr float tapSign (int line, int mask) noexcept
    {
        int bits = line & mask;
        int parity = 0;
        while (bits != 0)
        {
            parity ^= (bits & 1);
            bits >>= 1;
        }
        return parity == 0 ? 1.0f : -1.0f;
    }
}

void FdnReverb::prepare (double sampleRate)
{
    sr = sampleRate;

    const float maxLineSeconds = (kBaseDelayMs.back() + 20.0f) * 0.001f;

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        lines[idx].prepare (sampleRate, maxLineSeconds);
        dampers[idx].prepare (sampleRate, config::kLowCornerHz, config::kHighCornerHz);
        tank[idx].prepare (sampleRate, (kTankMs[idx] + 5.0f) * 0.001f);
        tank[idx].setDelaySamples (static_cast<float> (kTankMs[idx] * 0.001 * sampleRate));
        tank[idx].setCoefficient (juce::jlimit (0.0f, 0.85f, config::kTankDiffusion));
        delaySmooth[idx].reset (sampleRate, kDelayRampSeconds);
        lfoPhase[idx] = static_cast<float> (i) / static_cast<float> (kLines);
    }

    for (size_t i = 0; i < diffusers.size(); ++i)
    {
        diffusers[i].prepare (sampleRate, (kDiffuserMs[i] + 5.0f) * 0.001f);
        // Held constant across the decay sweep; retuning these mid-sweep clicks.
        diffusers[i].setDelaySamples (static_cast<float> (kDiffuserMs[i] * 0.001 * sampleRate));
        diffusers[i].setCoefficient (juce::jlimit (0.05f, 0.85f, config::kDiffusion * kDiffuserWeight[i]));
    }

    for (size_t i = 0; i < spreadL.size(); ++i)
    {
        const float spread = juce::jlimit (0.0f, 0.85f, config::kStereoSpread);

        spreadL[i].prepare (sampleRate, (kSpreadLeftMs[i] + 5.0f) * 0.001f);
        spreadL[i].setDelaySamples (static_cast<float> (kSpreadLeftMs[i] * 0.001 * sampleRate));
        spreadL[i].setCoefficient (spread);

        spreadR[i].prepare (sampleRate, (kSpreadRightMs[i] + 5.0f) * 0.001f);
        spreadR[i].setDelaySamples (static_cast<float> (kSpreadRightMs[i] * 0.001 * sampleRate));
        spreadR[i].setCoefficient (-spread);
    }

    predelayLine.prepare (sampleRate, (config::kPredelayMaxMs + 20.0f) * 0.001f);
    predelaySmooth.reset (sampleRate, kDelayRampSeconds);
    outputScale.reset (sampleRate, kDelayRampSeconds);
    highCutCoeff.reset (sampleRate, 0.05f);
    highCutCoeff.setCurrentAndTargetValue (highCutCoeffFor (highCutHz, sampleRate));
    highCutState.fill (0.0f);

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
    for (auto& a : tank)      a.reset();
    for (auto& a : diffusers) a.reset();
    for (auto& a : spreadL)   a.reset();
    for (auto& a : spreadR)   a.reset();
    predelayLine.reset();
    highCutState.fill (0.0f);
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

void FdnReverb::setHighCut (float hz) noexcept
{
    highCutHz = juce::jlimit (kMinHighCutHz, kMaxHighCutHz, hz);
    highCutCoeff.setTargetValue (highCutCoeffFor (highCutHz, sr));
}

void FdnReverb::setDecayTilt (float low, float high) noexcept
{
    lowRatio = juce::jlimit (0.05f, 1.0f, low);
    highRatio = juce::jlimit (0.05f, 1.0f, high);
    dirty = true;
}

void FdnReverb::updateDerived() noexcept
{
    dirty = false;

    const float norm = juce::jlimit (0.0f, 1.0f, (decaySeconds - kMinDecay) / (kMaxDecay - kMinDecay));

    // The two hidden parameters the decay knob drives. Room size stays in a
    // narrow band because a plate is dense at every setting; shrinking the
    // lines much further would push the modes up into audible pitches.
    const float roomSize = 0.55f + 0.45f * std::sqrt (norm);
    const float predelayMs = config::kPredelayMinMs
                             + (config::kPredelayMaxMs - config::kPredelayMinMs) * std::pow (norm, 0.7f);

    predelaySmooth.setTargetValue (static_cast<float> (predelayMs * 0.001 * sr));
    outputScale.setTargetValue (std::pow (kGainReferenceSeconds / decaySeconds, kGainExponent));

    // Scaled off 44.1k so the modulation depth is the same musical amount at any rate.
    modDepthSamples = static_cast<float> ((config::kModFloorSamples + config::kModDepthSamples * modAmount) * sr / 44100.0);

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const float samples = static_cast<float> (kBaseDelayMs[idx] * roomSize * 0.001 * sr);

        delaySmooth[idx].setTargetValue (samples);
        lfoInc[idx] = static_cast<float> (kLfoHz[idx] / sr);

        // Per-trip loss for a -60 dB decay over decaySeconds, then the two
        // bands expressed relative to it. The tank allpass is part of the trip,
        // so its delay counts towards the time round the loop.
        const float t = (samples + static_cast<float> (kTankMs[idx] * 0.001 * sr)) / static_cast<float> (sr);
        const float target = decaySeconds * kDecayCompensation;
        const float gMid  = juce::jmin (std::pow (10.0f, -3.0f * t / target), 0.9995f);
        const float gLow  = std::pow (10.0f, -3.0f * t / (target * lowRatio));
        const float gHigh = std::pow (10.0f, -3.0f * t / (target * highRatio));

        dampers[idx].set (gMid,
                          gLow / juce::jmax (gMid, 1.0e-6f),
                          gHigh / juce::jmax (gMid, 1.0e-6f));
    }
}

void FdnReverb::process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept
{
    if (dirty)
        updateDerived();

    const float crossFeed = crossFeedFor (config::kTargetCorrelation);
    const float crossNorm = 1.0f / std::sqrt (1.0f + crossFeed * crossFeed);

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

            v[idx] = dampers[idx].process (tank[idx].process (lines[idx].read (delaySmooth[idx].getNextValue() + mod)));
        }

        float sumL = 0.0f;
        float sumR = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float x = v[static_cast<size_t> (i)];
            sumL += tapSign (i, 0b0101) * x;
            sumR += tapSign (i, 0b1010) * x;
        }

        const float scale = outputScale.getNextValue() * kTapNorm;

        float l = sumL * scale;
        float r = sumR * scale;
        for (int i = 0; i < kDecorrelators; ++i)
        {
            l = spreadL[static_cast<size_t> (i)].process (l);
            r = spreadR[static_cast<size_t> (i)].process (r);
        }

        outL[s] = (l + crossFeed * r) * crossNorm;
        outR[s] = (r + crossFeed * l) * crossNorm;

        // Two cascaded one-poles, so the cut has a slope rather than a corner.
        const float hc = highCutCoeff.getNextValue();
        if (hc < 0.999f)
        {
            highCutState[0] += hc * (outL[s] - highCutState[0]);
            highCutState[1] += hc * (highCutState[0] - highCutState[1]);
            outL[s] = highCutState[1];

            highCutState[2] += hc * (outR[s] - highCutState[2]);
            highCutState[3] += hc * (highCutState[2] - highCutState[3]);
            outR[s] = highCutState[3];
        }

        hadamard16 (v.data());

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);
            const float inject = tapSign (i, 0b0110) * kInjectGain * diffused;
            lines[idx].write (v[idx] + inject);
            lines[idx].advance();
        }
    }
}

} // namespace ee::dsp
