#pragma once

#include "ee/dsp/Chorus.h"
#include "ee/dsp/Phaser.h"
#include "ee/dsp/TapeMachine.h"
#include "ee/dsp/Tremolo.h"
#include "ee/fx/MultiEngineModule.h"

namespace ee::fx
{

/** Peak Alpine's Modulation module: four engines, one at a time.
 *
 * Each is the same engine its own pedal runs - Peak Tape's machine, Peak Trem &
 * Pan's tremolo, Peak Chorus's chorus, Peak Phase's phaser - so a fix to any of
 * them lands in both places. This class is only which one is selected and what
 * it is set to.
 *
 * Values are per-engine and live in the owner's parameters, not here: switching
 * Chorus to Phaser and back restores the Chorus settings because each engine
 * has its own parameters, not because anything is remembered.
 */
class ModulationModule final : public MultiEngineModule
{
public:
    /** In the order the face steps through them, which is also the order of the
        owner's choice parameter. */
    enum Engine
    {
        Tape = 0,
        Tremolo,
        Chorus,
        Phaser,
        NumEngines
    };

    // -------------------------------------------------------------- the knobs

    void setTape (float saturation01, float flutter01, float wear01, float noise01) noexcept
    {
        tape.setSaturation01 (saturation01);
        tape.setFlutter01 (flutter01);
        tape.setWear01 (wear01);
        tape.setNoise01 (noise01);
    }

    /** `periodSeconds` rather than a rate knob: the mapping from a knob to a
        period is the owner's, and when synced it needs a tempo this module has
        no business reading. `transport` is the same - see ee::dsp::Tremolo. */
    void setTremolo (float amount01, float periodSeconds, float shape01, float tube01) noexcept
    {
        tremolo.setAmount01 (amount01);
        tremolo.setPeriodSeconds (periodSeconds);
        tremolo.setShape01 (shape01);
        tremolo.setBias01 (tube01);
    }

    void setTremoloTransport (const ee::dsp::Tremolo::Transport& t) noexcept { transport = t; }

    void setChorus (float rateHz, float depth01, float phaseDegrees) noexcept
    {
        chorus.setRateHz (rateHz);
        chorus.setDepth01 (depth01);
        chorus.setPhaseDegrees (phaseDegrees);
    }

    void setPhaser (float rateHz, float depth01) noexcept
    {
        phaser.setRateHz (rateHz);
        phaser.setDepth01 (depth01);
    }

protected:
    int engineCount() const noexcept override { return NumEngines; }

    void prepareEngines (double sampleRate, int maxBlockSize) override
    {
        tape.prepare (sampleRate);
        tremolo.prepare (sampleRate, maxBlockSize);
        chorus.prepare (sampleRate);
        phaser.prepare (sampleRate);

        // The module owns the dry/wet, so the chorus runs fully wet and its own
        // internal blend stays out of the way. Peak Chorus keeps its Mix knob;
        // here that knob is the module's, one level up.
        chorus.setMix01 (1.0f);

        // The tremolo's panning mode is Peak Trem & Pan's, not this module's:
        // the face has four knobs and no Tremolo/Panning switch, so it is fixed
        // on the tremolo law.
        tremolo.setPanning (false);
    }

    /** Both buffers are always kMaxChannels wide, so every engine renders two
        channels whatever the host's bus is - a mono host reads channel 0 twice
        on the way in, and the mix upstream simply ignores the right side on the
        way out. Passing the same pointer as both outputs would have the right
        channel write over the left mid-block, which is a stereo engine
        silently producing something that is neither. */
    void renderEngine (int index, const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet,
                       int numChannels, int numSamples) noexcept override
    {
        const float* inL = dry.getReadPointer (0);
        const float* inR = dry.getReadPointer (numChannels > 1 ? 1 : 0);

        // Tape and Tremolo work in place, so they get a copy to chew on; Chorus
        // and Phaser read one buffer and write another, which is what `wet` is.
        switch (index)
        {
            case Tape:
                copyDry (inL, inR, wet, numSamples);
                tape.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
                break;

            case Tremolo:
                copyDry (inL, inR, wet, numSamples);
                tremolo.process (wet, kMaxChannels, numSamples, transport);
                break;

            case Chorus:
                chorus.process (inL, inR, wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
                break;

            case Phaser:
                phaser.process (inL, inR, wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
                break;

            default:
                copyDry (inL, inR, wet, numSamples);
                break;
        }
    }

    /** The one engine here with any. TapeMachine reads its whole output off a
        transport delay line - 4.5 ms of capstan plus the tape stage's own - so
        without this the module would be mixing a 9.7 ms copy of the signal
        against the dry, which is a comb rather than a tape machine. See
        MultiEngineModule's note. */
    int engineLatencySamples (int index) const noexcept override
    {
        return index == Tape ? tape.getLatencySamples() : 0;
    }

    void resetEngine (int index) noexcept override
    {
        switch (index)
        {
            case Tape: tape.reset(); break;
            case Tremolo: tremolo.reset(); break;
            case Chorus: chorus.reset(); break;
            case Phaser: phaser.reset(); break;
            default: break;
        }
    }

private:
    static void copyDry (const float* inL, const float* inR, juce::AudioBuffer<float>& wet, int numSamples) noexcept
    {
        juce::FloatVectorOperations::copy (wet.getWritePointer (0), inL, numSamples);
        juce::FloatVectorOperations::copy (wet.getWritePointer (1), inR, numSamples);
    }

    ee::dsp::TapeMachine tape;
    ee::dsp::Tremolo tremolo;
    ee::dsp::Chorus chorus;
    ee::dsp::Phaser phaser;

    ee::dsp::Tremolo::Transport transport;
};

} // namespace ee::fx
