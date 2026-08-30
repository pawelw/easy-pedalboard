#include "ee/dsp/FdnReverb.h"

#include <cmath>

#include "Effects/pitchshifter.h"

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

    // Second stage, kept clear of the first so the pair does not comb.
    constexpr std::array<float, FdnReverb::kLines> kTank2Ms
        { 1.73f, 2.11f, 2.53f, 2.89f, 3.31f, 3.67f, 4.09f, 4.43f,
          4.87f, 5.21f, 5.59f, 5.93f, 6.37f, 6.71f, 7.13f, 7.49f };

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

    inline float onePoleCoeff (float hz, double sampleRate) noexcept
    {
        const float w = 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float> (sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    inline float lowCutCoeffFor (float hz, double sampleRate) noexcept
    {
        if (hz <= FdnReverb::kMinLowCutHz)
            return 0.0f; // exact pass-through, so "off" really is off
        return onePoleCoeff (hz, sampleRate);
    }

    /** Knob amount to shimmer feedback gain, tapered so the musical low end of
        the range is not crammed into the first part of the travel. */
    inline float shimmerGainFor (float amount01, float skew, float maxFeedback) noexcept
    {
        const float a = juce::jlimit (0.0f, 1.0f, amount01);
        return std::pow (a, skew) * maxFeedback;
    }

    /** One side of the shimmer feedback shaping: band limit, kill the sub the
        shifter's tracking error builds up, a low shelf for body and a high
        shelf for sparkle, then soft clip so no knob setting can let the octave
        stack run away. */
    inline float shapeShimmerSide (float x, float bass, float sparkle,
                                   float hiCutCoeff, float& hiCutState,
                                   float loCutCoeff, float& loCutState,
                                   float bassCoeff, float& bassState,
                                   float shelfCoeff, float& shelfState) noexcept
    {
        hiCutState += hiCutCoeff * (x - hiCutState);
        x = hiCutState;
        loCutState += loCutCoeff * (x - loCutState);
        x -= loCutState;
        bassState += bassCoeff * (x - bassState);
        x += bass * bassState;               // low shelf: adds the low-passed part
        shelfState += shelfCoeff * (x - shelfState);
        x += sparkle * (x - shelfState);     // high shelf: adds the complement
        return std::tanh (x);
    }

    /** Diffusion is only thinned once the top half of the sweep is reached. */
    inline float tankScaleFor (float resonance) noexcept
    {
        const float top = juce::jlimit (0.0f, 1.0f, (resonance - 0.5f) * 2.0f);
        return 1.0f - config::kResonanceDiffusionDrop * top;
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

// Out of line, where daisysp::PitchShifter is a complete type for unique_ptr.
FdnReverb::FdnReverb() = default;
FdnReverb::~FdnReverb() = default;

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
        tank[idx].setCoefficient (config::kTankDiffusion * tankScaleFor (resonance));

        tank2[idx].prepare (sampleRate, (kTank2Ms[idx] + 5.0f) * 0.001f);
        tank2[idx].setDelaySamples (static_cast<float> (kTank2Ms[idx] * 0.001 * sampleRate));
        tank2[idx].setCoefficient (config::kTankStage2 * tankScaleFor (resonance));
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
    lowCutCoeff.reset (sampleRate, 0.05f);
    lowCutCoeff.setCurrentAndTargetValue (lowCutCoeffFor (lowCutHz, sampleRate));
    lowCutState.fill (0.0f);

    wetLowShelfCoeff = onePoleCoeff (config::kWetLowShelfHz, sampleRate);
    wetLowShelfState.fill (0.0f);

    if (shimmerShifterL == nullptr)
        shimmerShifterL = std::make_unique<daisysp::PitchShifter>();
    if (shimmerShifterR == nullptr)
        shimmerShifterR = std::make_unique<daisysp::PitchShifter>();
    for (auto* shifter : { shimmerShifterL.get(), shimmerShifterR.get() })
        shifter->Init (static_cast<float> (sampleRate));

    shimmerGain.reset (sampleRate, kDelayRampSeconds);
    shimmerLowCutStateL = shimmerLowCutStateR = 0.0f;
    shimmerHighCutStateL = shimmerHighCutStateR = 0.0f;
    shimmerBassStateL = shimmerBassStateR = 0.0f;
    shimmerShelfStateL = shimmerShelfStateR = 0.0f;
    shimmerFeedbackL = shimmerFeedbackR = 0.0f;

    // Sized for the tuning panel's ceiling, not the current predelay, so the
    // panel can push it up without the delay line clamping.
    shimmerPredelay.prepare (sampleRate, (kShimmerPredelayCeilingMs + 40.0f) * 0.001f);
    shimmerPredelaySmooth.reset (sampleRate, kDelayRampSeconds);

    updateShimmerDerived();
    shimmerGain.setCurrentAndTargetValue (shimmerGain.getTargetValue());

    dirty = true;
    updateDerived();
    outputScale.setCurrentAndTargetValue (outputScale.getTargetValue());
    shimmerPredelaySmooth.setCurrentAndTargetValue (shimmerPredelaySmooth.getTargetValue());

    for (int i = 0; i < kLines; ++i)
        delaySmooth[static_cast<size_t> (i)].setCurrentAndTargetValue (delaySmooth[static_cast<size_t> (i)].getTargetValue());

    predelaySmooth.setCurrentAndTargetValue (predelaySmooth.getTargetValue());
}

void FdnReverb::reset()
{
    for (auto& l : lines)     l.reset();
    for (auto& d : dampers)   d.reset();
    for (auto& a : tank)      a.reset();
    for (auto& a : tank2)     a.reset();
    for (auto& a : diffusers) a.reset();
    for (auto& a : spreadL)   a.reset();
    for (auto& a : spreadR)   a.reset();
    predelayLine.reset();
    lowCutState.fill (0.0f);
    wetLowShelfState.fill (0.0f);

    if (shimmerShifterL != nullptr)
    {
        // No buffer-clear on its own, so re-init and restore the voicing.
        for (auto* shifter : { shimmerShifterL.get(), shimmerShifterR.get() })
            shifter->Init (static_cast<float> (sr));
        updateShimmerDerived();
    }
    shimmerPredelay.reset();
    shimmerFeedbackL = shimmerFeedbackR = 0.0f;
    shimmerLowCutStateL = shimmerLowCutStateR = 0.0f;
    shimmerHighCutStateL = shimmerHighCutStateR = 0.0f;
    shimmerBassStateL = shimmerBassStateR = 0.0f;
    shimmerShelfStateL = shimmerShelfStateR = 0.0f;
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


void FdnReverb::setLowCut (float hz) noexcept
{
    lowCutHz = juce::jlimit (kMinLowCutHz, kMaxLowCutHz, hz);
    lowCutCoeff.setTargetValue (lowCutCoeffFor (lowCutHz, sr));
}

void FdnReverb::setResonance (float amount01) noexcept
{
    amount01 = juce::jlimit (0.0f, 1.0f, amount01);
    if (! juce::approximatelyEqual (amount01, resonance))
    {
        resonance = amount01;

        const float thin = tankScaleFor (resonance);
        for (auto& ap : tank)
            ap.setCoefficient (config::kTankDiffusion * thin);
        for (auto& ap : tank2)
            ap.setCoefficient (config::kTankStage2 * thin);

        dirty = true;
    }
}

void FdnReverb::setDecayTilt (float low, float high) noexcept
{
    lowRatio = juce::jlimit (0.05f, 1.0f, low);
    highRatio = juce::jlimit (0.05f, 1.0f, high);
    dirty = true;
}

void FdnReverb::setShimmer (float amount01) noexcept
{
    amount01 = juce::jlimit (0.0f, 1.0f, amount01);
    if (juce::approximatelyEqual (amount01, shimmerAmount))
        return;

    shimmerAmount = amount01;
    shimmerGain.setTargetValue (
        shimmerGainFor (amount01, shimmerTuning.skew, shimmerTuning.maxFeedback));
}

void FdnReverb::updateShimmerDerived() noexcept
{
    shimmerLowCutCoeff  = onePoleCoeff (shimmerTuning.lowCutHz,  sr);
    shimmerHighCutCoeff = onePoleCoeff (shimmerTuning.highCutHz, sr);
    shimmerBassCoeff    = onePoleCoeff (shimmerTuning.bassHz,    sr);
    shimmerShelfCoeff   = onePoleCoeff (shimmerTuning.sparkleHz, sr);
    shimmerHaasSamples  = static_cast<float> (shimmerTuning.haasMs * 0.001 * sr);

    if (shimmerShifterL != nullptr)
    {
        for (auto* shifter : { shimmerShifterL.get(), shimmerShifterR.get() })
            shifter->SetFun (shimmerTuning.flutter);
        shimmerShifterL->SetTransposition (shimmerTuning.semitones - shimmerTuning.detuneSemis);
        shimmerShifterR->SetTransposition (shimmerTuning.semitones + shimmerTuning.detuneSemis);
    }

    shimmerGain.setTargetValue (
        shimmerGainFor (shimmerAmount, shimmerTuning.skew, shimmerTuning.maxFeedback));
}

void FdnReverb::setShimmerTuning (const ShimmerTuning& newTuning) noexcept
{
    shimmerTuning = newTuning;
    updateShimmerDerived();
    dirty = true;   // the predelay range is applied in updateDerived()
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

    // The octave feedback blooms further behind the note as the room grows.
    const float shimmerPreMs = shimmerTuning.predelayMinMs
        + (shimmerTuning.predelayMaxMs - shimmerTuning.predelayMinMs) * std::pow (norm, 0.7f);
    shimmerPredelaySmooth.setTargetValue (static_cast<float> (shimmerPreMs * 0.001 * sr));

    // Scaled off 44.1k so the modulation depth is the same musical amount at any rate.
    modDepthSamples = static_cast<float> (config::kModDepthSamples * (1.0f - resonance) * sr / 44100.0);

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const float samples = static_cast<float> (kBaseDelayMs[idx] * roomSize * 0.001 * sr);

        delaySmooth[idx].setTargetValue (samples);
        lfoInc[idx] = static_cast<float> (kLfoHz[idx] / sr);

        // Per-trip loss for a -60 dB decay over decaySeconds, then the two
        // bands expressed relative to it. The tank allpass is part of the trip,
        // so its delay counts towards the time round the loop.
        const float tankSamples = static_cast<float> ((kTankMs[idx] + kTank2Ms[idx]) * 0.001 * sr);
        const float t = (samples + tankSamples) / static_cast<float> (sr);
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

        // Skip the whole shimmer path while the knob sits at zero, so the plain
        // reverb is bit-for-bit what it was before shimmer existed. Stays true
        // through the gain ramp until the smoothed value lands exactly on 0.
        const bool shimmerActive = shimmerGain.getCurrentValue() > 1.0e-6f
                                || shimmerGain.getTargetValue()  > 1.0e-6f;

        // The shaped octave from the previous sample, one value per side, ready
        // to be injected into the network below with the L/R tap patterns.
        float shimmerInjectL = 0.0f;
        float shimmerInjectR = 0.0f;
        if (shimmerActive)
        {
            const float g = shimmerGain.getNextValue();
            shimmerInjectL = g * shimmerFeedbackL;
            shimmerInjectR = g * shimmerFeedbackR;
        }

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

            v[idx] = dampers[idx].process (
                tank2[idx].process (
                    tank[idx].process (lines[idx].read (delaySmooth[idx].getNextValue() + mod))));
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

        // Feed a mono tap of the raw network output through the shimmer
        // predelay, then to the two shifters a Haas offset apart, shape each
        // side, and hold both for the next sample's injection above.
        if (shimmerActive)
        {
            const float wetMono = (sumL + sumR) * 0.5f * scale;
            shimmerPredelay.write (wetMono);

            const float preDelaySamples = shimmerPredelaySmooth.getNextValue();
            float preL = shimmerPredelay.read (preDelaySamples);
            float preR = shimmerPredelay.read (preDelaySamples + shimmerHaasSamples);
            shimmerPredelay.advance();

            float shiftedL = shimmerShifterL->Process (preL);
            float shiftedR = shimmerShifterR->Process (preR);

            shimmerFeedbackL = shapeShimmerSide (shiftedL, shimmerTuning.bass, shimmerTuning.sparkle,
                                                 shimmerHighCutCoeff, shimmerHighCutStateL,
                                                 shimmerLowCutCoeff,  shimmerLowCutStateL,
                                                 shimmerBassCoeff,    shimmerBassStateL,
                                                 shimmerShelfCoeff,   shimmerShelfStateL);
            shimmerFeedbackR = shapeShimmerSide (shiftedR, shimmerTuning.bass, shimmerTuning.sparkle,
                                                 shimmerHighCutCoeff, shimmerHighCutStateR,
                                                 shimmerLowCutCoeff,  shimmerLowCutStateR,
                                                 shimmerBassCoeff,    shimmerBassStateR,
                                                 shimmerShelfCoeff,   shimmerShelfStateR);
        }
        else
        {
            shimmerFeedbackL = shimmerFeedbackR = 0.0f;
        }

        float l = sumL * scale;
        float r = sumR * scale;
        for (int i = 0; i < kDecorrelators; ++i)
        {
            l = spreadL[static_cast<size_t> (i)].process (l);
            r = spreadR[static_cast<size_t> (i)].process (r);
        }

        outL[s] = (l + crossFeed * r) * crossNorm;
        outR[s] = (r + crossFeed * l) * crossNorm;

        // Two one-pole highpasses in series, for a real 12 dB/oct slope.
        const float lc = lowCutCoeff.getNextValue();
        if (lc > 0.0f)
        {
            lowCutState[0] += lc * (outL[s] - lowCutState[0]);
            const float hpL = outL[s] - lowCutState[0];
            lowCutState[1] += lc * (hpL - lowCutState[1]);
            outL[s] = hpL - lowCutState[1];

            lowCutState[2] += lc * (outR[s] - lowCutState[2]);
            const float hpR = outR[s] - lowCutState[2];
            lowCutState[3] += lc * (hpR - lowCutState[3]);
            outR[s] = hpR - lowCutState[3];
        }

        // Widening is done last so it acts on the finished wet image.
        const float mid = 0.5f * (outL[s] + outR[s]);
        const float side = 0.5f * (outL[s] - outR[s]) * config::kStereoWidth;
        outL[s] = mid + side;
        outR[s] = mid - side;

        // Low shelf for body, on the finished output only - never fed back.
        if (config::kWetLowShelf > 0.0f)
        {
            wetLowShelfState[0] += wetLowShelfCoeff * (outL[s] - wetLowShelfState[0]);
            outL[s] += config::kWetLowShelf * wetLowShelfState[0];
            wetLowShelfState[1] += wetLowShelfCoeff * (outR[s] - wetLowShelfState[1]);
            outR[s] += config::kWetLowShelf * wetLowShelfState[1];
        }

        hadamard16 (v.data());

        // Shimmer re-enters as a correlated centre plus a scaled L/R
        // difference: the centre rides the dry tap pattern, the difference goes
        // in on the orthogonal output patterns so it opens up inside the tank
        // without gutting a mono sum.
        const float shimmerMono  = 0.5f * (shimmerInjectL + shimmerInjectR);
        const float shimmerSideL = (shimmerInjectL - shimmerMono) * shimmerTuning.width;
        const float shimmerSideR = (shimmerInjectR - shimmerMono) * shimmerTuning.width;

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);

            const float inject = tapSign (i, 0b0110) * kInjectGain * diffused;
            const float shimmerInject = kInjectGain
                * (tapSign (i, 0b0110) * shimmerMono
                 + tapSign (i, 0b0101) * shimmerSideL
                 + tapSign (i, 0b1010) * shimmerSideR);

            lines[idx].write (v[idx] + inject + shimmerInject);
            lines[idx].advance();
        }
    }
}

} // namespace ee::dsp
