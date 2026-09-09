#pragma once

#include "ee/dsp/AutoWah.h"
#include "ee/fx/MultiEngineModule.h"

namespace ee::fx
{

/** Peak Artifact's module: three engines, one at a time.
 *
 * Only Filter is voiced. Ring Mod and Bit Crush are placeholders - selectable,
 * latency-free, and audibly nothing: they run fully wet with their wet output a
 * straight copy of the dry, so the module hands the input back untouched. When
 * one of them is selected `engineUsesMix` is false, so the footer Mix knob does
 * not fold a scaled dry back on top of a scaled copy of itself and give a level
 * bump at half travel - the two no-op engines are exactly unity at every Mix.
 *
 * Filter is `ee::dsp::AutoWah`, the Peak Wah engine, with its per-note envelope
 * taken out of the picture: Decay is pinned fully up (the engine latches on and
 * the wave just runs) and the tap morph is pinned to low-pass. The module owns
 * the dry/wet, so the engine's own Mix is left fully wet.
 *
 * Values are the owner's parameters, not state here - see ModulationModule.
 */
class ArtifactModule final : public MultiEngineModule
{
public:
    /** In the order the face steps through them, which is also the order of the
        owner's choice parameter. */
    enum Engine
    {
        RingMod = 0,
        BitCrush,
        Filter,
        NumEngines
    };

    // -------------------------------------------------------------- the knobs

    /** Every Filter control in one call, in the units the engine takes. `freq01`,
        `q01`, `range01` and `waveShape01` are 0..1; `periodSeconds` is one LFO
        cycle, already resolved from the Time knob and the Sync switch by the
        owner. Decay and tap morph are not offered - see the class note. */
    void setFilter (float freq01, float q01, float range01, float waveShape01,
                    float periodSeconds, bool stereo) noexcept
    {
        wah.setFreq01 (freq01);
        wah.setQ01 (q01);
        wah.setRange01 (range01);
        wah.setShape01 (waveShape01);
        wah.setPeriodSeconds (juce::jmax (1.0e-4f, periodSeconds));
        wah.setStereo (stereo);
    }

    /** The LFO free-runs; when the owner is synced to a running transport it
        also aligns the phase to the host grid, exactly as Peak Wah does. */
    void snapFilterPhase (double target01) noexcept { wah.snapPhase (target01); }
    void nudgeFilterPhase (double target01) noexcept { wah.nudgePhase (target01); }

    /** The Filter engine's signed cutoff-sweep exponent per channel at the last
        processed sample (Range * gate * lfo) - what the face's response scope
        rides on, the same feed Peak Wah pushes. The engine stays warm even when
        it is not the selected one, so these keep moving; the face only reads
        them while Filter is showing. */
    float filterModL() const noexcept { return wah.modL(); }
    float filterModR() const noexcept { return wah.modR(); }

protected:
    int engineCount() const noexcept override { return NumEngines; }

    /** Only Filter is summed against the dry. The two placeholders run fully
        wet, and their wet is the dry unchanged, so the module is unity through
        them at every Mix position rather than +3 dB at the middle. */
    bool engineUsesMix (int index) const noexcept override { return index == Filter; }

    void prepareEngines (double sampleRate, int) override
    {
        wah.prepare (sampleRate);

        // Fixed for the life of the engine: latched on, low-pass tap, and the
        // module's dry/wet rather than the engine's own.
        wah.setDecay01 (1.0f);
        wah.setTypeMorph01 (0.0f);
        wah.setMix01 (1.0f);
    }

    void renderEngine (int index, const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet,
                       int numChannels, int numSamples) noexcept override
    {
        const float* inL = dry.getReadPointer (0);
        const float* inR = dry.getReadPointer (numChannels > 1 ? 1 : 0);

        copyDry (inL, inR, wet, numSamples);

        // Ring Mod and Bit Crush stop here: wet is the dry, untouched.
        if (index == Filter)
            wah.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
    }

    void resetEngine (int index) noexcept override
    {
        if (index == Filter)
            wah.reset();
    }

private:
    static void copyDry (const float* inL, const float* inR, juce::AudioBuffer<float>& wet, int numSamples) noexcept
    {
        juce::FloatVectorOperations::copy (wet.getWritePointer (0), inL, numSamples);
        juce::FloatVectorOperations::copy (wet.getWritePointer (1), inR, numSamples);
    }

    ee::dsp::AutoWah wah;
};

} // namespace ee::fx
