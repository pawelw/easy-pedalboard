#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

#include "Lfo.h"
#include "TremoloConfig.h"

namespace ee::dsp
{

/** Tremolo and auto-pan: one shaped LFO driving either a ducking gain law or an
    equal-power pan, with an optional bias-tube colour on the tremolo side.

    Peak Trem & Pan is this engine and its parameter layout; Peak Alpine's
    Modulation module runs a second instance. Neither owns the maths.

    The LFO always free-runs on its own phase accumulator, so a rate or division
    change never steps it - it just carries on at a new speed. When the caller
    says it is synced to a running transport, the phase is also aligned to the
    host grid: a hard snap on the first playing block or after a relocate,
    otherwise a gentle per-block pull, so the same bar always plays the same
    phase without a jitter in the host's ppq becoming a click.

    What is *not* here: the Rate knob's mapping to a period (a pedal's own -
    see ee::dsp::RateMap), and the bypass crossfade (ee::plugin::crossfadeToDry,
    which needs a dry copy the host layer already keeps). This engine only ever
    produces the wet signal in place.

    Not real-time-allocating: prepare() sizes everything, and process() writes
    into the buffer it is handed. */
class Tremolo
{
public:
    static constexpr int kMaxChannels = 2;

    /** What the host transport is doing this block. `synced` is the caller's
        decision, not a parameter: it means "the Sync switch is on AND the host
        gave us a finite ppq AND it is playing". Anything less and the LFO
        simply free-runs.

        `playing` is the transport's own state, and is deliberately *not* the
        same flag. It is what decides whether the next aligned block snaps or
        eases: a run of blocks that were playing but unsynced still counts as
        having been playing, so switching Sync on mid-take eases onto the grid
        rather than jumping the phase. Only starting the transport, or
        relocating it, is a jump.

        `cyclesPerQuarter` is how many LFO cycles fit in a quarter note at the
        current division - the caller owns the rate map that knows this. */
    struct Transport
    {
        bool synced = false;
        bool playing = false;
        double ppqStart = 0.0;
        double cyclesPerQuarter = 1.0;
        double ppqPerSample = 0.0;
    };

    void prepare (double newSampleRate, int maxBlockSize)
    {
        // A host that probes with prepareToPlay(0, 0) would otherwise leave a
        // zero here, and 1 / (period * 0) = +inf feeds an inf into the phase
        // accumulator.
        sr = newSampleRate > 0.0 ? newSampleRate : 44100.0;

        const int maxBlock = juce::jmax (1, maxBlockSize);

        reset();

        modSlewCoeff = 1.0f - std::exp (-1.0f / (tremolo::kModSlewSeconds * static_cast<float> (newSampleRate)));
        biasDcCoeff = juce::jlimit (0.0f, 1.0f,
                                    1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * tremolo::kDcHz
                                                     / static_cast<float> (newSampleRate)));

        modBuffer.assign (static_cast<size_t> (maxBlock), 0.0f);

        depth.reset (newSampleRate, tremolo::kSmoothingSeconds);
        depth.setCurrentAndTargetValue (amount01);

        makeup.reset (newSampleRate, tremolo::kSmoothingSeconds);
        makeup.setCurrentAndTargetValue (makeupGain (amount01));

        bias.reset (newSampleRate, tremolo::kSmoothingSeconds);
        bias.setCurrentAndTargetValue (bias01);
    }

    void reset() noexcept
    {
        lfoPhase = 0.0;
        expectedPpq = 0.0;
        haveExpectedPpq = false;
        wasPlaying = false;

        modZ1 = 0.0f;

        for (auto& s : biasDcState)
            s = 0.0f;
    }

    void setAmount01 (float v) noexcept { amount01 = juce::jlimit (0.0f, 1.0f, v); }
    void setShape01 (float v) noexcept { shape01 = juce::jlimit (0.0f, 1.0f, v); }
    void setBias01 (float v) noexcept { bias01 = juce::jlimit (0.0f, 1.0f, v); }
    void setPanning (bool shouldPan) noexcept { panning = shouldPan; }

    /** One LFO cycle's length. The caller works this out from its own Rate knob
        and, when synced, the host tempo. */
    void setPeriodSeconds (float seconds) noexcept { periodSeconds = juce::jmax (1.0e-4f, seconds); }

