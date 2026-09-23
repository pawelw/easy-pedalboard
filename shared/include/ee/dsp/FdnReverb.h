#pragma once

#include <array>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Allpass.h"
#include "LoopDamper.h"
#include "ModDelayLine.h"
#include "ReverbConfig.h"
#include "ShimmerTuning.h"

namespace ee::dsp { class ShimmerPitchShifter; }

namespace ee::dsp
{

/** Plate-voiced feedback delay network: mono in, stereo out.

    Sixteen lines rather than the usual eight, because at eight the mode
    density audibly thins out as the tail decays and the late reflections start
    to separate into distinct bounces.

    Decay time is the only size control exposed. Room size and predelay are
    derived from it so the space stays plausible across the whole sweep.

    Every knob here is available to a caller that wants the plain plate (BitBit
    Grain's own instance runs it that way, with Shimmer at 0). BitBit Reverb's
    Shimmer engine (ee::fx::ReverbModule) is a narrower, fixed voicing of the
    same class: Decay pinned at kMaxDecay and Shimmer at 1.0 rather than knobs,
    so what the face exposes is only setOctave, setLowCut/setHighCut and
    setDamping - see ReverbModule::setShimmer.
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
    static constexpr float kMinHighCutHz = 1000.0f;
    static constexpr float kMaxHighCutHz = 20000.0f;
    static constexpr float kMinPredelayMs = config::kUserPredelayMinMs;
    static constexpr float kMaxPredelayMs = config::kUserPredelayMaxMs;

    FdnReverb();
    ~FdnReverb();

    void prepare (double sampleRate);
    void reset();

    void setDecayTime (float seconds) noexcept;

    /** Two-pole highpass across the wet output. kMinLowCutHz is effectively off. */
    void setLowCut (float hz) noexcept;

    /** Two-pole lowpass across the wet output. kMaxHighCutHz is effectively off. */
    void setHighCut (float hz) noexcept;

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

    /** How fast the top end dies relative to the decay knob, 0..1. 0.5 (the
        default) reproduces the fixed kHighDecayRatio voicing; below it the
        tail stays brighter than that, above it the top is absorbed faster.
        The low band is untouched - see setDecayTilt for that. */
    void setDamping (float amount01) noexcept;

    /** Explicit predelay in ms (kMinPredelayMs..kMaxPredelayMs), overriding
        the decay-linked auto amount. Pass a negative value to return to that
        auto amount. */
    void setPredelay (float ms) noexcept;

    /** Amount of pitch-shifted tail folded back into the network, 0..1. 0 is
        exactly the reverb with no shimmer path running at all; turning it up
        stacks an octave on the tail on every pass round the loop. */
    void setShimmer (float amount01) noexcept;

    /** -1, 0 or +1 octaves on the shimmer's pitch shift (-12/0/+12 semitones),
        clamped to that range. This is the runtime control; ShimmerTuning::
        semitones is left as the struct's own documented reference default and
        is not read for this any more. Default +1 (an octave up), the original
        fixed voicing. Safe to call while playing - the transposition change
        is not smoothed, the same as a live retune through setShimmerTuning,
        and the shifter's own crossfading grains keep it from clicking. */
    void setOctave (int octaves) noexcept;

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
    // Negative = no override, use the decay-linked auto amount.
    float predelayOverrideMs = -1.0f;

    // Shimmer: a stereo pair of pitch shifters fed a predelayed tap of the wet
    // output, their results shaped and injected back into the network one
    // sample later as a mono centre plus a scaled L/R difference, so the octave
    // re-enters as a wide field rather than a mono point. The two read the
    // predelay line a Haas offset apart. Held behind pointers so the DaisySP
    // header stays out of this one.
    float shimmerAmount = 0.0f;
    // +1 octave, the original fixed voicing - see setOctave.
    float pitchSemitones = 12.0f;
    ShimmerTuning shimmerTuning;
    std::unique_ptr<ShimmerPitchShifter> shimmerShifterL;
    std::unique_ptr<ShimmerPitchShifter> shimmerShifterR;
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
    float highCutHz = kMaxHighCutHz;
    juce::SmoothedValue<float> highCutCoeff;
    std::array<float, 4> highCutState {};

    // Low shelf on the finished wet output, for body. Outside every feedback
    // path, so it only ever colours what you hear.
    std::array<float, 2> wetLowShelfState {};
    float wetLowShelfCoeff = 0.0f;
};

} // namespace ee::dsp
