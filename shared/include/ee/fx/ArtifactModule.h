#pragma once

#include "ee/dsp/AutoWah.h"
#include "ee/dsp/BitCrusher.h"
#include "ee/dsp/RingModulator.h"
#include "ee/dsp/Rust.h"
#include "ee/dsp/TubeDrive.h"
#include "ee/fx/MultiEngineModule.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

namespace ee::fx
{

/** Peak Artifact's module: five engines, one at a time.
 *
 * All five are voiced. Each takes the footer Mix as its dry/wet. Rust's warble
 * has a wet path that wanders in time, but only under wear and gently - it is
 * wow, the intended movement, not the tremolo artefact `engineUsesMix` guards
 * Tape against - so it too keeps the Mix.
 *
 * Ring Mod is `ee::dsp::RingModulator` - a sine-carrier ring modulator with a
 * post low-pass and two voicings (Earworm's carrier wobble, Green Lantern's
 * octave blend), voiced from the JHS 3 Series Ring Modulator. Its Blend is the
 * footer Mix.
 *
 * Rust is `ee::dsp::Rust` - degradation with a memory. A per-channel wear state
 * follows the recent input level and heals back when it stops, driving a
 * corrosion chain (warble, grit, bit/rate crumble, crackle, dropouts) whose
 * depth is wear * Grind. Two voicings: Oxide (a decaying magnetic coating) and
 * Contact (a failing jack). Its dry/wet is the footer Mix.
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
 * Amp is a driven, degraded voice built from `ee::dsp::TubeDrive` (an
 * asymmetric analog drive stage, deliberately not the same shape as Peak
 * Overdrive's diode clipper - unchanged since it first shipped) into a second
 * `ee::dsp::BitCrusher` instance repurposed for one thing only: sample-rate
 * reduction. Its Bit knob's calibration (kAmpCrushRateHz and the blend) comes
 * from measuring a real reference unit against a dry recording of the same
 * material at 50 % and 100 % - see the note on setAmp. There is no bit-depth
 * quantisation on this engine, and no anti-alias filter ahead of the hold: the
 * reference's output carries no amplitude lattice at all (it holds full
 * resolution), its added content scales exactly 1:1 with the signal over a
 * 56 dB range, and its spectrum nulls at exact multiples of 1500 Hz - the
 * signature of a zero-order hold, not of a quantiser. A quantiser was the
 * first cut here and it was wrong twice over: it gates a note's tail to
 * digital silence once the signal falls below one step, and its noise floor
 * is fixed rather than following the playing.
 *
 * After the crush, a peaking Mids boost (the same centre frequency and Q Peak
 * EQ's own Mid band uses) and a bipolar Tone tilt (Peak Tape's, resting flat
 * and bypassed dead centre) shape the result, and a fixed short delay on the
 * right channel (kAmpHaasDelayMs) widens it when Stereo is on - a Haas trick,
 * not a real stereo signal. Its dry/wet is the footer Mix, like Bit Crush.
 *
 * Values are the owner's parameters, not state here - see ModulationModule.
 */
class ArtifactModule final : public MultiEngineModule
{
public:
    /** In the order the face steps through them, which is also the order of the
        owner's choice parameter. Amp is appended after Rust rather than sorted
        in among the others so an existing session's saved engine index keeps
        meaning what it meant before this engine existed. */
    enum Engine
    {
        RingMod = 0,
        BitCrush,
        Filter,
        Rust,
        Amp,
        NumEngines
    };

    /** Amp's Bit knob is a sample-rate reducer: hold N input samples, where N
        runs 1 (no effect) to the host rate over kAmpCrushRateHz. Measured, not
        assumed - the reference's spectrum nulls at exact multiples of 1500 Hz
        at the top of its travel and of 9600 Hz at half, which is N = 32 and
        N = 5 at 48 kHz. kAmpCrushRateSkew is what puts 5 under the middle of
        the knob given 32 at the end. Expressed in Hz rather than as a sample
        count so the voicing is the same rate at 44.1 k and 96 k. */
    static constexpr float kAmpCrushRateHz = 1500.0f;
    static constexpr float kAmpCrushRateSkew = 1.1f;

    /** Where the Bit knob rests, 0..100. The reference's own half travel: a
        hold of 5 at 48 kHz, audibly lo-fi without burying the note. */
    static constexpr float kAmpDefaultBitPct = 50.0f;

    /** The held path runs against the un-held one at a fixed blend - not a
        knob, and not something the Bit knob moves: the reference holds the
        same 0.50 / 0.53 ratio at half travel as at full, and only the rate
        changes. The pair sums a little over unity (+0.31 dB); that is the
        reference's own make-up and it is kept, so a render nulls against it. */
    static constexpr float kAmpCrushDryGain = 0.5015f;
    static constexpr float kAmpCrushWetGain = 0.5343f;

    /** A 2-pole anti-imaging low-pass on the held path only. The reference's
        images roll off from about here upwards; without it our top octave runs
        5-14 dB hot against it while everything below 14 kHz already matches
        inside 0.4 dB. Bypassed when the hold is passing every sample, so the
        knob at rest changes nothing. */
    static constexpr float kAmpCrushImageHz = 14000.0f;

