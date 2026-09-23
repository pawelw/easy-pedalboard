#pragma once

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/SpaceReverb.h"
#include "ee/dsp/SpringReverb.h"
#include "ee/fx/MultiEngineModule.h"

namespace ee::fx
{

/** BitBit Reverb's engines, and BitBit Alpine's Reverb module: three, one at a
 * time.
 *
 *   - Spring is BitBit Spring's tank, the same engine that pedal runs.
 *   - Shimmer is FdnReverb - the engine this module was called Space for, with
 *     its octave feedback, Reso and all. Its parameters kept the `space.` ids,
 *     so a session saved before Modern existed opens on the same sound.
 *   - Modern is SpaceReverb, voiced against NI Raum (see SpaceConfig.h). No
 *     shimmer of its own; that is what the engine beside it is for.
 *
 * Spring and Shimmer are mono in, stereo out - a tank has one input, and the
 * FDN was voiced that way - so the module sums the incoming stereo for them
 * exactly as their pedals did. Modern is true stereo in: each input side has
 * its own echoes and its own way into the network, as the reference does, so a
 * wide source stays wide.
 *
 * Modern's Mix law is the reference's too, not the plugin-wide equal-power one
 * the other two keep: dry held at unity up to 50 % and then faded out, wet
 * rising as (2 x Mix)^1.5 to full at 50 %. Measured off Raum; a Mix of 20 %
 * there is dry 1.0 / wet 0.25, which equal power (0.95 / 0.31) is audibly not.
 */
class ReverbModule final : public MultiEngineModule
{
public:
    /** In the order the face steps through them, which is also the order of the
        owner's choice parameter. */
    enum Engine
    {
        Spring = 0,
        Shimmer,
        Modern,
        NumEngines
    };

    // -------------------------------------------------------------- the knobs

    void setShimmer (float decaySeconds, float shimmer01, float lowCutHz, float resonance01, float predelayMs,
                     float damping01) noexcept
    {
        shimmer.setDecayTime (decaySeconds);
        shimmer.setShimmer (shimmer01);
        shimmer.setLowCut (lowCutHz);
        shimmer.setResonance (resonance01);
        shimmer.setPredelay (predelayMs);
        shimmer.setDamping (damping01);
    }

    void setModern (float decaySeconds, float predelayMs, float damping01, float lowCutHz, float highCutHz) noexcept
    {
        modern.setDecayTime (decaySeconds);
        modern.setPredelay (predelayMs);
        modern.setDamping (damping01);
        modern.setLowCut (lowCutHz);
        modern.setHighCut (highCutHz);
    }

    void setSpring (float decaySeconds, float tension01, float lowCutHz) noexcept
    {
        spring.setDecayTime (decaySeconds);
        spring.setTension01 (tension01);
        spring.setLowCut (lowCutHz);
    }

    /** How long the selected engine rings for, so the owner can answer a host's
        getTailLengthSeconds. */
    double tailSeconds() const noexcept
    {
        switch (currentEngine())
        {
            case Spring:  return static_cast<double> (spring.getTailSeconds());
            case Shimmer: return static_cast<double> (shimmer.getTailSeconds());
            default:      return static_cast<double> (modern.getTailSeconds());
        }
    }

protected:
    int engineCount() const noexcept override { return NumEngines; }

    float dryGainFor (int index, float mix01) const noexcept override
    {
        return index == Modern ? juce::jmin (1.0f, 2.0f * (1.0f - mix01))
                               : MultiEngineModule::dryGainFor (index, mix01);
    }
    float wetGainFor (int index, float mix01) const noexcept override
    {
        return index == Modern ? std::pow (juce::jmin (1.0f, 2.0f * mix01), 1.5f)
                               : MultiEngineModule::wetGainFor (index, mix01);
    }

    void prepareEngines (double sampleRate, int maxBlockSize) override
    {
        shimmer.prepare (sampleRate);
        modern.prepare (sampleRate);
        spring.prepare (sampleRate);

        monoBuffer.assign (static_cast<size_t> (juce::jmax (1, maxBlockSize)), 0.0f);

        // The tank's own stereo conceit stays on: it is two tanks a few per
        // cent apart, which is what opens the tail up. BitBit Spring exposes a
        // switch for it; this face does not, so it keeps the better default.
        spring.setStereo (true);
    }

    void renderEngine (int index, const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet,
                       int numChannels, int numSamples) noexcept override
    {
        // Always two out, whatever the host's bus is - see ModulationModule's
        // note. Handing the same pointer in twice would have the tank's right
        // side write over its left.
        float* outL = wet.getWritePointer (0);
        float* outR = wet.getWritePointer (1);

        const float* inL = dry.getReadPointer (0);
        const float* inR = dry.getReadPointer (numChannels > 1 ? 1 : 0);

        if (index == Modern)
        {
            modern.process (inL, inR, outL, outR, numSamples);
            return;
        }

        if (static_cast<int> (monoBuffer.size()) < numSamples)
            monoBuffer.assign (static_cast<size_t> (numSamples), 0.0f);

        float* mono = monoBuffer.data();
        for (int i = 0; i < numSamples; ++i)
            mono[i] = numChannels > 1 ? 0.5f * (inL[i] + inR[i]) : inL[i];

        if (index == Spring)
            spring.process (mono, outL, outR, numSamples);
        else
            shimmer.process (mono, outL, outR, numSamples);
    }

    /** The one module that does not keep its engines warm. Two reasons, and
        both have to hold: three reverbs running at once is the heaviest thing
        in the plugin, and none of them clicks cold. The tank's and the FDN's
        output is a gradual build from silence; Modern's first sound is a
        discrete echo, a delayed copy of the input, so it ramps its input in
        after a reset rather than starting that copy mid-waveform.
        ee_module_stress measures the claim rather than assuming it. */
    bool enginesRunWarm() const noexcept override { return false; }

    void resetEngine (int index) noexcept override
    {
        switch (index)
        {
            case Spring:  spring.reset(); break;
            case Shimmer: shimmer.reset(); break;
            default:      modern.reset(); break;
        }
    }

private:
    ee::dsp::FdnReverb shimmer;
    ee::dsp::SpaceReverb modern;
    ee::dsp::SpringReverb spring;

    /** The mono engines' send: a tank takes one signal, so the two sides are
        summed. */
    std::vector<float> monoBuffer;
};

} // namespace ee::fx
