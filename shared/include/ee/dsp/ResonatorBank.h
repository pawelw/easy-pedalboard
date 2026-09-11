#pragma once

#include "ModDelayLine.h"
#include "OnsetGate.h"
#include "SympathyConfig.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** Peak Sympathy's sympathetic-resonance engine.

    The player's signal barely passes through; instead it excites a bank of
    tuned string loops that ring, bloom and beat against each other. Reference
    bar: Mutable Instruments Rings/Elements, Ableton Resonators. The default
    failure mode is "flanged mush", and the design is aimed against it - which
    is why the exciter, the limiting and the tuning, not the resonator, are
    where the work is.

    ---------------------------------------------------------------------------
    Why no interpolator sits in the string loop
    ---------------------------------------------------------------------------
    Fractional delay is mandatory - integer lengths detune audibly above a few
    hundred Hz. But a broadband interpolator inside the loop is a trap: a cubic
    Hermite read loses several dB up top per trip, and a 440 Hz string makes
    440 trips a second, so that loss compounds into the dominant damping term
    and the Damping control stops doing anything. So tuning is split into an
    integer delay tap (the Hermite read at fraction 0 is bit-exact identity)
    plus one first-order allpass for the sub-sample remainder. A first-order
    allpass has unity magnitude at every frequency - phase only - so the loop's
    damping is entirely the explicit one-pole. The allpass coefficient is
    solved so its phase delay is exact at each string's fundamental, not just
    at DC. Measured tuning error with this scheme: < 0.4 cents, 110 Hz-2.6 kHz.
*/

// ============================================================================
// The coupling matrix
// ============================================================================
/** One application of the doubly-stochastic circular smoother C to the 16
    string loop signals, in place:  out[i] = (1-2w)*v[i] + w*(v[i-1] + v[i+1]),
    indices wrapping. A circulant *matrix*, not a naive additive sum of
    neighbours: its rows sum to 1 and every coefficient is positive, so its
    spectral radius is exactly 1 and every non-DC spatial mode has an
    eigenvalue below 1. Blended into each string's feedback under the loop gain,
    it stays bounded by kLoopGainMax for any Coupling - which is the thing a
    naive additive coupling gets wrong - while the varied per-mode decay splits
    and beats the strings.

    Because C low-passes the (largely uncorrelated) per-string signals, a strong
    blend also shrinks each string's own feedback, so Coupling is kept modest
    here and a small output makeup (kCouplingMakeup) keeps the level flat across
    the knob. The beating proper is Spread's job, not this. A Hadamard mix (what
    an FDN uses) is wrong here: its +1 eigenspace concentrates on one string. */
inline void coupleCircular (float* v) noexcept
{
    constexpr int N = 16;
    constexpr float w = 0.12f;
    float out[N];
    for (int i = 0; i < N; ++i)
        out[i] = (1.0f - 2.0f * w) * v[i] + w * (v[(i + N - 1) % N] + v[(i + 1) % N]);
    for (int i = 0; i < N; ++i)
        v[i] = out[i];
}

