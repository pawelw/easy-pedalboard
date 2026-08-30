#pragma once

#include <array>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Allpass.h"
#include "LoopDamper.h"
#include "ModDelayLine.h"
#include "ReverbConfig.h"
#include "ShimmerTuning.h"

namespace daisysp { class PitchShifter; }

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
    static constexpr float kMinLowCutHz = 20.0f;
    static constexpr float kMaxLowCutHz = 800.0f;

    FdnReverb();
    ~FdnReverb();

    void prepare (double sampleRate);
    void reset();

    void setDecayTime (float seconds) noexcept;

    /** Two-pole highpass across the wet output. kMinLowCutHz is effectively off. */
    void setLowCut (float hz) noexcept;

    /** Scoops the midrange out of the wet output. 0 leaves it flat. */
    /** How much the tail is allowed to ring: 0 is fully smeared, 1 rings hardest.

        Two stages, because moving the delay lines is the only thing that
        smooths the tail but it bottoms out at zero. Below halfway this backs
        the movement off; above halfway, with the lines already still, it thins
        the in-loop diffusion so the modes stand out further.
    */
    void setResonance (float amount01) noexcept;

    /** Fixed voicing knobs. Not exposed on the pedal, but future effects can use them.
        Both are fractions of the mid-band decay and are clamped to 1.0, so no
        band can ever ring longer than the decay knob says.
    */
    void setDecayTilt (float lowRatio, float highRatio) noexcept;

    /** Amount of pitch-shifted tail folded back into the network, 0..1. 0 is
        exactly the reverb with no shimmer path running at all; turning it up
        stacks an octave on the tail on every pass round the loop. */
    void setShimmer (float amount01) noexcept;

    /** The full shimmer voicing. Safe to call while playing - the development
        tuning panel drives it live. */
    const ShimmerTuning& getShimmerTuning() const noexcept { return shimmerTuning; }
    void setShimmerTuning (const ShimmerTuning& newTuning) noexcept;

    void process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept;

    float getTailSeconds() const noexcept
    {
        // The shimmer feedback keeps re-exciting the network, so the audible
        // tail outlasts the bare decay the more of it is dialled in.
        return (decaySeconds * 1.5f + 0.25f) * (1.0f + 0.6f * shimmerAmount);
    }

private:
    void updateDerived() noexcept;

    /** Re-derives the sample-rate-dependent shimmer state (filter coeffs, Haas
        offset, shifter transpositions and flutter, gain target) from
        shimmerTuning. Called from prepare and setShimmerTuning. */
    void updateShimmerDerived() noexcept;

    double sr = 44100.0;
    bool dirty = true;

    float decaySeconds = 2.0f;
    float resonance = 0.5f;
    float lowCutHz = kMinLowCutHz;
    float lowRatio = config::kLowDecayRatio;
    float highRatio = config::kHighDecayRatio;

    // Shimmer: a stereo pair of pitch shifters fed a predelayed tap of the wet
    // output, their results shaped and injected back into the network one
    // sample later as a mono centre plus a scaled L/R difference, so the octave
    // re-enters as a wide field rather than a mono point. The two read the
    // predelay line a Haas offset apart. Held behind pointers so the DaisySP
    // header stays out of this one.
    float shimmerAmount = 0.0f;
    ShimmerTuning shimmerTuning;
    std::unique_ptr<daisysp::PitchShifter> shimmerShifterL;
    std::unique_ptr<daisysp::PitchShifter> shimmerShifterR;
    juce::SmoothedValue<float> shimmerGain;

    float shimmerFeedbackL = 0.0f;
    float shimmerFeedbackR = 0.0f;

    // Per-side one-pole states: a lowpass (band limit), a lowpass used as a
    // highpass (kills regen rumble), and lowpasses feeding the bass and
    // sparkle shelves.
    float shimmerHighCutStateL = 0.0f, shimmerHighCutStateR = 0.0f;
    float shimmerLowCutStateL = 0.0f,  shimmerLowCutStateR = 0.0f;
    float shimmerBassStateL = 0.0f,    shimmerBassStateR = 0.0f;
    float shimmerShelfStateL = 0.0f,   shimmerShelfStateR = 0.0f;
    float shimmerLowCutCoeff = 0.0f;
    float shimmerHighCutCoeff = 1.0f;
    float shimmerBassCoeff = 0.0f;
    float shimmerShelfCoeff = 0.0f;

    // Predelay on the octave feedback so it blooms behind the note; the Haas
    // read offset that opens the two sides apart.
    ModDelayLine shimmerPredelay;
    juce::SmoothedValue<float> shimmerPredelaySmooth;
    float shimmerHaasSamples = 0.0f;

    std::array<ModDelayLine, kLines> lines;
    std::array<LoopDamper, kLines> dampers;
    std::array<Allpass, kLines> tank;
    std::array<Allpass, kLines> tank2;
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
    juce::SmoothedValue<float> lowCutCoeff;
    std::array<float, 4> lowCutState {};
};

} // namespace ee::dsp