    /** The gain that keeps the perceived level roughly constant as depth comes
        up. The tremolo law pins its peak at unity and only ever ducks, so the
        effect always sounds quieter when engaged. For a symmetric LFO the
        applied gain sweeps linearly over [1 - d, 1], whose mean-square is
        (1 - d + d^2/3); the reciprocal square root of that restores the RMS
        level. Bounded: ~+2.3 dB at 50 %, ~+4.8 dB at full depth. Non-symmetric
        shapes (exp decay, ramp) lose a touch more, so this slightly
        under-compensates them - deliberately, to keep the wet path from ever
        out-running the dry transients.

        Public because a face may want to show it. */
    static float makeupGain (float depth01) noexcept
    {
        const float d = juce::jlimit (0.0f, 1.0f, depth01);
        return 1.0f / std::sqrt (1.0f - d + d * d / 3.0f);
    }

    /** Wet, in place. `numChannels` is what to touch, capped at kMaxChannels by
        the caller; panning needs two and falls back to leaving the signal alone
        on one, which is the only sensible thing a pan can do to a mono bus. */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples, const Transport& transport) noexcept
    {
        if (numChannels <= 0 || numSamples <= 0)
            return;

        double phaseInc = 1.0 / (static_cast<double> (periodSeconds) * sr);
        // A non-finite or negative increment would spin the wrap below forever
        // and wedge the audio thread - the roar you cannot turn down. Anything
        // past a full cycle per sample is already meaningless, so clamp hard.
        if (! std::isfinite (phaseInc) || phaseInc < 0.0)
            phaseInc = 0.0;
        phaseInc = juce::jmin (phaseInc, 1.0);

        alignToTransport (transport, numSamples);
        healNonFinite();

        depth.setTargetValue (amount01);
        // Only the tremolo law ducks; the panning branch is already equal-power,
        // so it needs no make-up and its target stays at unity.
        makeup.setTargetValue (panning ? 1.0f : makeupGain (amount01));
        // Bias is a tremolo-only colour; in panning mode it stays parked at 0.
        bias.setTargetValue (panning ? 0.0f : bias01);

        if (numSamples > static_cast<int> (modBuffer.size()))
            modBuffer.assign (static_cast<size_t> (numSamples), 0.0f);

        // One shaped LFO value per sample, slew-limited so nothing steps the
        // gain in a single sample. Same helper the UI preview uses, so the
        // drawing still tracks.
        for (int i = 0; i < numSamples; ++i)
        {
            const float raw = lfoValue (static_cast<float> (lfoPhase), shape01);
            modZ1 += modSlewCoeff * (raw - modZ1);
            modBuffer[static_cast<size_t> (i)] = modZ1;

            lfoPhase += phaseInc;
            // Branchless wrap to [0, 1). A `while (lfoPhase >= 1.0)` spun
            // forever if lfoPhase ever went non-finite; std::floor is O(1)
            // whatever it holds, and a non-finite result is caught by the
            // self-heal at the next block.
            lfoPhase -= std::floor (lfoPhase);
        }

        if (panning && numChannels >= 2)
            runPanning (buffer, numSamples);
        else if (! panning)
            runTremolo (buffer, numChannels, numSamples);
        else
        {
            // Panning asked for on a mono output: nothing sensible to sweep,
            // leave dry.
            depth.skip (numSamples);
            makeup.skip (numSamples);
            bias.skip (numSamples);
        }
    }