// ============================================================================
// StringVoice - one tuned waveguide, with retune glide and an energy limiter
// ============================================================================
class StringVoice
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? static_cast<float> (sampleRate) : 44100.0f;
        line.prepare (sampleRate, sympathy::kMaxDelaySeconds);
        dcR = 1.0f - juce::MathConstants<float>::twoPi * sympathy::kDcBlockHz / fs;
        aEnergy = onePoleCoeff (sympathy::kEnergyFollowHz, fs);

        smF0 = targetF0;
        smBright = targetBright;
        smLoopGain = 0.0f;
        reset();
        recompute();
    }

    void reset() noexcept
    {
        line.reset();
        dampState = 0.0f;
        apX1 = apY1 = 0.0f;
        apOut = yTap = 0.0f;
        dcX1 = dcY1 = 0.0f;
        energyFollow = 0.0f;
        burstLpZ = 0.0f; // the noise seed itself survives a reset - see seedNoise()
    }

    /** Per block: hand in this string's current tuning target and tone. */
    void setTargets (float f0Hz, float bright01, float t60Seconds, bool freeze) noexcept
    {
        targetF0 = juce::jlimit (sympathy::kMinF0, sympathy::kMaxF0, f0Hz);
        targetBright = juce::jlimit (0.0f, 1.0f, bright01);
        targetT60 = juce::jmax (0.02f, t60Seconds);
        frozen = freeze;
    }

    /** Per block: advance the glides over `numSamples` and rebuild coefficients.
        Delay glides geometrically (cents-linear) so a retune bends smoothly
        rather than zippering; tone and loop gain glide linearly. */
    void updateBlock (int numSamples) noexcept
    {
        const float n = static_cast<float> (juce::jmax (1, numSamples));
        const float kDelay = 1.0f - std::exp (-n / (fs * sympathy::kRetuneGlideMs * 0.001f));
        const float kCtrl = 1.0f - std::exp (-n / (fs * sympathy::kControlGlideMs * 0.001f));

        smF0 *= std::pow (juce::jmax (1.0e-6f, targetF0 / smF0), kDelay);
        smBright += kCtrl * (targetBright - smBright);

        recompute();

        const float target = frozen ? sympathy::kFreezeLoopGain : loopGainForDecay;
        smLoopGain += kCtrl * (target - smLoopGain);
    }

    /** Phase 1 of the loop: read the delay, run the damping one-pole and the
        allpass. Returns the pre-gain loop signal - the thing the bank mixes
        through the coupling matrix. The output tap is stashed for `outTap()`.
        Nothing is written back yet: `writeBack` closes the loop. */
    float readFilter() noexcept
    {
        const float d = line.read (static_cast<float> (L)); // integer tap: frac 0 -> identity

        dampState = (1.0f - b) * d + b * dampState;

        // First-order allpass, direct form:  y = a*x + x_{-1} - a*y_{-1}
        apOut = a * dampState + apX1 - a * apY1;
        apX1 = dampState;
        apY1 = apOut;

        // DC blocker on the output tap - its phase never enters the loop.
        yTap = apOut - dcX1 + dcR * dcY1;
        dcX1 = apOut;
        dcY1 = yTap;
        return apOut;
    }

    /** Phase 2: close the loop. `blendedLoop` is this string's coupling blend
        of the pre-gain loop signals (norm <= max of the inputs, since the
        blend and the mix matrix are both energy-preserving); the loop gain and
        the per-string energy limiter are applied here, so total loop gain
        stays bounded by kLoopGainMax whatever Coupling does. */
    void writeBack (float blendedLoop, float excite) noexcept
    {
        // Per-string energy limiter: track this string's mean square and, above
        // the ceiling, scale the feedback so stored energy cannot climb without
        // bound (a chord tuned near a mode, or Freeze at unity loop gain).
        energyFollow += aEnergy * (yTap * yTap - energyFollow);
        float limit = 1.0f;
        if (energyFollow > sympathy::kStringEnergyCeiling)
            limit = std::sqrt (sympathy::kStringEnergyCeiling / energyFollow);

        float fb = smLoopGain * limit * blendedLoop;
        fb = juce::jlimit (-sympathy::kStateCeiling, sympathy::kStateCeiling, fb);
        if (! std::isfinite (fb))
        {
            reset();
            fb = 0.0f;
        }

        line.write (excite + fb);
        line.advance();
    }

    float outTap() const noexcept { return yTap; }
    float loopGainValue() const noexcept { return smLoopGain; }
    int   integerDelay() const noexcept { return L; }
    float energy() const noexcept { return energyFollow; }
    float currentF0() const noexcept { return smF0; }

    /** A non-zero seed unique to this string, so its own noise sequence never
        matches another string's. See burstNoiseLp(). */
    void seedNoise (uint32_t seed) noexcept { noiseRng = seed != 0 ? seed : 1u; }

    /** This string's own share of the exciter burst: its own noise, windowed by
        the *shared* envelope (Exciter::burstEnvelope) and lowpassed by its own
        filter state. Sixteen strings each drawing independent noise from one
        envelope is what keeps the attack from summing into one loud, unpitched
        "click" that masks the sixteen tuned voices - see the note on
        Exciter::process(). */
    float burstNoiseLp (float envelopeAmount, float lpCoeff) noexcept
    {
        noiseRng ^= noiseRng << 13;
        noiseRng ^= noiseRng >> 17;
        noiseRng ^= noiseRng << 5;
        const float noise = static_cast<float> (static_cast<int32_t> (noiseRng)) * (1.0f / 2147483648.0f);
        burstLpZ += lpCoeff * (noise * envelopeAmount - burstLpZ);
        return burstLpZ;
    }

