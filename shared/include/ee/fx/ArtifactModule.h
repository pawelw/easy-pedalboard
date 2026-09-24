#pragma once

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

/** BitBit Artifact's module: four engines, one at a time.
 *
 * All four are voiced. Each takes the footer Mix as its dry/wet. Rust's warble
 * has a wet path that wanders in time, but only under wear and gently - it is
 * wow, the intended movement, not the tremolo artefact `engineUsesMix` guards
 * Tape against - so it too keeps the Mix.
 *
 * Ring Mod is `ee::dsp::RingModulator` - a sine-carrier ring modulator with a
 * post low-pass, a bipolar carrier Rectify and two voicings (Earworm's carrier
 * wobble, Green Lantern's octave blend), voiced from the JHS 3 Series Ring
 * Modulator. Its Blend is the footer Mix.
 *
 * Rust is `ee::dsp::Rust` - degradation with a memory. A per-channel wear state
 * follows the recent input level and heals back when it stops, driving a
 * corrosion chain (warble, grit, bit/rate crumble, crackle, dropouts) whose
 * depth is wear * Grind. Two voicings: Oxide (a decaying magnetic coating) and
 * Contact (a failing jack). Its dry/wet is the footer Mix.
 *
 * There was a fifth, Filter (BitBit Wah's swept filter), until it moved to
 * ModulationModule - an LFO sweep is modulation, not an artefact.
 *
 * Bit Crush is `ee::dsp::BitCrusher` - sample-and-hold downsampling, bit-depth
 * quantisation, a post low-pass and hold-clock jitter. It takes the footer Mix:
 * its wet does not wander in time, so a fixed dry alongside it combs harmlessly
 * rather than sweeping.
 *
 * Amp is a driven, degraded voice built from `ee::dsp::TubeDrive` (an
 * emphasis distortion - pre-emphasis, a logarithmic curve, de-emphasis -
 * fitted to a real reference unit at full drive, see TubeDriveConfig.h, and
 * deliberately not BitBit Overdrive's diode clipper) into a second
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
 * After the crush, a peaking Mids boost (the same centre frequency and Q BitBit
 * EQ's own Mid band uses) and a bipolar Tone tilt (BitBit Tape's, resting flat
 * and bypassed dead centre) shape the result. Its dry/wet is the footer Mix, like
 * Bit Crush. (It used to have a Mono/Stereo switch that put a fixed 15 ms delay on
 * the right channel - a Haas trick, not a real stereo signal. It was taken out; the
 * Amp is mono in, mono out, like the input.)
 *
 * Values are the owner's parameters, not state here - see ModulationModule.
 */
class ArtifactModule final : public MultiEngineModule
{
public:
    /** In the order the face steps through them, which is also the order of the
        owner's choice parameter. Filter used to sit between Bit Crush and Rust;
        taking it out moved Rust and Amp down one. */
    enum Engine
    {
        RingMod = 0,
        BitCrush,
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
        kAmpMidsFreqHz / kAmpMidsQ - BitBit EQ's own centre band and Q for its
        "MID" group (`BitBitEqProcessor::kBandFrequencies[4]`, `kBandQ`), so the
        lift reads as the same boost rather than a face of its own. */
    static constexpr float kAmpMidsFreqHz = 1600.0f;
    static constexpr float kAmpMidsQ = 1.4f;
    static constexpr float kAmpMidsMaxDb = 7.0f;