    /** Amp's Mids knob (0..1) -> 0..kAmpMidsMaxDb of peaking boost at
        kAmpMidsFreqHz / kAmpMidsQ - Peak EQ's own centre band and Q for its
        "MID" group (`PeakEqProcessor::kBandFrequencies[4]`, `kBandQ`), so the
        lift reads as the same boost rather than a face of its own. */
    static constexpr float kAmpMidsFreqHz = 1600.0f;
    static constexpr float kAmpMidsQ = 1.4f;
    static constexpr float kAmpMidsMaxDb = 4.0f;

    /** Amp's Tone is a tilt around kAmpTonePivotHz on a knob that rests dead
        centre: -1 leans into the lows, 0 is flat and the stage is bypassed
        exactly, +1 leans into the highs. Peak Tape's numbers
        (`ee::dsp::tape::kTone*`), restated rather than included so this
        engine's voicing is in one place - a bipolar tone knob should read the
        same wherever the user meets one. */
    static constexpr float kAmpTonePivotHz = 700.0f;
    static constexpr float kAmpToneLowGainDark = 1.90f;
    static constexpr float kAmpToneLowGainBright = 0.52f;
    static constexpr float kAmpToneHighGainDark = 0.52f;
    static constexpr float kAmpToneHighGainBright = 1.90f;

    /** The Haas widener's fixed delay on the right channel when Amp's Stereo
        switch is on. Short enough that the ear fuses it with the left channel
        as one sound coming from the side rather than as a discrete echo - see
        setAmp. Not a knob: the switch is Mono/Stereo, not a delay time. */
    static constexpr float kAmpHaasDelayMs = 15.0f;

    /** Bit knob (0..1) -> the sample-and-hold rate in Hz at `sampleRate`. Knob
        0 returns the host rate, where the hold passes every sample. Public so
        the host-facing text formatter reads the same map `setAmp` does rather
        than keeping its own copy of the formula. */
    static float ampRateHzFor (float bit01, double sampleRate) noexcept
    {
        const float host = static_cast<float> (sampleRate > 0.0 ? sampleRate : 48000.0);
        const float floorHz = juce::jmin (kAmpCrushRateHz, host);
        const float t = std::pow (juce::jlimit (0.0f, 1.0f, bit01), kAmpCrushRateSkew);
        return host * std::pow (floorHz / host, t);
    }

    // -------------------------------------------------------------- the knobs

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

    /** Every Amp control in one call. `drive01` is the raw 0..1 Drive knob,
        fed straight to `ee::dsp::TubeDrive`, unchanged from the engine's
        first cut. `mids01` is 0..1 across the 0..kAmpMidsMaxDb peaking boost
        at kAmpMidsFreqHz / kAmpMidsQ. `bit01` is sample-rate reduction only
        (ampRateHzFor) - no bit-depth quantisation, no anti-alias filter, see
        the class note. `tone` is bipolar, -1..1, and rests flat at 0.
        `stereo` switches the Haas widener on the right channel on or off. */
    void setAmp (float drive01, float mids01, float bit01, float tone, bool stereo) noexcept
    {
        ampDrive.setDrive01 (drive01);

        const float rateHz = ampRateHzFor (bit01, ampSampleRate);
        const int holdN = static_cast<int> (std::lround (ampSampleRate / std::max (1.0, static_cast<double> (rateHz))));

        ampCrusher.setBits (ee::dsp::bitcrush::kBitsClean);
        ampCrusher.setDecimation (holdN);
        ampCrusher.setJitter01 (0.0f);
        ampCrusher.setAntiAlias (false);
        ampCrusher.setLowpassHz (holdN > 1 ? kAmpCrushImageHz : ee::dsp::bitcrush::kLpMaxHz);

        const float midsGain = juce::Decibels::decibelsToGain (mids01 * kAmpMidsMaxDb);
        ampMidsCoeffs =
            juce::dsp::IIR::Coefficients<float>::makePeakFilter (ampSampleRate, kAmpMidsFreqHz, kAmpMidsQ, midsGain);
        for (auto& f : ampMids)
            f.coefficients = ampMidsCoeffs;

        // Exactly centred is flat and bypassed; a hair off it is not, so there
        // is no dead band around the middle. The knob snaps onto the centre in
        // the UI, which is what makes that usable.
        const float tilt01 = 0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, tone);
        ampToneEngaged = tone != 0.0f;
        ampToneLowGain = juce::jmap (tilt01, kAmpToneLowGainDark, kAmpToneLowGainBright);
        ampToneHighGain = juce::jmap (tilt01, kAmpToneHighGainDark, kAmpToneHighGainBright);

        ampStereo = stereo;
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

