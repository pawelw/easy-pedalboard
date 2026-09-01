#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "LoopDamper.h"
#include "ModDelayLine.h"
#include "SpringConfig.h"

namespace ee::dsp
{

/** Cascade of stretched all-pass sections: the dispersion that turns a plain
    delay loop into a spring.

    Each section is `(z^-D - a) / (1 - a z^-D)` - unity magnitude at every
    frequency, but a group delay that swings between `D(1-|a|)/(1+|a|)` and
    `D(1+|a|)/(1-|a|)` across the band. Stack a dozen of them and a click comes
    out as a chirp; put that inside a feedback loop and the chirp stretches
    further on every bounce, which is the whole spring sound.

    The delays are whole samples on purpose. Interpolating them would cost more
    than the rest of the reverb put together and buys nothing: nothing here ever
    moves, and a fractional dispersion delay is not audible in a signal that has
    already been through twelve of them.
*/
class ChirpChain
{
public:
    /** @param nominalDelaySamples  mean delay of one section
        @param spread              sections are staggered +/- this fraction of it
        @param coefficient         all-pass coefficient; negative chirps upward */
    void prepare (float nominalDelaySamples, float spread, float coefficient)
    {
        coeff = coefficient;
        total = 0;

        for (int i = 0; i < spring::kChirpStages; ++i)
        {
            // Stagger the sections across the spread, shortest first, so the
            // chain's own comb lands on a band rather than one pitch.
            const float t = spring::kChirpStages > 1
                              ? static_cast<float> (i) / static_cast<float> (spring::kChirpStages - 1)
                              : 0.5f;
            const float scale = 1.0f - spread + 2.0f * spread * t;

            auto& stage = stages[static_cast<size_t> (i)];
            stage.size = std::max (1, static_cast<int> (nominalDelaySamples * scale + 0.5f));
            stage.buffer.assign (static_cast<size_t> (stage.size), 0.0f);
            stage.index = 0;

            total += stage.size;
        }
    }

    void reset()
    {
        for (auto& stage : stages)
        {
            std::fill (stage.buffer.begin(), stage.buffer.end(), 0.0f);
            stage.index = 0;
        }
    }

    float process (float x) noexcept
    {
        for (auto& stage : stages)
        {
            const size_t at = static_cast<size_t> (stage.index);

            const float delayed = stage.buffer[at];   // v[n-D]
            const float v = x + coeff * delayed;
            stage.buffer[at] = v;

            if (++stage.index >= stage.size)
                stage.index = 0;

            x = delayed - coeff * v;
        }

        return x;
    }

    /** Mean delay the chain adds, in samples - an all-pass averages its own
        nominal delay across the band. The spring's delay line is shortened by
        this so the round trip stays the length the voicing asked for. */
    int meanDelaySamples() const noexcept { return total; }

private:
    struct Stage
    {
        std::vector<float> buffer;
        int size = 1;
        int index = 0;
    };

    std::array<Stage, spring::kChirpStages> stages;
    float coeff = spring::kChirpCoefficient;
    int total = 0;
};

/** Spring tank: mono in, stereo out.

    Three springs in parallel, each a delay loop with a dispersion chain inside
    it, band-limited on the way in and damped on every pass - a model of an
    Accutronics-style tank rather than a room. Decay time is the only control;
    everything else is voicing, in SpringConfig.h.

    The stereo image is two tanks whose springs differ by a few per cent. A real
    tank is mono, but two of them a hair apart open the tail up without either
    side sounding detuned.
*/
class SpringReverb
{
public:
    static constexpr float kMinDecay = spring::kMinDecaySeconds;
    static constexpr float kMaxDecay = spring::kMaxDecaySeconds;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** RT60 of the tank's mid band, in seconds. Clamped to kMinDecay..kMaxDecay. */
    void setDecayTime (float seconds) noexcept;

    /** Two tanks a few per cent apart, or one tank feeding both outputs. A real
        spring reverb is the mono case; the stereo one is a studio conceit. */
    void setStereo (bool shouldBeStereo) noexcept { stereo = shouldBeStereo; }

    /** @param monoIn  the tank's drive signal
        @param outL    wet only - the caller owns the dry/wet mix */
    void process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept;

    float getTailSeconds() const noexcept { return decaySeconds * 1.3f + 0.15f; }

private:
    /** One spring: a delay line, the dispersion inside its feedback path, and
        the losses the wire takes on every trip. */
    struct Spring
    {
        ModDelayLine line;
        ChirpChain chirp;
        LoopDamper damper;

        float delaySamples = 1.0f;      // line length alone, chirp already deducted
        float modDepthSamples = 0.0f;
        float lfoPhase = 0.0f;
        float lfoInc = 0.0f;

        float loopSeconds = 0.01f;      // whole round trip, for the decay maths
    };

    static constexpr int kTanks = 2;

    void updateFeedback() noexcept;

    float driveFilter (float x) noexcept;
    float outputFilter (float x, int tank) noexcept;

    double sr = 44100.0;
    float decaySeconds = spring::kDefaultDecaySeconds;
    bool stereo = true;

    std::array<std::array<Spring, spring::kSprings>, kTanks> tanks;

    // One-pole coefficients, shared by every spring and both tanks.
    float inputHighCutCoeff = 1.0f;
    float inputLowCutCoeff = 0.0f;
    float outputHighCutCoeff = 1.0f;
    float outputLowCutCoeff = 0.0f;

    float inputHighCutState = 0.0f;
    float inputLowCutState = 0.0f;
    std::array<float, kTanks> outputHighCutState {};
    std::array<float, kTanks> outputLowCutState {};
    std::array<float, kTanks> wetShelfState {};
    float wetShelfCoeff = 0.0f;

    float outputScale = 1.0f;
};

} // namespace ee::dsp
