#pragma once

#include "ee/dsp/AutoWah.h"
#include "ee/dsp/Chorus.h"
#include "ee/dsp/Phaser.h"
#include "ee/dsp/TapeMachine.h"
#include "ee/dsp/Tremolo.h"
#include "ee/fx/MultiEngineModule.h"

namespace ee::fx
{

/** BitBit Alpine's Modulation module: five engines, one at a time.
 *
 * Each is the same engine its own pedal runs - BitBit Tape's machine, BitBit Trem &
 * Pan's tremolo, BitBit Chorus's chorus, BitBit Phase's phaser, BitBit Wah's filter -
 * so a fix to any of them lands in both places. This class is only which one is
 * selected and what it is set to.
 *
 * Filter is `ee::dsp::AutoWah` with its per-note envelope taken out of the
 * picture: Decay is pinned fully up (the engine latches on and the wave just
 * runs) and the tap morph is pinned to low-pass. What is left is an LFO-swept
 * filter, which is modulation rather than an artefact - it lived in BitBit
 * Artifact until it moved here. The module owns the dry/wet, so the engine's own
 * Mix is left fully wet.
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
        Filter, // appended, so a saved engine index keeps meaning what it did
        NumEngines
    };

    // -------------------------------------------------------------- the knobs

    void setTape (float saturation01, float flutter01, float wear01, float noise01, float tone, float stereo01) noexcept
    {
        tape.setSaturation01 (saturation01);
        tape.setFlutter01 (flutter01);
        tape.setWear01 (wear01);
        tape.setNoise01 (noise01);
        tape.setTone (tone);
        tape.setStereo01 (stereo01);
    }

    /** Hands the tape engine a recording of a tape floor to loop, the same one
        BitBit Tape plays - without it the Noise knob is the synthesised hiss
        fallback rather than the real floor. Pointers are not owned; the caller
        keeps the samples alive for as long as the module runs. Safe before
        prepare(). See ee::dsp::TapeMachine::setNoiseSample. */
    void setTapeNoiseSample (const float* const* channelData, int numChannels,
                             int numSamples, double sampleRateOfSample) noexcept
    {
        tape.setNoiseSample (channelData, numChannels, numSamples, sampleRateOfSample);
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

    /** Every Filter control in one call, in the units the engine takes. `freq01`,
        `q01`, `range01` and `waveShape01` are 0..1; `periodSeconds` is one LFO
        cycle, already resolved from the Time knob and the Sync switch by the
        owner. Decay and tap morph are not offered - see the class note. */
    void
    setFilter (float freq01, float q01, float range01, float waveShape01, float periodSeconds, bool stereo) noexcept
    {
        wah.setFreq01 (freq01);
        wah.setQ01 (q01);
        wah.setRange01 (range01);
        wah.setShape01 (waveShape01);
        wah.setPeriodSeconds (juce::jmax (1.0e-4f, periodSeconds));
        wah.setStereo (stereo);
    }

    /** The LFO free-runs; when the owner is synced to a running transport it
        also aligns the phase to the host grid, exactly as BitBit Wah does. */
    void snapFilterPhase (double target01) noexcept { wah.snapPhase (target01); }
    void nudgeFilterPhase (double target01) noexcept { wah.nudgePhase (target01); }

    /** The Filter engine's signed cutoff-sweep exponent per channel at the last
        processed sample (Range * gate * lfo) - what the face's response scope
        rides on, the same feed BitBit Wah pushes. The engine stays warm even when
        it is not the selected one, so these keep moving; the face only reads
        them while Filter is showing. */
    float filterModL() const noexcept { return wah.modL(); }
    float filterModR() const noexcept { return wah.modR(); }

protected:
    int engineCount() const noexcept override { return NumEngines; }

    /** Tape rides a transport delay line, so its wet output wanders in time with
        the wow. Any partial Mix against a static dry is a comb whose notch
        sweeps at the wow rate - audible as tremolo, clean only at the ends. So
        Tape is not offered a Mix at all: it runs fully wet, matching BitBit Tape,
        which has no mix control either, and the face drops the Mix knob for it.
        The other engines are unchanged. */
    bool engineUsesMix (int index) const noexcept override { return index != Tape; }

    void prepareEngines (double sampleRate, int maxBlockSize) override
    {
        // The short transport, always: 1.85 ms instead of 4.5, which is most of
        // this module's latency (3.35 ms rather than 6). It is as short as the
        // wow fits in at full mono depth; Flutter and Stereo together are pulled
        // back only as far as the short line needs. See
        // tape::kLowLatencyNominalDelayMs.
        tape.setLowLatency (true);
        tape.prepare (sampleRate);
        tremolo.prepare (sampleRate, maxBlockSize);
        chorus.prepare (sampleRate);
        phaser.prepare (sampleRate);
        wah.prepare (sampleRate);

        // Fixed for the life of the engine: latched on, low-pass tap, and the
        // module's dry/wet rather than the engine's own.
        wah.setDecay01 (1.0f);
        wah.setTypeMorph01 (0.0f);
        wah.setMix01 (1.0f);

        // The module owns the dry/wet, so the chorus runs fully wet and its own
        // internal blend stays out of the way. BitBit Chorus keeps its Mix knob;
        // here that knob is the module's, one level up.
        chorus.setMix01 (1.0f);

        // The tremolo's panning mode is BitBit Trem & Pan's, not this module's:
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

        // Tape, Tremolo and Filter work in place, so they get a copy to chew on;
        // Chorus and Phaser read one buffer and write another, which is what
        // `wet` is.
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

            case Filter:
                copyDry (inL, inR, wet, numSamples);
                wah.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
                break;

            default:
                copyDry (inL, inR, wet, numSamples);
                break;
        }
    }

    /** The one engine here with any. TapeMachine reads its whole output off a
        transport delay line - 1.85 ms of capstan plus the tape stage's own - so
        without this the module would be mixing a copy of the signal that is
        that much late against the dry, which is a comb rather than a tape
        machine. See MultiEngineModule's note. */
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
            case Filter: wah.reset(); break;
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
    ee::dsp::AutoWah wah;

    ee::dsp::Tremolo::Transport transport;
};

} // namespace ee::fx
