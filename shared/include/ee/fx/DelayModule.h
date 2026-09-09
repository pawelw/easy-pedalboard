#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

#include "ee/dsp/Phaser.h"
#include "ee/dsp/PhaserConfig.h"
#include "ee/dsp/TapeCharacter.h"
#include "ee/dsp/TapeDelay.h"
#include "ee/dsp/TapeTransport.h"
#include "ee/fx/DelayModuleConfig.h"

namespace ee::fx
{

/** Peak Delay's whole chain, as one object.
 *
 * `ee::dsp::TapeDelay` is only the delay line. What makes the pedal is
 * everything around it - a tape machine that can sit either side of the line, a
 * filter pair on the repeats, a phaser after them, the routing, the dry/wet law
 * and the trims - and all of that used to live in PeakDelayProcessor. It is
 * here so Peak Alpine's Delay module can be a second instance rather than a
 * second copy.
 *
 * Above ee::dsp rather than in it: this is a composition of engines with an
 * opinion about their order, which is a different kind of thing from an engine.
 * Hence ee::fx.
 *
 * Everything is set in real units - seconds, hertz, linear gain - because a
 * module has no idea what a knob is. Parameter ranges, skews, tempo maps and
 * anything that reads a playhead stay with whoever owns the parameters.
 *
 * Requires juce_dsp for the filter pair; the consumer links it (ee_dsp does
 * not, and this is header-only).
 */
class DelayModule
{
public:
    static constexpr int kMaxChannels = 2;

    // ------------------------------------------------------------------ setup

    void prepare (double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        sr = sampleRate;
        maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

        tapeIn.prepare (sampleRate);
        tapeOut.prepare (sampleRate);
        phaser.prepare (sampleRate);
        delay.prepare (sampleRate);

        // One channel per filter object, so each keeps its own state - the same
        // shape Peak EQ prepares its cuts in.
        const juce::dsp::ProcessSpec filterSpec { sampleRate, static_cast<juce::uint32> (maxBlock), 1 };

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hiPass[static_cast<size_t> (ch)].prepare (filterSpec);
            hiPass[static_cast<size_t> (ch)].reset();
            loPass[static_cast<size_t> (ch)].prepare (filterSpec);
            loPass[static_cast<size_t> (ch)].reset();
        }

        updateFilters (true);

        // The phaser runs on Peak Phase's own default voicing - one knob here,
        // so everything but the amount is fixed once and never touched again.
        phaser.setRateHz (ee::dsp::phaser::kDefaultRateHz);
        phaser.setDepth01 (ee::dsp::phaser::kDefaultDepthPct * 0.01f);

        // The post section sits between the delay and the output, so its
        // latency lands on the repeats and would push the first one late by
        // that much. Taken off the delay's own time instead, so the gap the
        // Time knob names is the gap you hear - in either placement, since both
        // are always in circuit.
        postLatencySeconds = static_cast<float> (tapeOut.latencySamples() / sampleRate);

        stageBuffer.setSize (2, maxBlock, false, true, true);
        inputBuffer.setSize (2, maxBlock, false, true, true);
        wetBuffer.setSize (2, maxBlock, false, true, true);
        mixBuffer.setSize (2, maxBlock, false, true, true);
        modBuffer.setSize (2, maxBlock, false, true, true);
        engageRamp.assign (static_cast<size_t> (maxBlock), 0.0f);

        dryGain.reset (sampleRate, delaymodule::kGainRampSeconds);
        wetGain.reset (sampleRate, delaymodule::kGainRampSeconds);
        engageGain.reset (sampleRate, delaymodule::kGainRampSeconds);
        inGain.reset (sampleRate, delaymodule::kGainRampSeconds);
        outGain.reset (sampleRate, delaymodule::kGainRampSeconds);

