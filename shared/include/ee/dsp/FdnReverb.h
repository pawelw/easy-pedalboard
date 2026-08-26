#pragma once

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Allpass.h"
#include "LoopDamper.h"
#include "ModDelayLine.h"
#include "ReverbConfig.h"

namespace ee::dsp
{

/** Plate-voiced feedback delay network: mono in, stereo out.

    Sixteen lines rather than the usual eight, because at eight the mode
    density audibly thins out as the tail decays and the late reflections start
    to separate into distinct bounces.

    Decay time is the only size control exposed. Room size and predelay are
    derived from it so the space stays plausible across the whole sweep.
*/
class FdnReverb
{
public:
    static constexpr int kLines = 16;
    static constexpr int kDiffusers = 8;
    static constexpr int kDecorrelators = 3;
    // The diffusion ladder and the output decorrelators ring on for around
    // half a second regardless of the network, so anything shorter than this
    // could not be delivered and the knob would be lying.
    static constexpr float kMinDecay = 0.5f;
    static constexpr float kMaxDecay = 8.0f;
    static constexpr float kMinHighCutHz = 800.0f;
    static constexpr float kMaxHighCutHz = 20000.0f;

    void prepare (double sampleRate);
    void reset();

    void setDecayTime (float seconds) noexcept;
    void setModulation (float amount01) noexcept;

    /** Two-pole lowpass across the wet output. kMaxHighCutHz is effectively off. */
    void setHighCut (float hz) noexcept;

    /** Fixed voicing knobs. Not exposed on the pedal, but future effects can use them.
        Both are fractions of the mid-band decay and are clamped to 1.0, so no
        band can ever ring longer than the decay knob says.
    */
    void setDecayTilt (float lowRatio, float highRatio) noexcept;

    void process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept;

    float getTailSeconds() const noexcept { return decaySeconds * 1.5f + 0.25f; }

private:
    void updateDerived() noexcept;

    double sr = 44100.0;
    bool dirty = true;

    float decaySeconds = 2.0f;
    float modAmount = 0.25f;
    float highCutHz = kMaxHighCutHz;
    float lowRatio = config::kLowDecayRatio;
    float highRatio = config::kHighDecayRatio;

    std::array<ModDelayLine, kLines> lines;
    std::array<LoopDamper, kLines> dampers;
    std::array<Allpass, kLines> tank;
    std::array<juce::SmoothedValue<float>, kLines> delaySmooth;
    std::array<float, kLines> lfoPhase {};
    std::array<float, kLines> lfoInc {};
    float modDepthSamples = 0.0f;

    std::array<Allpass, kDiffusers> diffusers;
    std::array<Allpass, kDecorrelators> spreadL;
    std::array<Allpass, kDecorrelators> spreadR;

    ModDelayLine predelayLine;
    juce::SmoothedValue<float> predelaySmooth;
    juce::SmoothedValue<float> outputScale;
    juce::SmoothedValue<float> highCutCoeff;
    std::array<float, 4> highCutState {};
};

} // namespace ee::dsp
