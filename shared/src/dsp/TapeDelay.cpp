#include "ee/dsp/TapeDelay.h"

#include <algorithm>
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

    // How the feedback tap re-aims when the time changes. It does not glide
    // the way the output tap does: it holds its position, then crossfades to
    // the new one over this long. A resampled read is what warps pitch, so a
    // tap that only ever steps writes unwarped audio back into the line - the
    // head-shift warble is heard once on the way out instead of being baked
    // into the loop and repeated for the whole tail.
    //
    // 20 ms is the usual crossfade compromise: long enough that the splice is
    // a soft flange rather than a click, short enough that a knob being swept
    // tracks in steps too small to hear individually.
    constexpr float kLoopFadeSeconds = 0.020f;

    // Target movement below this doesn't start a crossfade, so once the knob
    // stops the loop tap can sit this far from the exact time - a repeat
    // spacing error of one sample, twenty microseconds, against an output tap
    // that is exact. Small enough to be a rounding error, and having a floor
    // at all is what stops a parameter dithering by a fraction of a sample
    // from splicing the loop forever.
    constexpr float kLoopStepSamples = 1.0f;

    // Equal-power would be wrong here: the two taps are the same signal read a
    // few milliseconds apart, so they are correlated, and a linear fade is
    // what keeps the sum level through the splice. Smoothstepped so the fade
    // starts and ends without a corner.
    inline float fadeShape (float t) noexcept
    {
        return t * t * (3.0f - 2.0f * t);
    }

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
    loopFadeInc = 1.0f / std::max (1.0f, kLoopFadeSeconds * static_cast<float> (sr));

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
        ch.loopLowpassState = 0.0f;
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
    {
        ch.currentSamples = ch.targetSamples;
        ch.loopSamples = ch.targetSamples;
        ch.loopFromSamples = ch.targetSamples;
        ch.loopFade = 1.0f;
    }
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

            // The feedback tap only ever moves between crossfades, and only
            // when the move is worth splicing for. Anything smaller is left
            // for the next step to pick up.
            if (ch.loopFade >= 1.0f && std::abs (ch.targetSamples - ch.loopSamples) > kLoopStepSamples)
            {
                ch.loopFromSamples = ch.loopSamples;
                ch.loopSamples = ch.targetSamples;
                ch.loopFade = 0.0f;
            }

            float wobble = 0.0f;

            if (wowDepth > 0.0f)
            {
                ch.wowPhase += wowInc * kRateScale[c];
                if (ch.wowPhase >= 1.0f) ch.wowPhase -= 1.0f;

                wobble += wowDepth * std::sin (kTwoPi * ch.wowPhase);
            }

            // Drift rides both taps. It is the delay line's own modulation,
            // documented as compounding with every repeat, so it has to be in
            // the loop - unlike the head-shift warp, which is exactly what
            // must not be.
            //
            // Two reads of one line, then: where the head is now, and where
            // the loop is spliced to. They coincide whenever the knob is
            // still, and the second read costs a handful of ops, so this is
            // unconditional rather than branched on their being equal - a
            // branch that flips mid-signal would put a step in the loop's read
            // position, which is the one thing this whole tap exists to avoid.
            const float y = ch.line.read (ch.currentSamples + wobble);

            float loop = ch.line.read (ch.loopSamples + wobble);

            if (ch.loopFade < 1.0f)
            {
                const float from = ch.line.read (ch.loopFromSamples + wobble);

                loop = from + (loop - from) * fadeShape (ch.loopFade);
                ch.loopFade = std::min (1.0f, ch.loopFade + loopFadeInc);
            }

            // Both rolloff states run every sample, settled or not: they see
            // the same input and the same history while the taps coincide, so
            // the loop filter is never picking up from a stale value when a
            // step does begin.
            float outSample = y;

            if (lowpassCoeff < 1.0f)
            {
                ch.lowpassState += lowpassCoeff * (y - ch.lowpassState);
                ch.loopLowpassState += lowpassCoeff * (loop - ch.loopLowpassState);

                outSample = ch.lowpassState;
                loop = ch.loopLowpassState;
            }

            out[c][i] = outSample;

            ch.line.write (in[c][i] + loop * feedbackGain);
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