        tapePlacement.reset (sampleRate, delaymodule::kPlacementSeconds);
        tapePlacement.setCurrentAndTargetValue (tapePost ? 1.0f : 0.0f);
        updateTapeAmounts (tapePlacement.getCurrentValue());
        tapeIn.transport.snapSmoothing();
        tapeOut.transport.snapSmoothing();

        delay.setRouting (routing);
        delay.setFeedback (feedback01);
        delay.setModulation (drift01);
        delay.setDelaySeconds (trimmed (leftSeconds), trimmed (rightSeconds));
        delay.snapDelays();

        dryGain.setCurrentAndTargetValue (dryTarget());
        wetGain.setCurrentAndTargetValue (wetTarget());
        engageGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
        inGain.setCurrentAndTargetValue (inGainLinear);
        outGain.setCurrentAndTargetValue (outGainLinear);
    }

    void reset() noexcept
    {
        tapeIn.reset();
        tapeOut.reset();
        phaser.reset();
        delay.reset();

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hiPass[static_cast<size_t> (ch)].reset();
            loPass[static_cast<size_t> (ch)].reset();
        }
    }

    // -------------------------------------------------------------- the knobs

    /** Both delay times, before the post section's latency is taken off - the
        module does that itself, because how much to take off is a property of
        its own tape stage. */
    void setTimes (float leftSecondsIn, float rightSecondsIn) noexcept
    {
        leftSeconds = leftSecondsIn;
        rightSeconds = rightSecondsIn;
    }

    void setFeedback01 (float v) noexcept { feedback01 = v; }
    void setRouting (ee::dsp::TapeDelay::Routing r) noexcept { routing = r; }

    /** Where the tape section sits: false in front of the delay, true on its
        repeats. A target, not a switch - the module glides between them. */
    void setTapePost (bool post) noexcept { tapePost = post; }
    void setTape (float wear01In, float flutter01In) noexcept
    {
        wear01 = wear01In;
        flutter01 = flutter01In;
    }

    /** Drift: the delay line's own modulation, inside the feedback path so it
        compounds with every repeat rather than being applied once the way an
        insert would be. That compounding is the whole point of the knob, and it
        is why there is no placement control for it - a stage inside the loop has
        no side to be on. */
    void setDrift01 (float v) noexcept { drift01 = v; }

    /** The phaser's blend, on the repeats only. At 0 the stage is skipped and
        the wet path is left bit exact. */
    void setPhaser01 (float v) noexcept { phaser01 = juce::jlimit (0.0f, 1.0f, v); }

    /** The Filter section, in hertz. Each cut is skipped entirely at its
        resting end - see runFilter. */
    void setFilter (float loCutHzIn, float hiCutHzIn) noexcept
    {
        pendingLoCutHz = loCutHzIn;
        pendingHiCutHz = hiCutHzIn;
    }

    /** The travel each cut rests at, which is where it stops being run. The
        owner's parameter ranges decide these, so it hands them over rather than
        the module guessing. */
    void setFilterRestingPoints (float loCutMinHzIn, float hiCutMaxHzIn) noexcept
    {
        loCutMinHz = loCutMinHzIn;
        hiCutMaxHz = hiCutMaxHzIn;
    }

    void setMix01 (float v) noexcept { mix01 = juce::jlimit (0.0f, 1.0f, v); }

    /** Engaged, or bypassed with trails: bypassing closes the delay's input and
        fades the tape off the dry path, but leaves the repeats running out. */
    void setEngaged (bool v) noexcept { engaged = v; }

    /** Input trim and output level, linear. Both sit inside the engage
        crossfade, so a bypassed module is unity whatever they say. */
    void setTrims (float inLinear, float outLinear) noexcept
    {
        inGainLinear = inLinear;
        outGainLinear = outLinear;
    }

    // ------------------------------------------------------------------ audio

    /** In place. `numIn`/`numOut` are the caller's real channel counts; the
        module reads a mono input on both sides and folds a stereo result down
        for a mono output rather than throwing a side away. */
    void process (juce::AudioBuffer<float>& buffer, int numIn, int numOut, int numSamples) noexcept
    {
        if (numOut <= 0 || numSamples <= 0)
            return;

        delay.setDelaySeconds (trimmed (leftSeconds), trimmed (rightSeconds));
        delay.setFeedback (feedback01);

        // Which way the two lines are wired, this block. Cheap to set every
        // time: the engine ignores a routing it is already in, and the one
        // thing a change does move - Wide's Haas offset - glides there rather
        // than stepping.
        delay.setRouting (routing);

        // Where the tape is, this block. Per block rather than per sample
        // because what it feeds is the two sections' Wear and Flutter, and
        // those are knob-rate settings with their own smoothing behind them -
        // the same path a hand on the Wear knob takes.
        tapePlacement.setTargetValue (tapePost ? 1.0f : 0.0f);
        updateTapeAmounts (tapePlacement.skip (numSamples));

        updateFilters (false);
        delay.setModulation (drift01);

        dryGain.setTargetValue (dryTarget());
        wetGain.setTargetValue (wetTarget());
        engageGain.setTargetValue (engaged ? 1.0f : 0.0f);
        inGain.setTargetValue (inGainLinear);
        outGain.setTargetValue (outGainLinear);

        for (int offset = 0; offset < numSamples; offset += maxBlock)
        {
            const int chunk = juce::jmin (maxBlock, numSamples - offset);

            const float* inL = buffer.getReadPointer (0, offset);
            const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : inL;

            // Walked once and kept: the pre and the post crossfade below both
            // fade against the same ramp, and a SmoothedValue only reads
            // forwards.
            float* engage = engageRamp.data();

            for (int i = 0; i < chunk; ++i)
                engage[i] = engageGain.getNextValue();

            // The module's own input, trimmed by the Input fader. Everything
            // past here works on this rather than on the caller's buffer, which
            // stays untouched as the bypass crossfade's reference - so a
            // bypassed module is unity whatever the fader says.
            float* preL = stageBuffer.getWritePointer (0);
            float* preR = stageBuffer.getWritePointer (1);

            for (int i = 0; i < chunk; ++i)
            {
                const float ig = inGain.getNextValue();

                preL[i] = inL[i] * ig;
                preR[i] = inR[i] * ig;
            }

            // The tape section in front of the delay. Always run, whatever the
            // router says: on Post its Wear and Flutter are at zero, where both
            // stages are bit-exact pass-through, so what it does then is supply
            // the dry path's 6 ms and nothing else. In front it colours the dry
            // signal as well as what goes on to be repeated - a pedal in front
            // of a delay is in front of all of it.
            tapeIn.process (preL, preR, chunk);

            for (int i = 0; i < chunk; ++i)
            {
                preL[i] = inL[i] + (preL[i] - inL[i]) * engage[i];
                preR[i] = inR[i] + (preR[i] - inR[i]) * engage[i];
            }

            float* feedL = inputBuffer.getWritePointer (0);
            float* feedR = inputBuffer.getWritePointer (1);

            for (int i = 0; i < chunk; ++i)
            {
                feedL[i] = preL[i] * engage[i];
                feedR[i] = preR[i] * engage[i];
            }

            float* wetL = wetBuffer.getWritePointer (0);
            float* wetR = wetBuffer.getWritePointer (1);
            delay.process (feedL, feedR, wetL, wetR, chunk);

            // ...and the one on the repeats, on the same terms: always in
            // circuit, silent until the router turns it up. Post puts the tape
            // on the delay's output and nothing else, so the wear and the
            // flutter are on the repeats and the note being played stays clean.
            // It used to run on the finished mix, dry included, which made Post
            // audible on a signal that had never been near the delay, at any
            // Mix, even at zero.
            tapeOut.process (wetL, wetR, chunk);

            // The Filter section, on the repeats and nothing else - see
            // runFilter.
            runFilter (wetL, wetR, chunk);

            // ...and the Mod section's insert half, last of the wet stages. On
            // the repeats and nothing else, for the same reason the tape's Post
            // placement is: a stage on the finished mix is audible on a dry
            // signal that never went near the delay, at any Mix setting and
            // even at zero.
            runPhaser (wetL, wetR, chunk);

            float* mixL = mixBuffer.getWritePointer (0);
            float* mixR = mixBuffer.getWritePointer (1);

            for (int i = 0; i < chunk; ++i)
            {
                const float dg = dryGain.getNextValue();
                const float wg = wetGain.getNextValue();

                mixL[i] = preL[i] * dg + wetL[i] * wg;
                mixR[i] = preR[i] * dg + wetR[i] * wg;
            }

            // ...and what goes after the mix: the Output fader, on its own now
            // that the phaser has moved onto the repeats. stageBuffer is free
            // again by now - the pre pass is finished with and preL/preR have
            // been folded into the mix.

            // With the fader at 0 dB (exactly 1.0 out of decibelsToGain) the
            // copy, the multiply and the crossfade below are all exact, so the
            // output is bit identical to the mix rather than merely close.
            float* postL = stageBuffer.getWritePointer (0);
            float* postR = stageBuffer.getWritePointer (1);

            juce::FloatVectorOperations::copy (postL, mixL, chunk);
            juce::FloatVectorOperations::copy (postR, mixR, chunk);

            for (int i = 0; i < chunk; ++i)
            {
                const float og = outGain.getNextValue();

                postL[i] *= og;
                postR[i] *= og;
            }

            float* outL = buffer.getWritePointer (0, offset);
            float* outR = numOut > 1 ? buffer.getWritePointer (1, offset) : nullptr;

            for (int i = 0; i < chunk; ++i)
            {
                const float e = engage[i];
                const float l = mixL[i] + (postL[i] - mixL[i]) * e;
                const float r = mixR[i] + (postR[i] - mixR[i]) * e;

                if (outR != nullptr)
                {
                    outL[i] = l;
                    outR[i] = r;
                }
                else
                {
                    outL[i] = 0.5f * (l + r);
                }
            }
        }
    }

    // ------------------------------------------------------------------ query

    /** What the host has to compensate for: the dry path's latency, which runs
        through the pre section and nothing else, whatever the router says.
        Constant - both tape stages run exactly once per block on either side of
        the delay, so this never moves. Drift is inside the delay line and the
        phaser is a wet/dry blend, so neither adds any. */
    int latencySamples() const noexcept { return tapeIn.latencySamples(); }

    double tailSeconds() const noexcept { return delay.getTailSeconds(); }

    /** The tape machine's voicing, for a development tuning panel. Both
        placements are the same machine and are tuned together; either one can
        answer for the pair. */
    const ee::dsp::TapeTuning& tapeTuning() const noexcept { return tapeIn.tape.getTuning(); }
    void setTapeTuning (const ee::dsp::TapeTuning& t) noexcept
    {
        tapeIn.tape.setTuning (t);
        tapeOut.tape.setTuning (t);
    }

