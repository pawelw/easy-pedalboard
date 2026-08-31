#pragma once

#include "AutoWahConfig.h"
#include "Lfo.h"

#include <chowdsp_wdf/chowdsp_wdf.h>

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** LFO-driven modulated filter (Peak Wah).

    A wave - or a random step - sweeps a series-RLC tank's cutoff around the
    Freq setting, tapped as a low-, band- or high-pass. A fast envelope opens
    the modulation on a note; the Decay knob is how fast it flattens once you
    stop, and fully up it latches on so the filter just runs.

    The tank is the same Wave Digital Filter as the original auto-wah
    (chowdsp_wdf, the library behind Peak Overdrive). The three element voltages
    give the three responses off one solve:
      V_C = low-pass    V_R = band-pass    V_L = high-pass

    Signal path, per sample:

      1. gate - mono sum, high-pass, rectify, noise floor, fast-attack /
         Decay-release follower; the top of the Decay knob ramps a floor under
         it so it can be pinned open. Two more detectors make it playable: a
         dynamics follower lifts the LFO rate up to kRateEnvDepth on a hard hit,
         and a transient detector resets the LFO phase to 0 on every new note so
         each pluck kicks the sweep from the top;
      2. LFO - ee::dsp::lfoValue at the Shape morph, or a slewed random step
         twice a cycle when Random is on; the Stereo switch offsets the right
         channel by half a cycle (anti-phase) with its own random stream;
      3. cutoff = fBase * kSweepRatioMax^(Amount * gate * lfo), per channel,
         smoothed; the tank is retuned every kControlBlock samples;
      4. the WDF tank, tapped by Type; per-type make-up, a tanh peak catcher, a
         DC blocker and a mild low-pass;
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
        aRnd = timeCoeff (autowah::kRandomSlewMs * 0.001f, fs);
        aDynAtt = timeCoeff (autowah::kDynAttackMs * 0.001f, fs);
        aDynRel = timeCoeff (autowah::kDynReleaseMs * 0.001f, fs);
        aOnSlow = timeCoeff (autowah::kOnsetSlowMs * 0.001f, fs);
        updateDecay();
        updateType();

        for (auto& tank : tanks)
            tank.prepare (fs);

        rng[0] = 0x9e3779b9u;
        rng[1] = 0x243f6a88u;

        reset();
    }

    void reset() noexcept
    {
        envHpZ = 0.0f;
        follow = 0.0f;
        dynEnv = 0.0f;
        onSlow = 0.0f;
        armed = true;
        lastGate = 0.0f;
        lfoPhase = 0.0;
        blockCounter = 0;
        for (auto& v : f0Smoothed) v = fBase;
        for (auto& v : rndHeld)    v = 0.0f;
        for (auto& v : rndTarget)  v = 0.0f;
        for (auto& v : lastHalf)   v = 0;
        dcZ.fill (0.0f);
        lpZ.fill (0.0f);
        for (auto& tank : tanks)
            tank.reset();
        retune();
    }

    void setAmount01 (float v) noexcept { amount = juce::jlimit (0.0f, 1.0f, v); }
    void setMix01    (float v) noexcept { mix    = juce::jlimit (0.0f, 1.0f, v); }
    void setStereo   (bool  v) noexcept { stereoOn = v; }
    void setShape01  (float v) noexcept { shape  = juce::jlimit (0.0f, 1.0f, v); }
    void setRandom   (bool  v) noexcept { random = v; }

    /** Heel (resting) centre frequency, log spaced over the configured range. */
    void setFreq01 (float v) noexcept
    {
        const float t = std::pow (juce::jlimit (0.0f, 1.0f, v), autowah::kFreqKnobSkew);
        fBase = autowah::kFreqMinHz
                * std::pow (autowah::kFreqMaxHz / autowah::kFreqMinHz, t);
    }

    /** Resonance of the tank, exponential between the bounds. */
    void setQ01 (float v) noexcept
    {
        const float t = std::pow (juce::jlimit (0.0f, 1.0f, v), autowah::kQKnobSkew);
        q = autowah::kQMin * std::pow (autowah::kQMax / autowah::kQMin, t);
    }

    /** Gate release, and - over its top slice - a floor that latches the LFO on. */
    void setDecay01 (float v) noexcept
    {
        decay01 = juce::jlimit (0.0f, 1.0f, v);
        updateDecay();
    }

    /** 0 = low-pass, 1 = band-pass, 2 = high-pass. */
    void setType (int t) noexcept
    {
        type = juce::jlimit (0, 2, t);
        updateType();
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

    /** Effective modulation depth (Amount * gate) at the last processed sample,
        for the UI trace's amplitude. */
    float depth01() const noexcept { return amount * lastGate; }

    /** In place, one channel per pointer. `right` may be null for a mono
        source; the in and out pointers alias. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        const bool stereoRun = right != nullptr;

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

            // ---- detectors --------------------------------------------
            envHpZ += aHp * (mono - envHpZ);
            const float hp = mono - envHpZ;

            float rect = std::abs (hp) - autowah::kNoiseFloor;
            if (rect < 0.0f) rect = 0.0f;
            follow += (rect > follow ? aAtt : aRel) * (rect - follow);
            const float played = juce::jlimit (0.0f, 1.0f, follow * autowah::kGateSensitivity);
            const float gate = juce::jmax (played, decayLatch);
            lastGate = gate;

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
                lfoPhase = 0.0;                     // start the sweep from the top
                rndHeld[0] = rndTarget[0] = 1.0f;
                lastHalf[0] = 0;
            }
            else if (! armed && onset < autowah::kOnsetOff)
            {
                armed = true;
            }

            // ---- LFO --------------------------------------------------
            const float offset = stereoOn ? autowah::kStereoOffset : 0.0f;
            const auto phF = static_cast<float> (lfoPhase);

            float lfoL, lfoR;
            if (random)
            {
                stepRandom (0, phF);
                if (stereoOn) stepRandom (1, phF + offset);
                lfoL = rndHeld[0];
                lfoR = stereoOn ? rndHeld[1] : rndHeld[0];
            }
            else
            {
                lfoL = lfoValue (phF, shape);
                lfoR = stereoOn ? lfoValue (phF + offset, shape) : lfoL;
            }

            lfoPhase += phaseInc * (1.0 + autowah::kRateEnvDepth * dyn);
            if (lfoPhase >= 1.0)
                lfoPhase -= std::floor (lfoPhase);

            // ---- cutoff, retuned at the control rate -----------------
            updateCutoff (0, amount * gate * lfoL);
            if (stereoRun)
                updateCutoff (1, amount * gate * lfoR);

            if (blockCounter == 0)
                retune();
            if (++blockCounter >= autowah::kControlBlock)
                blockCounter = 0;

            // ---- tank + output stage --------------------------------
            const float wetL = post (0, tanks[0].processSample (dryL, type));
            const float wetR = stereoRun ? post (1, tanks[1].processSample (dryR, type)) : 0.0f;

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

        inline float processSample (float x, int tap) noexcept
        {
            vin.setVoltage (x);
            vin.incident (inv.reflected());
            inv.incident (vin.reflected());
            if (tap == 0) return chowdsp::wdft::voltage<float> (C);   // low-pass
            if (tap == 2) return chowdsp::wdft::voltage<float> (L);   // high-pass
            return chowdsp::wdft::voltage<float> (R);                 // band-pass
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
    }

    void updateType() noexcept
    {
        const float db = type == 0 ? autowah::kMakeupDbLP
                       : type == 2 ? autowah::kMakeupDbHP
                                   : autowah::kMakeupDbBP;
        makeupGain = std::pow (10.0f, db / 20.0f);
    }

    float nextRandom (int ch) noexcept
    {
        uint32_t s = rng[static_cast<size_t> (ch)];
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        rng[static_cast<size_t> (ch)] = s;
        return (static_cast<float> (s) * (1.0f / 4294967296.0f)) * 2.0f - 1.0f;
    }

    /** Draw a new held level at each half-cycle crossing of `phaseArg`, then
        slew the channel's held value toward it. */
    void stepRandom (int ch, float phaseArg) noexcept
    {
        float p = phaseArg - std::floor (phaseArg);
        const int half = p < 0.5f ? 0 : 1;
        if (half != lastHalf[static_cast<size_t> (ch)])
        {
            lastHalf[static_cast<size_t> (ch)] = half;
            rndTarget[static_cast<size_t> (ch)] = nextRandom (ch);
        }
        rndHeld[static_cast<size_t> (ch)] +=
            aRnd * (rndTarget[static_cast<size_t> (ch)] - rndHeld[static_cast<size_t> (ch)]);
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

    inline float post (int ch, float x) noexcept
    {
        x *= makeupGain;
        x = std::tanh (autowah::kGritDrive * x) / autowah::kGritDrive;

        dcZ[static_cast<size_t> (ch)] += aDc * (x - dcZ[static_cast<size_t> (ch)]);
        x -= dcZ[static_cast<size_t> (ch)];

        lpZ[static_cast<size_t> (ch)] += aLp * (x - lpZ[static_cast<size_t> (ch)]);
        return lpZ[static_cast<size_t> (ch)];
    }

    double sampleRate = 44100.0;
    float nyquistLimit = 20000.0f;

    // Knob-derived voicing.
    float amount = 0.55f;
    float mix = 0.55f;
    bool stereoOn = false;
    float shape = 0.5f;
    float decay01 = 0.35f;
    float decayLatch = 0.0f;
    float fBase = autowah::kFreqMinHz;
    float q = autowah::kQMin;
    int type = autowah::kDefaultType;
    bool random = false;

    // Gate + playing-dynamics detectors.
    float envHpZ = 0.0f, follow = 0.0f;
    float dynEnv = 0.0f, onSlow = 0.0f;
    bool armed = true;
    float lastGate = 0.0f;
    float aHp = 0.0f, aAtt = 0.0f, aRel = 0.0f;
    float aDynAtt = 0.0f, aDynRel = 0.0f, aOnSlow = 0.0f;

    // LFO.
    double lfoPhase = 0.0;
    double phaseInc = 0.0;
    float aRnd = 0.0f;
    std::array<uint32_t, 2> rng { { 1u, 2u } };
    std::array<float, 2> rndHeld { { 0.0f, 0.0f } };
    std::array<float, 2> rndTarget { { 0.0f, 0.0f } };
    std::array<int, 2> lastHalf { { 0, 0 } };

    // Swept cutoff.
    std::array<float, 2> f0Smoothed { { autowah::kFreqMinHz, autowah::kFreqMinHz } };
    float aF0 = 0.0f;
    int blockCounter = 0;

    // Output stage.
    float makeupGain = 1.0f;
    float aDc = 0.0f, aLp = 0.0f;
    std::array<float, 2> dcZ { { 0.0f, 0.0f } };
    std::array<float, 2> lpZ { { 0.0f, 0.0f } };

    std::array<Tank, 2> tanks;
};

} // namespace ee::dsp
