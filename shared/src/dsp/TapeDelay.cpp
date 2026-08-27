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

    constexpr float kWowHz = 0.42f;
    constexpr float kFlutterHz = 5.9f;
    constexpr float kWowSeconds = 0.0032f;
    constexpr float kFlutterSeconds = 0.0004f;

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
        ch.highpassState = 0.0f;
        ch.holdValue = 0.0f;
        ch.holdPhase = 1.0f;
        ch.wowPhase = c == 0 ? 0.0f : 0.27f;
        ch.flutterPhase = c == 0 ? 0.0f : 0.5f;
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
    feedbackAmount = std::clamp (amount01, 0.0f, 1.0f);
    updateFeedback();
}

void TapeDelay::updateFeedback() noexcept
{
    // The drive stage eats level, so without this the crushed settings turn
    // into a single grubby slap however far the feedback knob is up.
    feedbackGain = feedbackAmount * kMaxFeedback * (1.0f + crushAmount * 0.10f);
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

void TapeDelay::setCrush (float amount01) noexcept
{
    const float next = std::clamp (amount01, 0.0f, 1.0f);
    if (next != crushAmount)
    {
        crushAmount = next;
        updateCharacter();
        updateFeedback();
    }
}

void TapeDelay::updateCharacter() noexcept
{
    const float m = modAmount;
    const float c = crushAmount;

    // Both controls pull the same rolloff down, crush considerably harder.
    float cutoff = 20000.0f * std::pow (0.30f, m) * std::pow (0.15f, c);
    cutoff = std::clamp (cutoff, 1400.0f, 20000.0f);

    const float openCorner = 0.4f * static_cast<float> (sr);
    lowpassCoeff = cutoff >= openCorner ? 1.0f : onePoleCoeff (cutoff, sr);

    // A little always-on rumble trim; the crushed settings get properly thin.
    highpassCoeff = onePoleCoeff (15.0f + m * 55.0f + c * 150.0f, sr);

    drive = m * 0.45f + c * 2.0f;

    quantSteps = c > 0.001f ? std::pow (2.0f, 16.0f - 12.0f * c - 1.0f) : 0.0f;

    // Aliasing on purpose: no filter in front of the decimator.
    holdStep = c > 0.001f ? std::pow (3200.0f / static_cast<float> (sr), c) : 1.0f;

    wowInc = kWowHz / static_cast<float> (sr);
    flutterInc = kFlutterHz / static_cast<float> (sr);
    wowDepth = m * kWowSeconds * static_cast<float> (sr);
    flutterDepth = m * kFlutterSeconds * static_cast<float> (sr);
}

float TapeDelay::character (Channel& ch, float x) noexcept
{
    float y = x;

    if (holdStep < 1.0f)
    {
        ch.holdPhase += holdStep;
        if (ch.holdPhase >= 1.0f)
        {
            ch.holdPhase -= 1.0f;
            ch.holdValue = y;
        }
        y = ch.holdValue;
    }

    if (quantSteps > 0.0f)
        y = std::round (y * quantSteps) / quantSteps;

    // Unity slope at the origin, so drive == 0 is bit-exact passthrough.
    if (drive > 0.0f)
        y = y / (1.0f + drive * std::abs (y));

    if (lowpassCoeff < 1.0f)
    {
        ch.lowpassState += lowpassCoeff * (y - ch.lowpassState);
        y = ch.lowpassState;
    }

    ch.highpassState += highpassCoeff * (y - ch.highpassState);
    y -= ch.highpassState;

    return y;
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

                ch.flutterPhase += flutterInc * kRateScale[c];
                if (ch.flutterPhase >= 1.0f) ch.flutterPhase -= 1.0f;

                wobble = wowDepth * std::sin (kTwoPi * ch.wowPhase)
                       + flutterDepth * std::sin (kTwoPi * ch.flutterPhase);
            }

            const float shaped = character (ch, ch.line.read (ch.currentSamples + wobble));

            out[c][i] = shaped;

            ch.line.write (in[c][i] + shaped * feedbackGain);
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