private:
    /** One tape machine in one place in the chain: the transport's wobble
        (Flutter), then the tape itself (Wear), in the order a machine has them.
        Both are the stages Peak Tape's knobs of the same name drive - shared
        engines, not second models of them, so the two pedals cannot drift
        apart. */
    struct TapeSection
    {
        ee::dsp::TapeTransport transport;
        ee::dsp::TapeCharacter tape;

        void prepare (double sampleRate)
        {
            transport.prepare (sampleRate);
            tape.prepare (sampleRate);
        }

        void reset() noexcept
        {
            transport.reset();
            tape.reset();
        }

        void setAmounts (float wear01, float flutter01) noexcept
        {
            transport.setFlutter01 (flutter01);
            tape.setAmount (wear01);
        }

        void process (float* left, float* right, int numSamples) noexcept
        {
            transport.process (left, right, numSamples);
            tape.process (left, right, numSamples);
        }

        int latencySamples() const noexcept { return transport.getLatencySamples() + tape.getLatencySamples(); }
    };

    /** A delay time with the post section's latency already taken off it, so
        the first repeat lands where the Time knob says. */
    float trimmed (float seconds) const noexcept
    {
        return juce::jmax (delaymodule::kMinDelaySeconds, seconds - postLatencySeconds);
    }

    /** Trails: bypassing leaves the dry path at unity rather than at the mix's
        dry side, so what fades out is the wet only. */
    float dryTarget() const noexcept
    {
        return engaged ? std::cos (mix01 * juce::MathConstants<float>::halfPi) : 1.0f;
    }

    float wetTarget() const noexcept { return std::sin (mix01 * juce::MathConstants<float>::halfPi); }

    /** Splits Wear and Flutter between the two tape placements - all to the pre
        section at 0, all to the post one at 1. */
    void updateTapeAmounts (float placement) noexcept
    {
        // Wear is scaled back to half the stage's travel - see kWearScale.
        // Flutter is not: the transport's wobble is a movement rather than a
        // drive, and at the top of its range it is still musical on a repeat.
        const float wear = juce::jlimit (0.0f, 1.0f, wear01) * delaymodule::kWearScale;
        const float flutter = juce::jlimit (0.0f, 1.0f, flutter01);

        // The router is a fader between the two placements, not a switch. A
        // straight linear split rather than equal power: at the halfway point
        // the section really is half as driven in each place, which is what
        // keeps the total colour roughly constant across the move - the two are
        // the same stage in series, not two takes of one signal being summed.
        tapeIn.setAmounts (wear * (1.0f - placement), flutter * (1.0f - placement));
        tapeOut.setAmounts (wear * placement, flutter * placement);
    }

    /** Recomputes the cut coefficients when either knob has actually moved.
        `force` rebuilds both, for the first block after prepare. */
    void updateFilters (bool force)
    {
        const float lo = pendingLoCutHz;

        if (force || std::abs (lo - loCutHz) > 1.0e-3f)
        {
            loCutHz = lo;
            hiPassActive = lo > loCutMinHz + delaymodule::kLoCutBypassMarginHz;

            if (hiPassActive)
            {
                auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, lo);
                for (int ch = 0; ch < kMaxChannels; ++ch)
                    hiPass[static_cast<size_t> (ch)].coefficients = c;
            }
        }

        const float hi = pendingHiCutHz;

        if (force || std::abs (hi - hiCutHz) > 1.0e-3f)
        {
            hiCutHz = hi;
            loPassActive = hi < hiCutMaxHz - delaymodule::kHiCutBypassMarginHz;

            if (loPassActive)
            {
                auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, hi);
                for (int ch = 0; ch < kMaxChannels; ++ch)
                    loPass[static_cast<size_t> (ch)].coefficients = c;
            }
        }
    }

    /** The Filter section, in place on the repeats. Two cuts either end of the
        band, the same pair Peak EQ carries in its top corner and built from the
        same juce::dsp coefficients, so the two cut alike.

        On the delay's output rather than inside its feedback path, which is
        where a tone control on an echo often sits - because the compounding
        version of this knob already exists. Drift is a rolloff inside the loop
        that takes a little more top off every pass; this shapes the repeats
        once, and leaves the dry signal alone. The two do different jobs, and
        putting a second lowpass in the loop would only have blurred the first.

        Both ends bypass exactly at their resting positions, so a Filter section
        nobody has touched is not in the signal path at all. */
    void runFilter (float* left, float* right, int numSamples) noexcept
    {
        if (! hiPassActive && ! loPassActive)
            return;

        float* io[kMaxChannels] = { left, right };

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            juce::dsp::AudioBlock<float> block (&io[ch], 1, static_cast<size_t> (numSamples));
            juce::dsp::ProcessContextReplacing<float> context (block);

            if (hiPassActive)
                hiPass[static_cast<size_t> (ch)].process (context);

            if (loPassActive)
                loPass[static_cast<size_t> (ch)].process (context);
        }
    }

    /** The Mod section's insert half, in place. ee::dsp::Phaser on Peak Phase's
        own default voicing; its wet/dry is fixed at the setting where the
        notches are deepest (ee::dsp::phaser::kWetMix), so the knob blends the
        whole stage in from out here. At 0 the stage is skipped and the input is
        left bit exact. */
    void runPhaser (float* left, float* right, int numSamples) noexcept
    {
        if (phaser01 <= 0.0f)
            return;

        float* wetL = modBuffer.getWritePointer (0);
        float* wetR = modBuffer.getWritePointer (1);

        phaser.process (left, right, wetL, wetR, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            left[i] += (wetL[i] - left[i]) * phaser01;
            right[i] += (wetR[i] - right[i]) * phaser01;
        }
    }

    double sr = 44100.0;
    int maxBlock = 512;

    TapeSection tapeIn;
    TapeSection tapeOut;
    ee::dsp::TapeDelay delay;
    ee::dsp::Phaser phaser;

    std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> hiPass;
    std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> loPass;

    // Settings, in real units, as the owner last handed them over.
    float leftSeconds = 0.5f;
    float rightSeconds = 0.5f;
    float feedback01 = 0.0f;
    float drift01 = 0.0f;
    float phaser01 = 0.0f;
    float wear01 = 0.0f;
    float flutter01 = 0.0f;
    float mix01 = 0.0f;
    float inGainLinear = 1.0f;
    float outGainLinear = 1.0f;
    bool engaged = true;
    bool tapePost = false;
    ee::dsp::TapeDelay::Routing routing {};

    float pendingLoCutHz = 20.0f;
    float pendingHiCutHz = 20000.0f;
    float loCutMinHz = 20.0f;
    float hiCutMaxHz = 20000.0f;

    // What updateFilters has actually built coefficients for, so a knob that
    // has not moved does not rebuild them.
    float loCutHz = 20.0f;
    float hiCutHz = 20000.0f;
    bool hiPassActive = false;
    bool loPassActive = false;

    /** Where the Tape section is, as a number rather than a switch: 0 is
        entirely in front of the delay, 1 entirely on its repeats, and the
        router glides between them over kPlacementSeconds. Read once per block -
        the two sections' own parameter smoothing carries it the rest of the
        way, exactly as it does for a hand on the Wear knob. */
    juce::SmoothedValue<float> tapePlacement;

    /** What the post section's latency costs the repeats, taken back off the
        delay's own time so the gap between the dry signal and its first repeat
        is what the Time knob says in either placement. */
    float postLatencySeconds = 0.0f;

    juce::SmoothedValue<float> dryGain;
    juce::SmoothedValue<float> wetGain;
    juce::SmoothedValue<float> inGain;
    juce::SmoothedValue<float> outGain;

    /** 1 while engaged, 0 when bypassed. Fades the tape off the dry path and
        closes the delay input, leaving the repeats to ring out. */
    juce::SmoothedValue<float> engageGain;

    /** The stage chain's working buffers. `stageBuffer` carries the signal
        through a side's stages so the untouched version survives for the bypass
        crossfade; `mixBuffer` holds dry+wet before the post side runs;
        `modBuffer` is the Phaser's output, blended back by its knob. */
    juce::AudioBuffer<float> stageBuffer;
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> mixBuffer;
    juce::AudioBuffer<float> modBuffer;

    /** engageGain sampled once per chunk. The pre and the post crossfade both
        need the same ramp, and a SmoothedValue can only be walked once. */
    std::vector<float> engageRamp;
};

} // namespace ee::fx
