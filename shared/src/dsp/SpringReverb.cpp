#include "ee/dsp/SpringReverb.h"

#include <cmath>

namespace ee::dsp
{
namespace
{
constexpr float kPi = 3.14159265358979f;

/** One-pole lowpass coefficient for a corner frequency. */
float onePoleCoeff (float cornerHz, double sampleRate) noexcept
{
    const float w = 2.0f * kPi * cornerHz / static_cast<float> (sampleRate);
    return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
}
} // namespace

void SpringReverb::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;

    inputHighCutCoeff = onePoleCoeff (spring::kInputHighCutHz, sr);
    inputLowCutCoeff = onePoleCoeff (spring::kInputLowCutHz, sr);
    outputHighCutCoeff = onePoleCoeff (spring::kOutputHighCutHz, sr);
    outputLowCutCoeff = onePoleCoeff (spring::kOutputLowCutHz, sr);
    wetShelfCoeff = onePoleCoeff (spring::kWetLowShelfHz, sr);

    const float chirpNominal = spring::kChirpDelayMs * 0.001f * static_cast<float> (sr);

    for (int t = 0; t < kTanks; ++t)
    {
        const float tankRatio = t == 0 ? 1.0f : spring::kRightTankRatio;

        for (int i = 0; i < spring::kSprings; ++i)
        {
            auto& s = tanks[static_cast<size_t> (t)][static_cast<size_t> (i)];

            s.chirp.prepare (chirpNominal, spring::kChirpSpread, spring::kChirpCoefficient);
            s.damper.prepare (sr, spring::kLowCornerHz, spring::kHighCornerHz);

            // The voicing quotes the whole round trip, so the delay line only
            // has to carry whatever the dispersion chain does not.
            const float roundTrip = spring::kSpringMs[i] * tankRatio * 0.001f * static_cast<float> (sr);
            const float chirpDelay = static_cast<float> (s.chirp.meanDelaySamples());

            s.delaySamples = std::max (2.0f, roundTrip - chirpDelay);
            s.loopSeconds = (s.delaySamples + chirpDelay) / static_cast<float> (sr);
            s.modDepthSamples = s.delaySamples * spring::kModDepth;

            // Leave room for the modulation and a little slack on top.
            s.line.prepare (sr, (s.delaySamples * 1.05f + 8.0f) / static_cast<float> (sr));

            // Every spring wanders at its own rate, and the two tanks start out
            // of step, or the modulation reads as one shared vibrato.
            s.lfoInc = spring::kModRateHz * std::pow (1.0f + spring::kModRateSpread, static_cast<float> (i))
                       / static_cast<float> (sr);
            s.lfoPhase = 0.17f * static_cast<float> (i) + 0.41f * static_cast<float> (t);
        }
    }

    // Springs sum incoherently, so the tank stays at roughly one spring's level
    // however many are strung in it.
    outputScale = spring::kWetTrim / std::sqrt (static_cast<float> (spring::kSprings));

    updateFeedback();
    reset();
}

void SpringReverb::reset() noexcept
{
    for (auto& tank : tanks)
        for (auto& s : tank)
        {
            s.line.reset();
            s.chirp.reset();
            s.damper.reset();
        }

    inputHighCutState = 0.0f;
    inputLowCutState = 0.0f;
    outputHighCutState.fill (0.0f);
    outputLowCutState.fill (0.0f);
    wetShelfState.fill (0.0f);
}

void SpringReverb::setDecayTime (float seconds) noexcept
{
    const float clamped = std::clamp (seconds, kMinDecay, kMaxDecay);
    if (clamped == decaySeconds)
        return;

    decaySeconds = clamped;
    updateFeedback();
}