    /** Amp's Tone is a tilt around kAmpTonePivotHz on a knob that rests dead
        centre: -1 leans into the lows, 0 is flat and the stage is bypassed
        exactly, +1 leans into the highs. BitBit Tape's numbers
        (`ee::dsp::tape::kTone*`), restated rather than included so this
        engine's voicing is in one place - a bipolar tone knob should read the
        same wherever the user meets one. */
    static constexpr float kAmpTonePivotHz = 700.0f;
    static constexpr float kAmpToneLowGainDark = 1.90f;
    static constexpr float kAmpToneLowGainBright = 0.52f;
    static constexpr float kAmpToneHighGainDark = 0.52f;
    static constexpr float kAmpToneHighGainBright = 1.90f;
    static constexpr float kAmpToneCentreEpsilon = 1.0e-4f;

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
        fed straight to `ee::dsp::TubeDrive`, whose 100 % is the matched
        reference. `mids01` is 0..1 across the 0..kAmpMidsMaxDb peaking boost
        at kAmpMidsFreqHz / kAmpMidsQ. `bit01` is sample-rate reduction only
        (ampRateHzFor) - no bit-depth quantisation, no anti-alias filter, see
        the class note. `tone` is bipolar, -1..1, and rests flat at 0. */
    void setAmp (float drive01, float mids01, float bit01, float tone) noexcept
    {
        ampDrive.setDrive01 (drive01);

        const float rateHz = ampRateHzFor (bit01, ampSampleRate);
        const int holdN = static_cast<int> (std::lround (ampSampleRate / std::max (1.0, static_cast<double> (rateHz))));

        ampCrusher.setBits (ee::dsp::bitcrush::kBitsClean);
        ampCrusher.setDecimation (holdN);
        ampCrusher.setJitter01 (0.0f);
        ampCrusher.setAntiAlias (false);
        ampCrusher.setLowpassHz (holdN > 1 ? kAmpCrushImageHz : ee::dsp::bitcrush::kLpMaxHz);

        // setAmp runs every block (pushSettings does, whether or not the knob
        // moved), and Coefficients<float>::makePeakFilter allocates a new
        // Coefficients object every call - real-time-unsafe on the audio
        // thread, and exactly what ee_soak_BitBitArtifact's allocation guard
        // caught. ArrayCoefficients does the identical maths into a
        // stack-returned std::array with no allocation at all; assigning it
        // onto an *existing* Coefficients object reuses that object's array
        // storage (Coefficients::assignImpl clearQuick()s and
        // ensureStorageAllocated()s rather than reallocating) once it has
        // been sized once. Only the first call, before ampMidsCoeffs exists,
        // still goes through the allocating factory.
        const float midsGain = juce::Decibels::decibelsToGain (mids01 * kAmpMidsMaxDb);

        if (ampMidsCoeffs == nullptr)
        {
            ampMidsCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (ampSampleRate, kAmpMidsFreqHz,
                                                                                 kAmpMidsQ, midsGain);
            for (auto& f : ampMids)
                f.coefficients = ampMidsCoeffs;
        }
        else
        {
            *ampMidsCoeffs = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (ampSampleRate, kAmpMidsFreqHz,
                                                                                       kAmpMidsQ, midsGain);
        }

        // Centred is flat and bypassed; anything off it is not. "Centred" has a
        // hair of room - a Tone set to the middle through a host or the face
        // is a normalised 0.5 run through JUCE's interval rounding, which lands
        // a few millionths off zero, and that must not switch the tilt on (it
        // runs at a flat +1.7 dB). The knob's own step is 0.1 %, so nothing
        // reachable is lost. Same reason and same figure as
        // ee::dsp::tape::kToneCentreEpsilon.
        const float tilt01 = 0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, tone);
        ampToneEngaged = std::abs (tone) > kAmpToneCentreEpsilon;
        ampToneLowGain = juce::jmap (tilt01, kAmpToneLowGainDark, kAmpToneLowGainBright);
        ampToneHighGain = juce::jmap (tilt01, kAmpToneHighGainDark, kAmpToneHighGainBright);
    }

    /** Every Ring Mod control in one call. `freq01`, `tweak01` and `lp01` are
        raw 0..1 knob positions resolved through the `ee::dsp::ringmod` maps the
        pedal's readouts share; `rectify` is bipolar, -1..+1, resting at 0; `mode`
        is 0 = Earworm, 1 = Green Lantern. The Blend is the module's footer Mix,
        not a control here. */
    void setRing (float freq01, float tweak01, float lp01, float rectify, int mode) noexcept
    {
        ring.setFrequencyHz (ee::dsp::ringmod::freqHzFor (freq01));
        ring.setTweak01 (tweak01);
        ring.setLowpassHz (ee::dsp::ringmod::lpHzFor (lp01));
        ring.setRectify (rectify);
        ring.setMode (mode);
    }

    /** Every Rust control in one call. `grind01` is the raw 0..1 knob
        position; `mode` is 0 = Oxide, 1 = Contact. Wear and its recovery are
        fixed inside the engine - there is no knob for them - and so is its post
        low-pass, at `ee::dsp::rust::kDefaultTonePct` (see prepareEngines): the
        module's footer Tone does that job for every engine. The dry/wet is the
        module's footer Mix. */
    void setRust (float grind01, int mode) noexcept
    {
        rust.setGrind01 (grind01);
        rust.setMode (mode);
    }

protected:
    int engineCount() const noexcept override { return NumEngines; }

    /** All four engines are summed against the dry - the footer Mix is their
        Blend. Rust's warble wanders in time but only gently and only under
        wear, so it keeps the Mix like the rest (see the class note). */
    bool engineUsesMix (int) const noexcept override { return true; }

    void prepareEngines (double sampleRate, int maxBlock) override
    {
        crusher.prepare (sampleRate);
        ring.prepare (sampleRate);

        // Where Rust's own Tone knob rested before it was taken off the face,
        // so an untouched Rust sounds exactly as it did. It tames the grit's
        // fizz; the footer Tone is the knob for anything more. Before prepare,
        // which snaps the filter to it rather than gliding down from open.
        rust.setTone01 (ee::dsp::rust::kDefaultTonePct * 0.01f);
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

        if (index == BitCrush)
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
        }
    }

    void resetEngine (int index) noexcept override
    {
        if (index == BitCrush)
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
};

} // namespace ee::fx