private:
    void recompute() noexcept
    {
        const float w0 = juce::MathConstants<float>::twoPi * smF0 / fs;

        b = juce::jmap (smBright, sympathy::kDampBMax, sympathy::kDampBMin);

        const float gd = onePoleMag (b, w0);         // |H_damp(w0)|, <= 1
        const float pdd = onePolePhaseDelay (b, w0); // its phase delay, samples

        const float needed = fs / smF0 - pdd;
        int li = static_cast<int> (std::floor (needed - 0.5f));
        li = juce::jmax (2, li);
        const float frac = juce::jlimit (0.5f, 1.5f, needed - static_cast<float> (li));
        L = li;
        a = solveAllpassCoeff (frac, w0);

        const float trips = juce::jmax (1.0e-3f, targetT60 * smF0);
        const float perTrip = std::exp (sympathy::kLn001 / trips);
        loopGainForDecay = juce::jmin (perTrip / juce::jmax (0.05f, gd), sympathy::kLoopGainMax);
    }

    static float onePoleCoeff (float hz, float fs) noexcept
    {
        const float w = juce::MathConstants<float>::twoPi * hz / fs;
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    static float onePoleMag (float b, float w) noexcept
    {
        return (1.0f - b) / std::sqrt (std::max (1.0e-12f, 1.0f - 2.0f * b * std::cos (w) + b * b));
    }

    static float onePolePhaseDelay (float b, float w) noexcept
    {
        if (w < 1.0e-6f)
            return b / juce::jmax (1.0e-4f, 1.0f - b);
        return std::atan2 (b * std::sin (w), 1.0f - b * std::cos (w)) / w;
    }

    /** Phase delay, in samples, of A(z) = (a + z^-1) / (1 + a z^-1) at w. */
    static float allpassPhaseDelay (float a, float w) noexcept
    {
        const float c = std::cos (w), s = std::sin (w);
        float ph = std::atan2 (-s, a + c) - std::atan2 (-a * s, 1.0f + a * c);
        if (ph > 0.0f)
            ph -= juce::MathConstants<float>::twoPi;
        return -ph / w;
    }

    /** Coefficient whose allpass phase delay equals `target` samples at `w`.
        Monotonic in a, so a bisection is robust and exact enough. Runs once
        per block per string - never per sample. */
    static float solveAllpassCoeff (float target, float w) noexcept
    {
        float lo = -0.99f, hi = 0.99f;
        for (int i = 0; i < 40; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            if (allpassPhaseDelay (mid, w) > target)
                lo = mid;
            else
                hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    ModDelayLine line;
    float fs = 44100.0f;

    // targets (per block) and their glided values
    float targetF0 = 220.0f, targetBright = 0.5f, targetT60 = 4.0f;
    bool  frozen = false;
    float smF0 = 220.0f, smBright = 0.5f, smLoopGain = 0.0f;

    // derived coefficients
    int   L = 200;
    float a = 0.0f;                 // allpass coefficient
    float b = 0.1f;                 // damping one-pole coefficient
    float loopGainForDecay = 0.0f;  // gain the Decay time asks for, pre-Freeze

    // filter + limiter state
    float dampState = 0.0f;
    float apX1 = 0.0f, apY1 = 0.0f;
    float apOut = 0.0f, yTap = 0.0f;
    float dcR = 0.999f, dcX1 = 0.0f, dcY1 = 0.0f;
    float aEnergy = 0.0f, energyFollow = 0.0f;

    // this string's own share of the exciter burst - see burstNoiseLp()
    uint32_t noiseRng = 1u;
    float burstLpZ = 0.0f;
};

// ============================================================================
// Exciter - the 80 %
// ============================================================================
/** Turns the player's signal into what feeds the bank: a windowed noise burst
    fired on each attack (fixed-seed xorshift, so the whole engine is
    deterministic), plus a much weaker continuous bleed. Also owns the two
    bank-wide envelopes the burst drives - Bloom (a post-onset brightness
    sweep) and the dry duck. */
class Exciter
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? static_cast<float> (sampleRate) : 44100.0f;

        aEnvHp = onePoleCoeff (sympathy::kEnvHpHz, fs);
        aAtt = onePoleCoeff (1000.0f / sympathy::kEnvAttackMs, fs);
        aRel = onePoleCoeff (1000.0f / sympathy::kEnvReleaseMs, fs);
        aBleedHp = onePoleCoeff (sympathy::kBleedHpHz, fs);
        aBurstLp = onePoleCoeff (sympathy::kBurstLpHz, fs);
        aDriveZip = onePoleCoeff (1.0f / sympathy::kSensZipSeconds, fs);
        aDuckAtt = onePoleCoeff (1000.0f / sympathy::kDuckAttackMs, fs);
        aDuckRel = onePoleCoeff (1000.0f / sympathy::kDuckReleaseMs, fs);

        burstLen = juce::jmax (1, static_cast<int> (fs * sympathy::kBurstMs * 0.001f));
        burstAtk = juce::jmax (1, static_cast<int> (fs * sympathy::kBurstAttackMs * 0.001f));

        onset.prepare (fs, sympathy::kOnsetEnvDecayMs, sympathy::kOnsetAttackWidthMs,
                       sympathy::kOnsetRiseRatioOn, sympathy::kOnsetRiseRatioOff,
                       sympathy::kOnsetMinRise, sympathy::kOnsetLockoutMs);
        setBloom01 (bloom01);
        reset();
    }

    void reset() noexcept
    {
        envHpZ = follow = 0.0f;
        bleedHpZ = 0.0f;
        burstPos = burstLen;
        burstGain = 0.0f;
        burstEnv = 0.0f;
        drive = driveTarget;
        bloomEnv = 0.0f;
        duckEnv = 0.0f;
        triggers = 0;
        onset.reset();
    }

    void setSensitivity01 (float s01) noexcept
    {
        const float s = juce::jlimit (0.0f, 1.0f, s01);
        driveTarget = sympathy::kSensMin * std::pow (sympathy::kSensMax / sympathy::kSensMin, s);
    }

    void setBloom01 (float b01) noexcept
    {
        bloom01 = juce::jlimit (0.0f, 1.0f, b01);
        const float ms = juce::jmap (bloom01, sympathy::kBloomMinMs, sympathy::kBloomMaxMs);
        bloomDecay = std::exp (-1.0f / (fs * juce::jmax (1.0f, ms) * 0.001f));
    }

    /** @return the *shared* excitation to inject into every string: the weak
        continuous bleed, deterministic and legitimately identical for all
        sixteen (it is the real played signal). The burst is deliberately not
        part of this - see `burstEnvelope()`. */
    float process (float xin) noexcept
    {
        drive += aDriveZip * (driveTarget - drive);

        envHpZ += aEnvHp * (xin - envHpZ);
        const float hp = xin - envHpZ;
        float rect = std::abs (hp) - sympathy::kNoiseFloor;
        if (rect < 0.0f)
            rect = 0.0f;
        follow += (rect > follow ? aAtt : aRel) * (rect - follow);

        // Dry-duck follower: fast in, slow out.
        duckEnv += (rect > duckEnv ? aDuckAtt : aDuckRel) * (rect - duckEnv);

        if (onset (follow))
        {
            burstPos = 0;
            burstGain = juce::jmin (sympathy::kBurstGainMax, follow * sympathy::kBurstEnvScale);
            bloomEnv = 1.0f; // snap the Bloom sweep back to dark
            ++triggers;
        }

        bloomEnv *= bloomDecay;

        // The burst *envelope* only - the window's shape and level. Deliberately
        // no noise sample here: if every string were handed the same noise
        // sequence, sixteen resonators filtering an identical broadband signal
        // sum into one loud, undamped, unpitched "click" for as long as it takes
        // them to individually ring up and diverge - which reads as exactly one
        // more "voice" in the mix, the one that never changes pitch. Each string
        // draws its own noise from this envelope instead - see
        // StringVoice::burstNoiseLp and ResonatorBank::render.
        if (burstPos < burstLen)
        {
            const float atk = burstPos < burstAtk
                                  ? static_cast<float> (burstPos) / static_cast<float> (burstAtk)
                                  : 1.0f;
            const float env = std::exp (-sympathy::kBurstDecayShape
                                        * static_cast<float> (burstPos) / static_cast<float> (burstLen));
            burstEnv = atk * env * burstGain;
            ++burstPos;
        }
        else
        {
            burstEnv = 0.0f;
        }

        bleedHpZ += aBleedHp * (xin - bleedHpZ);
        const float bleed = (xin - bleedHpZ) * sympathy::kBleedGain;

        return drive * bleed;
    }

    /** The burst envelope for this sample, already scaled by drive - each
        string multiplies this by its own noise and low-passes it. 0 between
        attacks. */
    float burstEnvelope() const noexcept { return drive * burstEnv; }

    /** One-pole coefficient for the burst lowpass, shared by every string's
        own filter (the state is per-string; the corner is not). */
    float burstLpCoeff() const noexcept { return aBurstLp; }

    /** Effective brightness for the bank: static Damping, darkened right after
        an onset and sweeping back open (Bloom). */
    float bloomBrightness (float bright01) const noexcept
    {
        return bright01 - bloom01 * bloomEnv * (bright01 - sympathy::kBloomFloor);
    }

    /** How far to pull the dry down right now, 0..kDuckDepth. */
    float dryDuck() const noexcept
    {
        return juce::jmin (sympathy::kDuckDepth, duckEnv * sympathy::kDuckDriveScale);
    }

    float envelope() const noexcept { return follow; }
    int   triggerCount() const noexcept { return triggers; }

private:
    static float onePoleCoeff (float hz, float fs) noexcept
    {
        const float w = juce::MathConstants<float>::twoPi * hz / fs;
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    float fs = 44100.0f;
    float aEnvHp = 0.0f, aAtt = 0.0f, aRel = 0.0f;
    float aBleedHp = 0.0f, aBurstLp = 0.0f, aDriveZip = 0.0f;
    float aDuckAtt = 0.0f, aDuckRel = 0.0f;

    float envHpZ = 0.0f, follow = 0.0f;
    float bleedHpZ = 0.0f;
    float duckEnv = 0.0f;

    int   burstLen = 1, burstAtk = 1, burstPos = 1;
    float burstGain = 0.0f, burstEnv = 0.0f;

    float drive = 1.0f, driveTarget = 1.0f;
    float bloom01 = 0.0f, bloomEnv = 0.0f, bloomDecay = 0.0f;

    int triggers = 0;

    OnsetGate onset;
};

// ============================================================================
// ResonatorBank - 16 coupled strings, one exciter, stereo out
// ============================================================================
class ResonatorBank
{
public:
    using TuningMode = sympathy::TuningMode;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? static_cast<float> (sampleRate) : 44100.0f;
        exciter.prepare (sampleRate);
        for (int i = 0; i < sympathy::kNumStrings; ++i)
        {
            auto& v = voices[static_cast<size_t> (i)];
            v.prepare (sampleRate);
            // A distinct, well-mixed seed per string so no two draw the same
            // noise sequence for the burst - see StringVoice::burstNoiseLp().
            v.seedNoise (0x9E3779B9u * static_cast<uint32_t> (i * 2 + 1));
        }

        // Fixed equal-power stereo fan, folded together with the bank output
        // trim. 1/sqrt(16) = 0.25 would be right if all sixteen strings rang
        // equally on every note; in practice a note lights a handful, so the
        // trim sits above that (see kBankOutputGain) to bring the wet up to
        // where Mix can balance it against the dry.
        const float bankNorm = sympathy::kBankOutputGain;
        for (int i = 0; i < sympathy::kNumStrings; ++i)
        {
            const float pan = (static_cast<float> (i) / (sympathy::kNumStrings - 1) * 2.0f - 1.0f)
                              * sympathy::kStereoSpread;
            const float th = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            panL[static_cast<size_t> (i)] = std::cos (th) * bankNorm;
            panR[static_cast<size_t> (i)] = std::sin (th) * bankNorm;
        }

        smCoupling = coupling01;
        smSpread = spread01;
        recomputeTargets (1);
        reset();
    }

    void reset() noexcept
    {
        exciter.reset();
        for (auto& v : voices)
            v.reset();
        peak = 0.0f;
    }

    //== controls ============================================================
    void setTuning (TuningMode mode, int keySemitone) noexcept
    {
        tuningMode = mode;
        key = ((keySemitone % 12) + 12) % 12;
    }
    void setOctave (int octaveShift) noexcept
    {
        octave = juce::jlimit (sympathy::kOctaveMin, sympathy::kOctaveMax, octaveShift);
    }
    void setDecay01 (float d01) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, d01);
        t60Seconds = sympathy::kDecayMinSeconds
                     * std::pow (sympathy::kDecayMaxSeconds / sympathy::kDecayMinSeconds, t);
    }
    void setDamping01 (float b01) noexcept { damping01 = juce::jlimit (0.0f, 1.0f, b01); }
    void setCoupling01 (float c01) noexcept { coupling01 = juce::jlimit (0.0f, 1.0f, c01); }
    void setSpread01 (float s01) noexcept { spread01 = juce::jlimit (0.0f, 1.0f, s01); }
    void setBloom01 (float b01) noexcept { exciter.setBloom01 (b01); }
    void setSensitivity01 (float s01) noexcept { exciter.setSensitivity01 (s01); }
    void setFreeze (bool shouldFreeze) noexcept { freeze = shouldFreeze; }

    //== per-block update ===================================================
    /** Advance control glides and rebuild every string's tuning + tone for a
        block of `numSamples`. Call once before each `render` of the same size. */
    void updateBlock (int numSamples) noexcept
    {
        // Per-block smoothing coefficient - derived from the block length, not a
        // fixed per-sample value applied once a block (which would make the
        // glide depend on the host's buffer size).
        const float k = 1.0f - std::exp (-static_cast<float> (juce::jmax (1, numSamples))
                                         / (fs * sympathy::kControlGlideMs * 0.001f));
        smCoupling += k * (coupling01 - smCoupling);
        smSpread += k * (spread01 - smSpread);
        recomputeTargets (numSamples);
    }

    //== per-sample rendering ==============================================
    /** @param inL,inR   dry input (mono is fine - pass the same pointer twice)
        @param wetL,wetR the bank output; the caller mixes it against the dry
        @param dryGain   optional: per-sample dry level (1 - envelope duck) for
                         the caller's mix stage. Null to skip.
        @param n         sample count for this block (match `updateBlock`) */
    void render (const float* inL, const float* inR, float* wetL, float* wetR,
                 float* dryGain, int n) noexcept
    {
        const float exGain = freeze ? sympathy::kFreezeExciterBleed : 1.0f;
        // Freeze bypasses the coupling blend: the blend trades a little loop
        // gain for movement, which at unity (frozen) loop gain would make a
        // "held" bank slowly decay instead of ring on.
        const float c = freeze ? 0.0f : smCoupling * sympathy::kMaxCoupling;
        // A little output makeup for the sustain the blend trades for movement,
        // so Coupling does not read purely as a level drop.
        const float bankMakeup = freeze ? 1.0f : 1.0f + sympathy::kCouplingMakeup * smCoupling;

        const float burstLpCoeff = exciter.burstLpCoeff();

        for (int s = 0; s < n; ++s)
        {
            const float mono = 0.5f * (inL[s] + inR[s]);
            const float bleed = exciter.process (mono) * exGain;
            const float burstAmt = exciter.burstEnvelope() * exGain;
            if (dryGain != nullptr)
                dryGain[s] = 1.0f - exciter.dryDuck();

            // Phase 1: every string reads its delay and filters, returning its
            // pre-gain loop signal. Each also draws its own noise from the
            // shared burst envelope here, rather than all sixteen sharing one
            // noise sequence - see Exciter::process()'s note on why a shared
            // burst sums into one loud, unpitched click that buries the tuned
            // strings.
            float loop[sympathy::kNumStrings];
            float excite[sympathy::kNumStrings];
            for (int i = 0; i < sympathy::kNumStrings; ++i)
            {
                auto& v = voices[static_cast<size_t> (i)];
                excite[i] = bleed + v.burstNoiseLp (burstAmt, burstLpCoeff);
                loop[i] = v.readFilter();
            }

            // Coupling: an energy-preserving circular-smoother mix, blended into
            // each string's feedback as (1-c)*own + c*mixed. Doubly stochastic,
            // spectral radius 1, so total loop gain stays bounded by
            // kLoopGainMax for any Coupling - a naive additive "sum of
            // neighbours" is the thing that self-oscillates; this is not that.
            float mix[sympathy::kNumStrings];
            for (int i = 0; i < sympathy::kNumStrings; ++i)
                mix[i] = loop[i];
            coupleCircular (mix);

            float l = 0.0f, r = 0.0f;
            for (int i = 0; i < sympathy::kNumStrings; ++i)
            {
                voices[static_cast<size_t> (i)].writeBack ((1.0f - c) * loop[i] + c * mix[i], excite[i]);

                const float y = voices[static_cast<size_t> (i)].outTap();
                l += y * panL[static_cast<size_t> (i)];
                r += y * panR[static_cast<size_t> (i)];
            }

            // Bank-wide soft limiter: odd, smooth, unity slope at the origin.
            l = softLimit (l * bankMakeup);
            r = softLimit (r * bankMakeup);

            if (! std::isfinite (l) || ! std::isfinite (r)
                || std::abs (l) > sympathy::kBankRunawayLevel || std::abs (r) > sympathy::kBankRunawayLevel)
            {
                reset();
                l = r = 0.0f;
            }

            wetL[s] = l;
            wetR[s] = r;
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
        }
    }

    //== introspection for the test/voicing tools =========================
    float dryDuck() const noexcept { return exciter.dryDuck(); }
    int   triggerCount() const noexcept { return exciter.triggerCount(); }
    float lastPeak() const noexcept { return peak; }
    void  clearPeak() noexcept { peak = 0.0f; }
    float voiceF0 (int i) const noexcept { return voices[static_cast<size_t> (juce::jlimit (0, sympathy::kNumStrings - 1, i))].currentF0(); }
    float voiceLoopGain (int i) const noexcept { return voices[static_cast<size_t> (juce::jlimit (0, sympathy::kNumStrings - 1, i))].loopGainValue(); }

