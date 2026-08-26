#pragma once

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Allpass.h"
#include "ModDelayLine.h"
#include "OnePoleDamper.h"

namespace ee::dsp
{

/** Feedback delay network reverb: mono in, stereo out.

    Decay time is the only size control exposed. Room size and predelay are
    derived from it so the space stays plausible across the whole sweep.
*/
class FdnReverb
{
public:
    static constexpr int kLines = 8;
    static constexpr float kMinDecay = 0.3f;
    static constexpr float kMaxDecay = 8.0f;

    void prepare (double sampleRate);
    void reset();

    void setDecayTime (float seconds) noexcept;
    void setModulation (float amount01) noexcept;

    /** Fixed voicing knobs. Not exposed on the pedal, but future effects can use them. */
    void setDecayTilt (float lowMultiplier, float highMultiplier) noexcept;

    void process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept;

    float getTailSeconds() const noexcept { return decaySeconds * 1.5f + 0.25f; }

private:
    void updateDerived() noexcept;

    double sr = 44100.0;
    bool dirty = true;

    float decaySeconds = 2.0f;
    float modAmount = 0.25f;
    float lowMult = 1.20f;
    float highMult = 0.45f;

    std::array<ModDelayLine, kLines> lines;
    std::array<OnePoleDamper, kLines> dampers;
    std::array<juce::SmoothedValue<float>, kLines> delaySmooth;
    std::array<float, kLines> lfoPhase {};
    std::array<float, kLines> lfoInc {};
    float modDepthSamples = 0.0f;

    std::array<Allpass, 4> diffusers;
    ModDelayLine predelayLine;
    juce::SmoothedValue<float> predelaySmooth;
    juce::SmoothedValue<float> outputScale;
};

} // namespace ee::dsp
