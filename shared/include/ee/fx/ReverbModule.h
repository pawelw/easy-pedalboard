#pragma once

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/SpringReverb.h"
#include "ee/fx/MultiEngineModule.h"

namespace ee::fx
{

/** Peak Alpine's Reverb module: two engines, one at a time.
 *
 * Space is Peak Reverb's FDN, Spring is Peak Spring's tank - the same two
 * engines those pedals run, so neither can drift from its own pedal.
 *
 * Both are mono in, stereo out, which is what a reverb is: a room does not have
 * a left and a right input. The module sums the incoming stereo before the send
 * exactly as those two pedals do, so a signal already wide does not arrive as
 * two uncorrelated rooms.
 */
class ReverbModule final : public MultiEngineModule
{
public:
    /** In the order the face steps through them, which is also the order of the
        owner's choice parameter. */
    enum Engine
    {
        Space = 0,
        Spring,
        NumEngines
    };

    // -------------------------------------------------------------- the knobs

    void setSpace (float decaySeconds, float shimmer01, float lowCutHz, float resonance01) noexcept
    {
        space.setDecayTime (decaySeconds);
        space.setShimmer (shimmer01);
        space.setLowCut (lowCutHz);
        space.setResonance (resonance01);
    }

    /** Three, not four: a spring tank has no resonance to expose. What Space
        calls Reso is how hard its network is allowed to ring, and a tank's
        equivalent of that is its decay. */
    void setSpring (float decaySeconds, float tension01, float lowCutHz) noexcept
    {
        spring.setDecayTime (decaySeconds);
        spring.setTension01 (tension01);
        spring.setLowCut (lowCutHz);
    }

    /** How long the selected engine rings for, so the owner can answer a host's
        getTailLengthSeconds. Mid-switch it is the longer of the two, because
        both really are still ringing. */
    double tailSeconds() const noexcept
    {
        return currentEngine() == Spring ? static_cast<double> (spring.getTailSeconds())
                                         : static_cast<double> (space.getTailSeconds());
    }

protected:
    int engineCount() const noexcept override { return NumEngines; }

    void prepareEngines (double sampleRate, int maxBlockSize) override
    {
        space.prepare (sampleRate);
        spring.prepare (sampleRate);

        monoBuffer.assign (static_cast<size_t> (juce::jmax (1, maxBlockSize)), 0.0f);

        // The tank's own stereo conceit stays on: it is two tanks a few per
        // cent apart, which is what opens the tail up. Peak Spring exposes a
        // switch for it; this face does not, so it keeps the better default.
        spring.setStereo (true);
    }

    void renderEngine (int index, const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet,
                       int numChannels, int numSamples) noexcept override
    {
        if (static_cast<int> (monoBuffer.size()) < numSamples)
            monoBuffer.assign (static_cast<size_t> (numSamples), 0.0f);

        float* mono = monoBuffer.data();

        if (numChannels > 1)
            for (int i = 0; i < numSamples; ++i)
                mono[i] = 0.5f * (dry.getReadPointer (0)[i] + dry.getReadPointer (1)[i]);
        else
            for (int i = 0; i < numSamples; ++i)
                mono[i] = dry.getReadPointer (0)[i];

        // Always two out, whatever the host's bus is - see ModulationModule's
        // note. Handing the same pointer in twice would have the tank's right
        // side write over its left.
        float* outL = wet.getWritePointer (0);
        float* outR = wet.getWritePointer (1);

        if (index == Spring)
            spring.process (mono, outL, outR, numSamples);
        else
            space.process (mono, outL, outR, numSamples);
    }

    /** The one module that does not keep its engines warm. Two reasons, and
        both have to hold: a 16-line FDN and a three-spring tank running at once
        is the heaviest thing in the plugin, and neither of them clicks cold -
        a reverb's output *is* a gradual build from silence, so there is no dry
        path in it to arrive abruptly the way a chorus's delayed signal does.
        ee_module_stress measures the second claim rather than assuming it. */
    bool enginesRunWarm() const noexcept override { return false; }

    void resetEngine (int index) noexcept override
    {
        if (index == Spring)
            spring.reset();
        else
            space.reset();
    }

private:
    ee::dsp::FdnReverb space;
    ee::dsp::SpringReverb spring;

    /** The send. A reverb takes one signal, so the two sides are summed here
        rather than each being given a room of its own. */
    std::vector<float> monoBuffer;
};

} // namespace ee::fx