private:
    /** The LFO always free-runs; when synced to a running transport this also
        pulls it onto the host grid. A hard snap only on the first playing block
        or a transport jump (loop / relocate); otherwise a gentle per-block pull,
        capped small. */
    void alignToTransport (const Transport& transport, int numSamples) noexcept
    {
        if (transport.synced)
        {
            double target = transport.ppqStart * transport.cyclesPerQuarter;
            target -= std::floor (target);

            const bool jumped = ! wasPlaying
                                || (haveExpectedPpq
                                    && std::abs (transport.ppqStart - expectedPpq) > tremolo::kJumpPpq);

            if (jumped)
            {
                lfoPhase = target;
            }
            else
            {
                double err = target - lfoPhase;
                err -= std::round (err); // wrap to [-0.5, 0.5]
                lfoPhase += juce::jlimit (-tremolo::kPhasePullMax, tremolo::kPhasePullMax,
                                          tremolo::kPhasePullFraction * err);
            }

            expectedPpq = transport.ppqStart + numSamples * transport.ppqPerSample;
            haveExpectedPpq = true;
        }
        else
        {
            haveExpectedPpq = false; // next playing block re-aligns from scratch
        }

        wasPlaying = transport.playing;
    }

    /** Self-heal if a bad host value ever slipped a non-finite into the state -
        otherwise a single NaN here would stick and roar. Every running state
        variable that feeds the next block has to be covered: the bias-tube DC
        blocker latches just as hard as the LFO phase does. */
    void healNonFinite() noexcept
    {
        if (! std::isfinite (lfoPhase))
            lfoPhase = 0.0;
        if (! std::isfinite (modZ1))
            modZ1 = 0.0f;
        for (auto& s : biasDcState)
            if (! std::isfinite (s))
                s = 0.0f;
    }

    /** Equal-power auto-pan, sample accurate so it tracks the LFO exactly.
        juce::dsp::Panner is the obvious reuse here, but it bakes in a fixed
        50 ms gain ramp that swallows anything moving at an LFO rate, so the pan
        law is applied directly - unity in the centre, +3 dB / silence at the
        extremes. */
    void runPanning (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        constexpr float kCentreComp = juce::MathConstants<float>::sqrt2;
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const float d = depth.getNextValue();
            const float pan = juce::jlimit (-1.0f, 1.0f, d * modBuffer[static_cast<size_t> (i)]);
            const float angle = (pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;

            left[i] *= std::cos (angle) * kCentreComp;
            right[i] *= std::sin (angle) * kCentreComp;
        }

        makeup.skip (numSamples);
        bias.skip (numSamples);
    }

    /** Tremolo: LFO at +1 is unity, at -1 is (1 - depth). JUCE has no tremolo
        primitive, so the gain law is written out here. The make-up factor lifts
        the whole envelope so bringing the depth up doesn't just make it quieter.
        Bias (0 = clean opto, 1 = full bias-tube) reshapes the ducking envelope
        and folds in a throb-synced asymmetric drive; see TremoloConfig.h. */
    void runTremolo (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float d = depth.getNextValue();
            const float mk = makeup.getNextValue();
            const float b01 = bias.getNextValue();
            const float rawDuck = 0.5f - 0.5f * modBuffer[static_cast<size_t> (i)]; // 0 loud .. 1 quiet

            // Bend the duck toward a harder, flatter-bottomed pulse as Bias
            // comes up. Exponent 1 (b01 = 0) leaves the LFO shape exactly as the
            // opto law had it.
            const float duck = b01 > 0.0f
                                   ? std::pow (juce::jlimit (0.0f, 1.0f, rawDuck), 1.0f - tremolo::kDuckSkew * b01)
                                   : rawDuck;

            const float g = mk * (1.0f - d * duck);

            if (b01 <= 0.0f)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
                continue;
            }

            // Drive rises with the (shaped) dip, so the grind swells and clears
            // in time with the throb. One-sided offset -> pulsing even
            // harmonics; the trim tracks the drive so the level only sags a
            // little.
            const float driveAmt = b01 * d * duck;
            const float k = 1.0f + tremolo::kDrive * driveAmt;
            const float trim = 1.0f / (1.0f + tremolo::kTrim * driveAmt);
            const float tanhAsym = std::tanh (tremolo::kAsym * driveAmt);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* s = buffer.getWritePointer (ch) + i;
                const float clean = *s * g;

                float coloured = (std::tanh (clean * k + tremolo::kAsym * driveAmt) - tanhAsym) * trim;
                biasDcState[ch] += biasDcCoeff * (coloured - biasDcState[ch]);
                coloured -= biasDcState[ch];

                *s = clean + b01 * (coloured - clean);
            }
        }
    }

    double sr = 44100.0;

    float amount01 = 0.0f;
    float shape01 = 0.0f;
    float bias01 = 0.0f;
    bool panning = false;
    float periodSeconds = 0.5f;

    juce::SmoothedValue<float> depth;  // 0..1 LFO amount
    juce::SmoothedValue<float> makeup; // gain that offsets the tremolo's level drop
    juce::SmoothedValue<float> bias;   // 0..1 - crossfade from opto to bias-tube tremolo

    std::vector<float> modBuffer; // shaped, slew-limited LFO, per sample

    // The LFO free-runs on this phase accumulator; when synced it is nudged
    // (or, on a transport jump, snapped) towards the host timeline so the same
    // bar always plays the same phase.
    double lfoPhase = 0.0; // [0, 1)
    double expectedPpq = 0.0;
    bool haveExpectedPpq = false;
    bool wasPlaying = false;

    float modZ1 = 0.0f;
    float modSlewCoeff = 1.0f;

    // Per-channel DC blocker for the bias-tube stage: the LFO-driven operating
    // point leaves a wandering offset that would otherwise pump the output.
    float biasDcState[kMaxChannels] = {};
    float biasDcCoeff = 1.0f;
};

} // namespace ee::dsp