    /** Every Rust control in one call. `grind01` and `tone01` are raw 0..1 knob
        positions; `mode` is 0 = Oxide, 1 = Contact. Wear and its recovery are
        fixed inside the engine - there is no knob for them. The dry/wet is the
        module's footer Mix. */
    void setRust (float grind01, float tone01, int mode) noexcept
    {
        rust.setGrind01 (grind01);
        rust.setTone01 (tone01);
        rust.setMode (mode);
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

    /** All five engines are summed against the dry - the footer Mix is their
        Blend. Rust's warble wanders in time but only gently and only under
        wear, so it keeps the Mix like the rest (see the class note). */
    bool engineUsesMix (int) const noexcept override { return true; }

    void prepareEngines (double sampleRate, int maxBlock) override
    {
        wah.prepare (sampleRate);

        // Fixed for the life of the engine: latched on, low-pass tap, and the
        // module's dry/wet rather than the engine's own.
        wah.setDecay01 (1.0f);
        wah.setTypeMorph01 (0.0f);
        wah.setMix01 (1.0f);

        crusher.prepare (sampleRate);
        ring.prepare (sampleRate);
        rust.prepare (sampleRate);

        ampSampleRate = sampleRate;
        ampDrive.prepare (sampleRate);
        ampCrusher.prepare (sampleRate);
        ampCrushed.setSize (2, juce::jmax (1, maxBlock), false, true, true);
        ampCrushed.clear();
        for (auto& f : ampMids)
            f.reset();
        ampToneCoeff = onePoleCoeff (kAmpTonePivotHz, sampleRate);
        ampToneLp.fill (0.0f);
        ampHaas.prepare (1, static_cast<int> (kAmpHaasDelayMs * 0.001 * sampleRate));
    }

    void renderEngine (int index,
                       const juce::AudioBuffer<float>& dry,
                       juce::AudioBuffer<float>& wet,
                       int numChannels,
                       int numSamples) noexcept override
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
        else if (index == Rust)
            rust.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);
        else if (index == Amp)
        {
            ampDrive.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples);

            // The held path runs alongside the un-held one rather than
            // replacing it - see kAmpCrushDryGain. At rest the crusher passes
            // every sample and the two are the same signal.
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::copy (ampCrushed.getWritePointer (ch), wet.getReadPointer (ch),
                                                   numSamples);

            ampCrusher.process (ampCrushed.getWritePointer (0), ampCrushed.getWritePointer (1), numSamples);

            for (int ch = 0; ch < 2; ++ch)
            {
                float* out = wet.getWritePointer (ch);
                const float* held = ampCrushed.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    out[i] = kAmpCrushDryGain * out[i] + kAmpCrushWetGain * held[i];
            }

            juce::dsp::AudioBlock<float> block (wet.getArrayOfWritePointers(), 2, static_cast<size_t> (numSamples));
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto chBlock = block.getSingleChannelBlock (ch);
                juce::dsp::ProcessContextReplacing<float> ctx (chBlock);
                ampMids[ch].process (ctx);
            }

            if (ampToneEngaged)
            {
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    float* d = wet.getWritePointer (static_cast<int> (ch));
                    float lp = ampToneLp[ch];
                    for (int i = 0; i < numSamples; ++i)
                    {
                        lp += ampToneCoeff * (d[i] - lp);
                        d[i] = lp * ampToneLowGain + (d[i] - lp) * ampToneHighGain;
                    }
                    ampToneLp[ch] = lp;
                }
            }

            if (ampStereo)
            {
                float* rPtr = wet.getWritePointer (1);
                juce::AudioBuffer<float> rView (&rPtr, 1, numSamples);
                ampHaas.process (rView, 1, numSamples);
            }
        }
    }

    void resetEngine (int index) noexcept override
    {
        if (index == Filter)
            wah.reset();
        else if (index == BitCrush)
            crusher.reset();
        else if (index == RingMod)
            ring.reset();
        else if (index == Rust)
            rust.reset();
        else if (index == Amp)
        {
            ampDrive.reset();
            ampCrusher.reset();
            ampCrushed.clear();
            for (auto& f : ampMids)
                f.reset();
            ampToneLp.fill (0.0f);
            ampHaas.reset();
        }
    }

private:
    static float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = 2.0f * juce::MathConstants<float>::pi * cornerHz / static_cast<float> (sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    static void copyDry (const float* inL, const float* inR, juce::AudioBuffer<float>& wet, int numSamples) noexcept
    {
        juce::FloatVectorOperations::copy (wet.getWritePointer (0), inL, numSamples);
        juce::FloatVectorOperations::copy (wet.getWritePointer (1), inR, numSamples);
    }

    ee::dsp::AutoWah wah;
    ee::dsp::BitCrusher crusher;
    ee::dsp::RingModulator ring;
    ee::dsp::Rust rust;

    ee::dsp::TubeDrive ampDrive;
    ee::dsp::BitCrusher ampCrusher;
    juce::AudioBuffer<float> ampCrushed;
    double ampSampleRate = 44100.0;
    std::array<juce::dsp::IIR::Filter<float>, 2> ampMids;
    juce::dsp::IIR::Coefficients<float>::Ptr ampMidsCoeffs;
    bool ampToneEngaged = false;
    float ampToneCoeff = 0.0f;
    float ampToneLowGain = 1.0f;
    float ampToneHighGain = 1.0f;
    std::array<float, 2> ampToneLp { 0.0f, 0.0f };
    bool ampStereo = false;
    AlignDelay ampHaas;
};

} // namespace ee::fx