private:
    static float softLimit (float x) noexcept
    {
        return x / (1.0f + std::abs (x) * sympathy::kSoftLimitK);
    }

    /** The per-string tuning ratio for `mode`, relative to the key root. Every
        mode's set spans a few octaves; the "cannot clash" modes (octaves,
        fifths, harmonic) contain no scale seconds or thirds. */
    static float ratioForString (TuningMode mode, int i) noexcept
    {
        switch (mode)
        {
            case TuningMode::octaves:
            {
                // Four octaves, four strings each - 16 divides evenly, so no
                // octave gets a surplus string over the others. Range -1..+2
                // rather than -2..+2: the low end of -2 sat right against
                // kMinF0's floor for most keys, which clamped every string
                // in that bucket to the same frequency and collapsed Spread's
                // detune for a quarter of the bank into one coherent, always-
                // on low drone.
                const int oct = (i / 4) - 1;
                return std::pow (2.0f, static_cast<float> (oct));
            }
            case TuningMode::fifths:
            {
                const int deg = i % 2;             // root / fifth
                const int oct = (i / 2) % 4 - 1;   // -1..+2
                return (deg ? 1.5f : 1.0f) * std::pow (2.0f, static_cast<float> (oct));
            }
            case TuningMode::harmonic:
                return static_cast<float> (i + 1) * 0.5f; // harmonics 1..16 over a root an octave down
            case TuningMode::majorJI:
            case TuningMode::minorJI:
            {
                // Seven degrees do not divide 16 evenly. The two leftover
                // strings go to the fourth and the fifth, an octave up - not
                // the tonic: doubling the root a third time (0.5x, 1x, 2x all
                // at once) locks three octave-related strings onto the one
                // pitch every chord shares, and that reinforced tonic drones
                // over the bank regardless of what is played, while the other
                // six degrees sit at only two strings each.
                const auto& tbl = mode == TuningMode::majorJI ? sympathy::kMajorJI : sympathy::kMinorJI;
                if (i < 14)
                {
                    const int deg = i % 7;
                    const int oct = (i / 7) - 1; // -1, 0 - every degree exactly twice
                    return tbl[static_cast<size_t> (deg)] * std::pow (2.0f, static_cast<float> (oct));
                }
                const int extraDeg = (i == 14) ? 3 : 4; // fourth, fifth
                return tbl[static_cast<size_t> (extraDeg)] * 2.0f; // an octave above centre
            }
            case TuningMode::chroma:
            default:
                return std::pow (2.0f, static_cast<float> (i - 8) / 12.0f); // +-8 semitones around the root
        }
    }

    void recomputeTargets (int numSamples) noexcept
    {
        const float rootHz = sympathy::kRootBaseHz
                             * std::pow (2.0f, static_cast<float> (key) / 12.0f)
                             * std::pow (2.0f, static_cast<float> (octave));

        const float bright = exciter.bloomBrightness (damping01);
        const float detune = smSpread * sympathy::kMaxDetuneCents;

        for (int i = 0; i < sympathy::kNumStrings; ++i)
        {
            float f0 = rootHz * ratioForString (tuningMode, i);
            f0 *= std::pow (2.0f, sympathy::kDetunePattern[static_cast<size_t> (i)] * detune / 1200.0f);

            auto& v = voices[static_cast<size_t> (i)];
            v.setTargets (f0, bright, t60Seconds, freeze);
            v.updateBlock (numSamples);
        }
    }

    Exciter exciter;
    std::array<StringVoice, sympathy::kNumStrings> voices;
    std::array<float, sympathy::kNumStrings> panL {}, panR {};

    float fs = 44100.0f;

    TuningMode tuningMode = TuningMode::majorJI;
    int   key = 0;      // 0..11 semitones above the base root
    int   octave = 0;
    float t60Seconds = 4.0f;
    float damping01 = 0.5f;
    float coupling01 = 0.25f, smCoupling = 0.25f;
    float spread01 = 0.25f, smSpread = 0.25f;
    bool  freeze = false;

    float peak = 0.0f;
};

} // namespace ee::dsp