void SpringReverb::updateFeedback() noexcept
{
    for (auto& tank : tanks)
        for (auto& s : tank)
        {
            // -60 dB after `decaySeconds`, spread over however many trips round
            // this particular spring fit into that time.
            const float g = std::pow (10.0f, -3.0f * s.loopSeconds
                                                / (decaySeconds * spring::kDecayScale));
            s.damper.set (std::min (g, spring::kMaxLoopGain),
                          spring::kLowDecayRatio, spring::kHighDecayRatio);
        }
}

float SpringReverb::driveFilter (float x) noexcept
{
    // The transducer: a band-pass, built as a lowpass and the complement of a
    // second lowpass, so the springs are shaken with the same narrow slice of
    // the signal a real tank gets.
    inputHighCutState += inputHighCutCoeff * (x - inputHighCutState);
    float y = inputHighCutState;

    inputLowCutState += inputLowCutCoeff * (y - inputLowCutState);
    return y - inputLowCutState;
}

float SpringReverb::outputFilter (float x, int tank) noexcept
{
    const size_t t = static_cast<size_t> (tank);

    outputHighCutState[t] += outputHighCutCoeff * (x - outputHighCutState[t]);
    float y = outputHighCutState[t];

    outputLowCutState[t] += outputLowCutCoeff * (y - outputLowCutState[t]);
    y -= outputLowCutState[t];

    // Low shelf for body - level only, no effect on how fast the low end dies.
    wetShelfState[t] += wetShelfCoeff * (y - wetShelfState[t]);
    return y + (spring::kWetLowShelfGain - 1.0f) * wetShelfState[t];
}

void SpringReverb::process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        // A single non-finite sample from upstream would latch into the delay
        // lines and re-circulate forever - a roar that only a reset clears.
        // Scrub the input on the way in, and bail to a full reset below if the
        // tank goes non-finite anyway.
        const float in = std::isfinite (monoIn[n]) ? monoIn[n] : 0.0f;
        const float drive = driveFilter (in);

        float wet[kTanks] = { 0.0f, 0.0f };

        // Mono runs the left tank alone and sends it to both outputs, which is
        // what a real one-tank spring reverb does.
        const int liveTanks = stereo ? kTanks : 1;

        for (int t = 0; t < liveTanks; ++t)
        {
            for (auto& s : tanks[static_cast<size_t> (t)])
            {
                // Springs are never perfectly still; a fraction of a per cent of
                // wander keeps the tank's comb from ringing on fixed pitches.
                const float mod = s.modDepthSamples * std::sin (2.0f * kPi * s.lfoPhase);
                s.lfoPhase += s.lfoInc;
                if (s.lfoPhase >= 1.0f)
                    s.lfoPhase -= 1.0f;

                const float delayed = s.line.read (s.delaySamples + mod);

                // Dispersion, then the losses: a spring carries almost no low
                // end, and loses a little off the top on every pass. The
                // damper carries the decay gain itself.
                const float y = s.damper.process (s.chirp.process (delayed));

                s.line.write (drive + y);
                s.line.advance();

                wet[t] += y;
            }
        }

        if (stereo)
        {
            // Cross the two tanks into each other so the pair reads as one
            // wide tank rather than as two unrelated ones.
            constexpr float c = spring::kStereoCrossfeed;
            const float norm = 1.0f / std::sqrt (1.0f + c * c);
            const float l = (wet[0] + c * wet[1]) * norm;
            const float r = (wet[1] + c * wet[0]) * norm;

            outL[n] = outputFilter (l, 0) * outputScale;
            outR[n] = outputFilter (r, 1) * outputScale;
        }
        else
        {
            outL[n] = outputFilter (wet[0], 0) * outputScale;
            outR[n] = outL[n];
        }

        if (! std::isfinite (outL[n]) || ! std::isfinite (outR[n]))
        {
            // Whatever got in there is already in the feedback path, so every
            // later sample would roar. Silence the rest of the block and clear
            // the tank - one glitched block, then recovery.
            for (int k = n; k < numSamples; ++k)
                outL[k] = outR[k] = 0.0f;
            reset();
            return;
        }
    }
}

} // namespace ee::dsp
