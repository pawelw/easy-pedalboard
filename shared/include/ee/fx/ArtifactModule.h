#pragma once

#include "ee/dsp/AutoWah.h"
#include "ee/dsp/BitCrusher.h"
#include "ee/dsp/RingModulator.h"
#include "ee/fx/MultiEngineModule.h"

namespace ee::fx
{

/** Peak Artifact's module: three engines, one at a time.
 *
 * All three are voiced. Each takes the footer Mix as its dry/wet - none of them
 * has a wet path that wanders in time (the trap `engineUsesMix` exists for), so
 * a fixed dry alongside the wet combs harmlessly rather than sweeping.
 *
 * Ring Mod is `ee::dsp::RingModulator` - a sine-carrier ring modulator with a
 * post low-pass and two voicings (Earworm's carrier wobble, Green Lantern's
 * octave blend), voiced from the JHS 3 Series Ring Modulator. Its Blend is the
 * footer Mix.
 *
 * Filter is `ee::dsp::AutoWah`, the Peak Wah engine, with its per-note envelope
 * taken out of the picture: Decay is pinned fully up (the engine latches on and
 * the wave just runs) and the tap morph is pinned to low-pass. The module owns
 * the dry/wet, so the engine's own Mix is left fully wet.
 *
 * Bit Crush is `ee::dsp::BitCrusher` - sample-and-hold downsampling, bit-depth
 * quantisation, a post low-pass and hold-clock jitter. It takes the footer Mix
 * like Filter does: its wet does not wander in time, so a fixed dry alongside it
 * combs harmlessly rather than sweeping.
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

    /** Every Bit Crush control in one call. `bits01`, `rate01`, `lp01` and
        `jitter01` are the raw 0..1 knob positions; the maps to real units are
        `ee::dsp::bitcrush`'s, shared with the pedal's printed readouts. */
    void setCrush (float bits01, float rate01, float lp01, float jitter01) noexcept
    {
        crusher.setBits (ee::dsp::bitcrush::bitsFor (bits01));
        crusher.setDecimation (ee::dsp::bitcrush::decimationFactorFor (rate01));
        crusher.setLowpassHz (ee::dsp::bitcrush::lpHzFor (lp01));
        crusher.setJitter01 (jitter01);
    }

    /** Every Ring Mod control in one call. `freq01`, `tweak01` and `lp01` are
        raw 0..1 knob positions resolved through the `ee::dsp::ringmod` maps the
        pedal's readouts share; `mode` is 0 = Earworm, 1 = Green Lantern. The
        Blend is the module's footer Mix, not a control here. */
    void setRing (float freq01, float tweak01, float lp01, int mode) noexcept
    {
        ring.setFrequencyHz (ee::dsp::ringmod::freqHzFor (freq01));
        ring.setTweak01 (tweak01);
        ring.setLowpassHz (ee::dsp::ringmod::lpHzFor (lp01));
        ring.setMode (mode);
    }

    /** The Filter engine's signed cutoff-sweep exponent per channel at the last
        processed sample (Range * gate * lfo) - what the face's response scope
        rides on, the same feed Peak Wah pushes. The engine stays warm even when
        it is not the selected one, so these keep moving; the face only reads
        them while Filter is showing. */
    float filterModL() const noexcept { return wah.modL(); }
    float filterModR() const noexcept { return wah.modR(); }

protected:
    int engineCount() const noexcept override { return NumEngines; }

    /** All three engines are summed against the dry - the footer Mix is their
        Blend. None has a wet path that wanders in time, so none opts out. */
    bool engineUsesMix (int) const noexcept override { return true; }

    void prepareEngines (double sampleRate, int) override
    {
        wah.prepare (sampleRate);

        // Fixed for the life of the engine: latched on, low-pass tap, and the
        // module's dry/wet rather than the engine's own.
        wah.setDecay01 (1.0f);
        wah.setTypeMorph01 (0.0f);
        wah.setMix01 (1.0f);

        crusher.prepare (sampleRate);
        ring.prepare (sampleRate);
    }

    void renderEngine (int index, const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet,
                       int numChannels, int numSamples) noexcept override
    {
        const float* inL = dry.getReadPointer (0);
        const float* inR = dry.getReadPointer (numChannels > 1 ? 1 : 0);

        copyDry (inL, inR, wet, numSamples);

        if (index == Filter)
            wah.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
        else if (index == BitCrush)
            crusher.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
        else if (index == RingMod)
            ring.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
    }

    void resetEngine (int index) noexcept override
    {
        if (index == Filter)
            wah.reset();
        else if (index == BitCrush)
            crusher.reset();
        else if (index == RingMod)
            ring.reset();
    }

private:
    static void copyDry (const float* inL, const float* inR, juce::AudioBuffer<float>& wet, int numSamples) noexcept
    {
        juce::FloatVectorOperations::copy (wet.getWritePointer (0), inL, numSamples);
        juce::FloatVectorOperations::copy (wet.getWritePointer (1), inR, numSamples);
    }

    ee::dsp::AutoWah wah;
    ee::dsp::BitCrusher crusher;
    ee::dsp::RingModulator ring;
};

} // namespace ee::fx
