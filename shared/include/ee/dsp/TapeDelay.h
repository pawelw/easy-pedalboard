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

    /** Delay per channel, in seconds. The *output* tap glides rather than
        jumps, so moving the knob while a repeat is ringing warps its pitch -
        the tape-head-shifting sound, and the point of gliding at all.

        The feedback tap does not glide (see Channel::loopSamples): it re-aims
        in short crossfaded steps, so that warp is heard once, on its way out,
        instead of being written back into the loop and repeated for the rest
        of the tail. */
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

        /** The output tap. Glides towards targetSamples, which is what makes a
            moving Time knob warp the pitch of whatever is passing the head. */
        float currentSamples = 0.0f;

        /** The feedback tap, and the two things that move it. It holds still
            while `loopFade` is 1, then re-aims by crossfading from
            loopFromSamples to loopSamples over kLoopFadeSeconds - a read
            position that steps rather than slides, so nothing it writes back
            into the line has been resampled. */
        float loopSamples = 0.0f;
        float loopFromSamples = 0.0f;
        float loopFade = 1.0f;

        float wowPhase = 0.0f;

        /** One loop rolloff state per tap. They see the same input and the
            same history whenever the two taps coincide, so in the steady state
            the feedback path is sample-for-sample what a single shared filter
            gave before; they only diverge while the feedback tap is stepping. */
        float lowpassState = 0.0f;
        float loopLowpassState = 0.0f;
    };

    void updateCharacter() noexcept;

    double sr = 44100.0;

    std::array<Channel, 2> channels;

    float feedbackGain = 0.0f;
    float modAmount = 0.0f;

    float glideCoeff = 0.001f;
    float loopFadeInc = 1.0f;

    float lowpassCoeff = 1.0f;
    float wowInc = 0.0f;
    float wowDepth = 0.0f;
};

} // namespace ee::dsp
