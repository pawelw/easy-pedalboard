#pragma once

#include "ee/dsp/ModDelayLine.h"

#include <array>

namespace ee::dsp
{

/** Stereo delay with independent per-channel times.

    The two character controls are additive stages sitting in the repeat path,
    so every trip round the loop compounds them:

      - modulation is tape wow and flutter plus a gentle loop rolloff and a
        touch of soft clipping, which is what makes the repeats read as analog;
      - crush is decimation, bit reduction and hard drive on top of that, for
        the worn-tape / broken-sampler end.

    With both at zero every stage is bypassed exactly, so the plugin is a clean
    digital delay rather than an almost-clean one.
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
    void setCrush (float amount01) noexcept;

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
        float flutterPhase = 0.0f;
        float lowpassState = 0.0f;
        float highpassState = 0.0f;
        float holdValue = 0.0f;
        float holdPhase = 1.0f;
    };

    float character (Channel& ch, float x) noexcept;
    void updateCharacter() noexcept;
    void updateFeedback() noexcept;

    double sr = 44100.0;

    std::array<Channel, 2> channels;

    float feedbackAmount = 0.0f;
    float feedbackGain = 0.0f;
    float modAmount = 0.0f;
    float crushAmount = 0.0f;

    float glideCoeff = 0.001f;

    // All derived in updateCharacter(); the neutral values bypass their stage.
    float lowpassCoeff = 1.0f;
    float highpassCoeff = 0.0f;
    float drive = 0.0f;
    float quantSteps = 0.0f;
    float holdStep = 1.0f;
    float wowInc = 0.0f;
    float flutterInc = 0.0f;
    float wowDepth = 0.0f;
    float flutterDepth = 0.0f;
};

} // namespace ee::dsp
