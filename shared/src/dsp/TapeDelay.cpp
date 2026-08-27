#include "ee/dsp/TapeDelay.h"

#include <cmath>

namespace ee::dsp
{
namespace
{
    constexpr float kTwoPi = 6.28318530718f;

    // 0 % feedback is a single slap; 100 % is long but still lands, which is
    // what "reasonable" means here - no self-oscillation on a clean setting.
    constexpr float kMaxFeedback = 0.86f;

    // Mod is the slow, wide movement. Fast flutter belongs to the tape stage in
    // front of the delay, not in here.
    constexpr float kWowHz = 0.42f;
    constexpr float kWowSeconds = 0.0032f;

    // Right runs its wobble slightly slower so the two sides drift apart.
    constexpr float kRateScale[2] = { 1.0f, 0.83f };

    float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = kTwoPi * cornerHz / static_cast<float> (sampleRate);
        return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
    }
}

void TapeDelay::prepare (double sampleRate)
{
    sr = sampleRate;
    glideCoeff = onePoleCoeff (2.6f, sr); // ~60 ms tape-style glide

    for (auto& ch : channels)
        ch.line.prepare (sr, kMaxDelaySeconds + 0.1f);

    updateCharacter();
    reset();
}

void TapeDelay::reset()
{
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto& ch = channels[c];
        ch.line.reset();
        ch.lowpassState = 0.0f;
        ch.wowPhase = c == 0 ? 0.0f : 0.27f;
    }
}

void TapeDelay::setDelaySeconds (float left, float right) noexcept
{
    const float maxSamples = kMaxDelaySeconds * static_cast<float> (sr);

    channels[0].targetSamples = std::clamp (left * static_cast<float> (sr), 2.0f, maxSamples);
    channels[1].targetSamples = std::clamp (right * static_cast<float> (sr), 2.0f, maxSamples);
}

void TapeDelay::snapDelays() noexcept
{
    for (auto& ch : channels)
        ch.currentSamples = ch.targetSamples;
}

void TapeDelay::setFeedback (float amount01) noexcept
{
    feedbackGain = std::clamp (amount01, 0.0f, 1.0f) * kMaxFeedback;
}

void TapeDelay::setModulation (float amount01) noexcept
{
    const float next = std::clamp (amount01, 0.0f, 1.0f);
    if (next != modAmount)
    {
        modAmount = next;
        updateCharacter();
    }
}

void TapeDelay::updateCharacter() noexcept
{
    float cutoff = 20000.0f * std::pow (0.30f, modAmount);
    cutoff = std::clamp (cutoff, 1400.0f, 20000.0f);

    const float openCorner = 0.4f * static_cast<float> (sr);
    lowpassCoeff = cutoff >= openCorner ? 1.0f : onePoleCoeff (cutoff, sr);

    wowInc = kWowHz / static_cast<float> (sr);
    wowDepth = modAmount * kWowSeconds * static_cast<float> (sr);
}

void TapeDelay::process (const float* inL, const float* inR,
                         float* outL, float* outR, int numSamples) noexcept
{
    const float* in[2] = { inL, inR };
    float* out[2] = { outL, outR };

    for (int i = 0; i < numSamples; ++i)
    {
        for (size_t c = 0; c < channels.size(); ++c)
        {
            auto& ch = channels[c];

            ch.currentSamples += glideCoeff * (ch.targetSamples - ch.currentSamples);

            float wobble = 0.0f;

            if (wowDepth > 0.0f)
            {
                ch.wowPhase += wowInc * kRateScale[c];
                if (ch.wowPhase >= 1.0f) ch.wowPhase -= 1.0f;

                wobble += wowDepth * std::sin (kTwoPi * ch.wowPhase);
            }

            float y = ch.line.read (ch.currentSamples + wobble);

            if (lowpassCoeff < 1.0f)
            {
                ch.lowpassState += lowpassCoeff * (y - ch.lowpassState);
                y = ch.lowpassState;
            }

            out[c][i] = y;

            ch.line.write (in[c][i] + y * feedbackGain);
            ch.line.advance();
        }
    }
}

float TapeDelay::getTailSeconds() const noexcept
{
    const float longest = std::max (channels[0].targetSamples, channels[1].targetSamples)
                          / static_cast<float> (sr);

    if (feedbackGain <= 0.001f)
        return longest * 1.5f;

    const float repeats = std::log (0.001f) / std::log (feedbackGain);
    return std::min (30.0f, longest * (repeats + 1.0f));
}

} // namespace ee::dsp
