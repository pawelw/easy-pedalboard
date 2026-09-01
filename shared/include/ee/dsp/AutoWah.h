#pragma once

#include "AutoWahConfig.h"
#include "Lfo.h"

#include <chowdsp_wdf/chowdsp_wdf.h>

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace ee::dsp
{

/** LFO-driven modulated filter (Peak Wah).

    A wave sweeps a series-RLC tank's cutoff around the Freq setting, tapped
    anywhere on a continuous low- .. band- .. high-pass morph. A fast envelope opens the modulation on a note; the
    Decay knob shapes what happens after:

      * at 0 the LFO runs a half cycle per pluck and then flattens - the sweep
        opens and closes once, with no return leg;
      * in the middle it is a release-time follower (short .. long tail);
      * fully up it latches on and the filter just runs.

    The tank is the same Wave Digital Filter as the original auto-wah
    (chowdsp_wdf, the library behind Peak Overdrive). The three element voltages
    give the three responses off one solve:
      V_C = low-pass    V_R = band-pass    V_L = high-pass

    Signal path, per sample:

      1. gate - mono sum, high-pass, rectify, noise floor, fast-attack /
         Decay-release follower; the top of the Decay knob ramps a floor under
         it, the bottom crossfades to a one-shot that lasts kOneShotCycles of an
         LFO cycle. Two more detectors make it playable: a dynamics follower
         lifts the LFO rate up to kRateEnvDepth on a hard hit, and a transient
         detector resets the LFO phase to 0 on every new note so each pluck
         kicks the sweep from the top;
      2. LFO - ee::dsp::lfoValue at the Shape morph; the Stereo switch offsets
         the right channel by half a cycle (anti-phase);
      3. cutoff = fBase * kSweepRatioMax^(Range * gate * lfo), per channel,
         smoothed; the tank is retuned every kControlBlock samples;
      4. the WDF tank, its three taps crossfaded by the Type morph; matching
         make-up, a tanh peak catcher, a DC blocker and a mild low-pass;
      5. Mix - dry/wet blend.

    The host owns tempo sync: it calls setPeriodSeconds each block and
    snapPhase / nudgePhase to align the free-running LFO to the transport.

    Pure DSP: no JUCE audio-processor types, so it unit-tests headless the same
    way Phaser and Overdrive do.
*/
class AutoWah
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
        const float fs = static_cast<float> (sampleRate);

        nyquistLimit = 0.45f * fs;

        aHp  = freqCoeff (autowah::kDetectorHighpassHz, fs);
        aDc  = freqCoeff (autowah::kDcBlockerHz, fs);
        aLp  = freqCoeff (autowah::kOutputLowpassHz, fs);
        aAtt = timeCoeff (autowah::kAttackMs * 0.001f, fs);
        aF0  = timeCoeff (autowah::kFreqSmoothingMs * 0.001f, fs);
        aDynAtt = timeCoeff (autowah::kDynAttackMs * 0.001f, fs);
        aDynRel = timeCoeff (autowah::kDynReleaseMs * 0.001f, fs);
        aOnSlow = timeCoeff (autowah::kOnsetSlowMs * 0.001f, fs);
        aParam  = timeCoeff (autowah::kParamSmoothMs * 0.001f, fs);
        aOneShotRel = timeCoeff (autowah::kOneShotReleaseMs * 0.001f, fs);
        aGlide = timeCoeff (autowah::kRetriggerGlideMs * 0.001f, fs);
        updateDecay();
        updateType();

        for (auto& tank : tanks)
            tank.prepare (fs);

        reset();
    }

    void reset() noexcept
    {
        envHpZ = 0.0f;
        follow = 0.0f;
        lfoGlideL = 0.0f;
        lfoGlideR = 0.0f;
        dynEnv = 0.0f;
        onSlow = 0.0f;
        armed = true;
        lastGate = 0.0f;
        lastModL = lastModR = 0.0f;
        primed = false;
        lfoPhase = 0.0;
        oneShotGate = 0.0f;
        blockCounter = 0;
        for (auto& v : f0Smoothed) v = fBase;
        dcZ.fill (0.0f);
        lpZ.fill (0.0f);
        for (auto& tank : tanks)
            tank.reset();
        retune();
    }

    void setRange01  (float v) noexcept { rangeTarget = juce::jlimit (0.0f, 1.0f, v); }
    void setMix01    (float v) noexcept { mixTarget   = juce::jlimit (0.0f, 1.0f, v); }
    void setStereo   (bool  v) noexcept { stereoOn = v; }
    void setShape01  (float v) noexcept { shape  = juce::jlimit (0.0f, 1.0f, v); }

    /** Heel (resting) centre frequency, log spaced over the configured range. */
    void setFreq01 (float v) noexcept
    {
        const float t = std::pow (juce::jlimit (0.0f, 1.0f, v), autowah::kFreqKnobSkew);
        fBaseTarget = autowah::kFreqMinHz
                      * std::pow (autowah::kFreqMaxHz / autowah::kFreqMinHz, t);
    }

    /** Resonance of the tank, exponential between the bounds. */
    void setQ01 (float v) noexcept
    {
        const float t = std::pow (juce::jlimit (0.0f, 1.0f, v), autowah::kQKnobSkew);
        qTarget = autowah::kQMin * std::pow (autowah::kQMax / autowah::kQMin, t);
    }

    /** 0 = one sweep per pluck, middle = release-time follower, top = latched. */
    void setDecay01 (float v) noexcept
    {
        decay01 = juce::jlimit (0.0f, 1.0f, v);
        updateDecay();
    }

    /** Filter shape, morphed continuously: 0 = low-pass, 1/2 = band-pass,
        1 = high-pass, crossfaded in between. */
    void setTypeMorph01 (float v) noexcept
    {
        typeMorphTarget = juce::jlimit (0.0f, 1.0f, v);
    }

    /** The three discrete taps, for tests and presets: 0 = LP, 1 = BP, 2 = HP. */
    void setType (int t) noexcept
    {
        setTypeMorph01 (0.5f * static_cast<float> (juce::jlimit (0, 2, t)));
    }

    /** Seconds for one LFO cycle. */
    void setPeriodSeconds (float seconds) noexcept
    {
        const double s = juce::jmax (1.0e-4, static_cast<double> (seconds));
        phaseInc = 1.0 / (s * sampleRate);
    }

    /** Hard-align the LFO to the transport (first playing block, loop jump). */
    void snapPhase (double target01) noexcept
    {
        if (std::isfinite (target01))
            lfoPhase = target01 - std::floor (target01);
    }

    /** Gently pull the LFO toward the transport grid each block. */
    void nudgePhase (double target01) noexcept
    {
        if (! std::isfinite (target01))
            return;
        double err = (target01 - std::floor (target01)) - lfoPhase;
        err -= std::round (err);
        lfoPhase += juce::jlimit (-0.006, 0.006, 0.15 * err);
    }

    /** LFO phase in [0, 1) - for a live UI trace. */
    double phase() const noexcept { return lfoPhase; }

    /** Signed cutoff-sweep exponent (Range * gate * lfo) for each channel at the
        last processed sample: fc_ch = Freq * kSweepRatioMax^mod. For the scope. */
    float modL() const noexcept { return lastModL; }
    float modR() const noexcept { return lastModR; }

    /** In place, one channel per pointer. `right` may be null for a mono
        source; the in and out pointers alias. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        const bool stereoRun = right != nullptr;

        // The opening block after a reset takes the knob values as they stand;
        // after that a nudge ramps rather than steps.
        if (! primed)
        {
            range = rangeTarget;
            mix = mixTarget;
            q = qTarget;
            fBase = fBaseTarget;
            typeMorph = typeMorphTarget;
            for (auto& v : f0Smoothed) v = fBase;
            primed = true;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i];
            const float dryR = stereoRun ? right[i] : 0.0f;
            const float mono = stereoRun ? 0.5f * (dryL + dryR) : dryL;

            if (! std::isfinite (mono))
            {
                reset();
                left[i] = 0.0f;
                if (stereoRun) right[i] = 0.0f;
                continue;
            }

            range += aParam * (rangeTarget - range);
            mix   += aParam * (mixTarget - mix);
            q     += aParam * (qTarget - q);
            fBase += aParam * (fBaseTarget - fBase);
            typeMorph += aParam * (typeMorphTarget - typeMorph);

            // ---- detectors --------------------------------------------
            envHpZ += aHp * (mono - envHpZ);
            const float hp = mono - envHpZ;

            float rect = std::abs (hp) - autowah::kNoiseFloor;
            if (rect < 0.0f) rect = 0.0f;
            follow += (rect > follow ? aAtt : aRel) * (rect - follow);
            const float played = juce::jlimit (0.0f, 1.0f, follow * autowah::kGateSensitivity);
            const float followerGate = juce::jmax (played, decayLatch);

            // Playing-dynamics follower: drives the rate lift and re-arms the
            // per-note retrigger.
            const float aDyn = rect > dynEnv ? aDynAtt : aDynRel;
            dynEnv += aDyn * (rect - dynEnv);
            const float dyn = juce::jlimit (0.0f, 1.0f, dynEnv * autowah::kDynSensitivity);

            onSlow += aOnSlow * (follow - onSlow);
            const float onset = follow - onSlow;
            if (armed && onset > autowah::kOnsetOn)
            {
                armed = false;

                // Snapping the phase steps the LFO, and the gate is already
                // open by the time this fires - so bank the size of the step
                // and hand it back over kRetriggerGlideMs. The modulation is
                // continuous across this sample; only where it is heading
                // changes. Without it the cutoff jumps mid-note and the tank
                // answers with a click.
                const float offsetR = stereoOn ? autowah::kStereoOffset : 0.0f;
                const auto before = static_cast<float> (lfoPhase);

                lfoGlideL = lfoValue (before, shape) - lfoValue (0.0f, shape) + lfoGlideL;
                lfoGlideR = lfoValue (before + offsetR, shape) - lfoValue (offsetR, shape) + lfoGlideR;

                lfoPhase = 0.0;                     // start the sweep from the top
                wrapsSinceRetrigger = 0;
                oneShotGate = 1.0f;
            }
            else if (! armed && onset < autowah::kOnsetOff)
            {
                armed = true;
            }

            // One-shot: hold full until the LFO has run kOneShotCycles of a
            // cycle since the last pluck, then fade out - half a cycle leaves
            // the sweep at the bottom of the wave rather than carrying it back
            // up. oneShotBlend crossfades this in as Decay approaches 0.
            if (wrapsSinceRetrigger >= 1 || lfoPhase >= autowah::kOneShotCycles)
                oneShotGate += aOneShotRel * (0.0f - oneShotGate);
            const float gate = oneShotBlend * oneShotGate
                               + (1.0f - oneShotBlend) * followerGate;
            lastGate = gate;

            // ---- LFO --------------------------------------------------
            const float offset = stereoOn ? autowah::kStereoOffset : 0.0f;
            const auto phF = static_cast<float> (lfoPhase);

            const float lfoL = lfoValue (phF, shape) + lfoGlideL;
            const float lfoR = stereoOn ? lfoValue (phF + offset, shape) + lfoGlideR : lfoL;

            lfoGlideL -= aGlide * lfoGlideL;
            lfoGlideR -= aGlide * lfoGlideR;

            lfoPhase += phaseInc * (1.0 + autowah::kRateEnvDepth * dyn);
            if (lfoPhase >= 1.0)
            {
                lfoPhase -= std::floor (lfoPhase);
                if (wrapsSinceRetrigger < 1000000)
                    ++wrapsSinceRetrigger;
            }

            // ---- cutoff, retuned at the control rate -----------------
            lastModL = range * gate * lfoL;
            lastModR = range * gate * (stereoRun ? lfoR : lfoL);
            updateCutoff (0, lastModL);
            if (stereoRun)
                updateCutoff (1, lastModR);

            if (blockCounter == 0)
                retune();
            if (++blockCounter >= autowah::kControlBlock)
                blockCounter = 0;

            // ---- tank + output stage --------------------------------
            const float makeup = makeupFor (typeMorph);
            const float wetL = post (0, tanks[0].processSample (dryL, typeMorph), makeup);
            const float wetR = stereoRun ? post (1, tanks[1].processSample (dryR, typeMorph), makeup)
                                         : 0.0f;

            left[i] = dryL * (1.0f - mix) + wetL * mix;
            if (stereoRun)
                right[i] = dryR * (1.0f - mix) + wetR * mix;
        }
    }

private:
    //==========================================================================
    /** A series RLC as a Wave Digital Filter. The three element voltages give
        low-pass (C), band-pass (R) and high-pass (L) off one solve; L is fixed,
        C sweeps the centre frequency and R sets Q. */
    struct Tank
    {
        chowdsp::wdft::ResistorT<float>  R { 1000.0f };
        chowdsp::wdft::InductorT<float>  L { autowah::kInductanceH };
        chowdsp::wdft::CapacitorT<float> C { 1.0e-8f };

        chowdsp::wdft::WDFSeriesT<float, decltype (L), decltype (C)>   lc  { L, C };
        chowdsp::wdft::WDFSeriesT<float, decltype (R), decltype (lc)>  rlc { R, lc };
        chowdsp::wdft::PolarityInverterT<float, decltype (rlc)>        inv { rlc };
        chowdsp::wdft::IdealVoltageSourceT<float, decltype (inv)>      vin { inv };

        void prepare (float fs) noexcept { L.prepare (fs); C.prepare (fs); }
        void reset() noexcept { L.reset(); C.reset(); }

        void retune (float capF, float resOhms) noexcept
        {
            C.setCapacitanceValue (capF);
            R.setResistanceValue (resOhms);
        }

        /** One solve, three element voltages, crossfaded by the morph:
            0 = V_C (low-pass), 1/2 = V_R (band-pass), 1 = V_L (high-pass). */
        inline float processSample (float x, float morph) noexcept
        {
            vin.setVoltage (x);
            vin.incident (inv.reflected());
            inv.incident (vin.reflected());

            const float lp = chowdsp::wdft::voltage<float> (C);
            const float bp = chowdsp::wdft::voltage<float> (R);
            const float hp = chowdsp::wdft::voltage<float> (L);

            if (morph <= 0.5f)
                return lp + (morph * 2.0f) * (bp - lp);
            return bp + ((morph - 0.5f) * 2.0f) * (hp - bp);
        }
    };

    static float freqCoeff (float hz, float fs) noexcept
    {
        return juce::jlimit (0.0f, 1.0f,
                             1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / fs));
    }

    static float timeCoeff (float seconds, float fs) noexcept
    {
        if (seconds <= 0.0f)
            return 1.0f;
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-1.0f / (seconds * fs)));
    }

    void updateDecay() noexcept
    {
        const float ms = autowah::kDecayMinMs
                         * std::pow (autowah::kDecayMaxMs / autowah::kDecayMinMs, decay01);
        aRel = timeCoeff (ms * 0.001f, static_cast<float> (sampleRate));

        const float t = juce::jlimit (0.0f, 1.0f,
                                      (decay01 - autowah::kLatchKnee0) / (1.0f - autowah::kLatchKnee0));
        decayLatch = t * t * (3.0f - 2.0f * t);

        const float u = juce::jlimit (0.0f, 1.0f, decay01 / autowah::kOneShotKnee);
        oneShotBlend = 1.0f - u * u * (3.0f - 2.0f * u);
    }

    void updateType() noexcept
    {
        auto gain = [] (float db) { return std::pow (10.0f, db / 20.0f); };
        makeupLP = gain (autowah::kMakeupDbLP);
        makeupBP = gain (autowah::kMakeupDbBP);
        makeupHP = gain (autowah::kMakeupDbHP);
    }

    /** Make-up for the current tap blend, crossfaded in dB rather than in gain:
        a straight line between two gains that are 15 dB apart bulges well above
        either of them in the middle, which is heard as a bump halfway through
        the Type knob's travel. */
    inline float makeupFor (float morph) const noexcept
    {
        const float a = morph <= 0.5f ? makeupLP : makeupBP;
        const float b = morph <= 0.5f ? makeupBP : makeupHP;
        const float t = morph <= 0.5f ? morph * 2.0f : (morph - 0.5f) * 2.0f;

        return a * std::pow (b / a, t);
    }

    void updateCutoff (int ch, float mod) noexcept
    {
        const float target = juce::jlimit (
            20.0f, nyquistLimit,
            fBase * std::pow (autowah::kSweepRatioMax, mod));
        f0Smoothed[static_cast<size_t> (ch)] +=
            aF0 * (target - f0Smoothed[static_cast<size_t> (ch)]);
    }

    void retune() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float w0 = juce::MathConstants<float>::twoPi
                             * juce::jmax (20.0f, f0Smoothed[static_cast<size_t> (ch)]);
            const float capF = 1.0f / (w0 * w0 * autowah::kInductanceH);
            const float resOhms = juce::jmax (
                1.0f, (1.0f / q) * std::sqrt (autowah::kInductanceH / capF));
            tanks[static_cast<size_t> (ch)].retune (capF, resOhms);
        }
    }

    inline float post (int ch, float x, float makeup) noexcept
    {
        x *= makeup;
        x = std::tanh (autowah::kGritDrive * x) / autowah::kGritDrive;

        dcZ[static_cast<size_t> (ch)] += aDc * (x - dcZ[static_cast<size_t> (ch)]);
        x -= dcZ[static_cast<size_t> (ch)];

        lpZ[static_cast<size_t> (ch)] += aLp * (x - lpZ[static_cast<size_t> (ch)]);
        return lpZ[static_cast<size_t> (ch)];
    }

    double sampleRate = 44100.0;
    float nyquistLimit = 20000.0f;

    // Knob-derived voicing - working value plus its ramp target.
    float range = 0.55f, rangeTarget = 0.55f;
    float mix = 0.55f, mixTarget = 0.55f;
    float q = autowah::kQMin, qTarget = autowah::kQMin;
    float fBase = autowah::kFreqMinHz, fBaseTarget = autowah::kFreqMinHz;
    bool stereoOn = false;
    float shape = 0.5f;
    float decay01 = 0.35f;
    float decayLatch = 0.0f;
    float oneShotBlend = 0.0f;
    float typeMorph = autowah::kDefaultTypePct * 0.01f;
    float typeMorphTarget = autowah::kDefaultTypePct * 0.01f;
    bool primed = false;
    float aParam = 1.0f;

    // Gate + playing-dynamics detectors.
    float envHpZ = 0.0f, follow = 0.0f;
    float dynEnv = 0.0f, onSlow = 0.0f;
    bool armed = true;
    float lastGate = 0.0f, lastModL = 0.0f, lastModR = 0.0f;
    float aHp = 0.0f, aAtt = 0.0f, aRel = 0.0f;
    float aDynAtt = 0.0f, aDynRel = 0.0f, aOnSlow = 0.0f;

    // LFO + one-shot.
    double lfoPhase = 0.0;
    double phaseInc = 0.0;
    int wrapsSinceRetrigger = 1;
    float oneShotGate = 0.0f;
    float aOneShotRel = 1.0f;

    // The retrigger's phase jump, handed back over kRetriggerGlideMs.
    float lfoGlideL = 0.0f, lfoGlideR = 0.0f;
    float aGlide = 1.0f;

    // Swept cutoff.
    std::array<float, 2> f0Smoothed { { autowah::kFreqMinHz, autowah::kFreqMinHz } };
    float aF0 = 0.0f;
    int blockCounter = 0;

    // Output stage.
    float makeupLP = 1.0f, makeupBP = 1.0f, makeupHP = 1.0f;
    float aDc = 0.0f, aLp = 0.0f;
    std::array<float, 2> dcZ { { 0.0f, 0.0f } };
    std::array<float, 2> lpZ { { 0.0f, 0.0f } };

    std::array<Tank, 2> tanks;
};

} // namespace ee::dsp
