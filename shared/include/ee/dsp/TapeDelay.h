#pragma once

#include "ee/dsp/ModDelayLine.h"

#include <array>

namespace ee::dsp
{

/** Stereo delay with independent per-channel times.

    Modulation is slow wow plus a gentle loop rolloff, and it lives inside the
    feedback path so it compounds with every repeat. Tape colour is not in here:
    it sits in front of the delay, where a tape machine would be in a chain.

    With modulation at zero every stage is bypassed exactly, so the plugin is a
    clean digital delay rather than an almost-clean one.
*/
class TapeDelay
{
public:
    static constexpr float kMaxDelaySeconds = 6.0f;

    void prepare (double sampleRate);
    void reset();

    /** Delay per channel, in seconds. Changes glide rather than jump, so
        switching division while a repeat is ringing warps its pitch. */
    void setDelaySeconds (float left, float right) noexcept;

    /** Drops the glide, for the first block after prepare or a state load. */
    void snapDelays() noexcept;

    void setFeedback (float amount01) noexcept;
    void setModulation (float amount01) noexcept;

    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples) noexcept;

    float getTailSeconds() const noexcept;

private:
    struct Channel
    {
        ModDelayLine line;
        float targetSamples = 0.0f;
        float currentSamples = 0.0f;
        float wowPhase = 0.0f;
        float lowpassState = 0.0f;
    };

    void updateCharacter() noexcept;

    double sr = 44100.0;

    std::array<Channel, 2> channels;

    float feedbackGain = 0.0f;
    float modAmount = 0.0f;

    float glideCoeff = 0.001f;

    float lowpassCoeff = 1.0f;
    float wowInc = 0.0f;
    float wowDepth = 0.0f;
};

} // namespace ee::dsp
