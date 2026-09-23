#pragma once

#include "ChorusConfig.h"
#include "ModDelayLine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace ee::dsp
{

/** Wide stereo chorus.

    The wet path is fed from a mono sum of the input and read back through a
    handful of modulated delay taps per channel. The right channel's LFOs are
    offset from the left's by the Phase control, so turning Phase up opens the
    stereo image. The dry signal passes straight through untouched; only the wet
    is filtered and spread.

    All voicing lives in ee/dsp/ChorusConfig.h. The four setters map 1:1 to the
    pedal knobs. The LFO free-runs in Hz - this engine is not tempo-aware.

    Pure DSP: no JUCE audio-processor types, so it unit-tests headless the same
    way FdnReverb does.
*/
class Chorus
{
public:
    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        float largestBaseMs = 0.0f;
        for (int v = 0; v < config::kMaxChorusVoices; ++v)
        {
            largestBaseMs = juce::jmax (largestBaseMs, config::kBaseDelayMsLeft[v]);
            largestBaseMs = juce::jmax (largestBaseMs, config::kBaseDelayMsRight[v]);
        }

        const float maxDelaySeconds =
            (largestBaseMs + config::kDepthMaxMs) * 1.0e-3f + 0.002f;

        for (auto& channel : lines)
            for (auto& line : channel)
                line.prepare (sampleRate, maxDelaySeconds);

        hpCoeff = onePoleCoeff (config::kWetHighPassHz);
        lpCoeff = onePoleCoeff (config::kWetLowPassHz);

        rateHzSmooth.reset (sampleRate, config::kSmoothingSeconds);
        depthMsSmooth.reset (sampleRate, config::kSmoothingSeconds);
        phaseOffsetSmooth.reset (sampleRate, config::kSmoothingSeconds);
        mixSmooth.reset (sampleRate, config::kSmoothingSeconds);

        reset();
    }

    void reset()
    {
        for (auto& channel : lines)
            for (auto& line : channel)
                line.reset();

        lfoPhase = 0.0;
        hpZ.fill (0.0f);
        lpZ.fill (0.0f);

        // Whatever the setters are called with before the next process() call
        // should take effect immediately rather than fading in from whatever
        // was set before the reset - only a knob move *during* playback should
        // ramp.
        needsSnap = true;
    }

    void setRateHz (float hz) noexcept
    {
        rateHz = juce::jlimit (config::kRateMinHz, config::kRateMaxHz, hz);
    }

    void setDepth01 (float depth01) noexcept
    {
        depthMs = juce::jlimit (0.0f, 1.0f, depth01) * config::kDepthMaxMs;
    }

    void setPhaseDegrees (float degrees) noexcept
    {
        // The knob reads 0..kMaxPhaseDeg but drives an LFO offset capped at
        // kPhaseSpanCycles, held short of the 0.5-cycle antiphase point where
        // the right channel would mirror the left and the image would collapse.
        const float x = juce::jlimit (0.0f, config::kMaxPhaseDeg, degrees)
                        / config::kMaxPhaseDeg;
        phaseOffsetCycles = x * config::kPhaseSpanCycles;
    }

    void setMix01 (float mix01) noexcept
    {
        mix = juce::jlimit (0.0f, 1.0f, mix01);
    }

    /** Process one block. `inR` may be null for a mono source. The in and out
        pointers may alias. */
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples) noexcept
    {
        if (! std::isfinite (lfoPhase))
            lfoPhase = 0.0;

        const int voices = juce::jlimit (1, config::kMaxChorusVoices,
                                         config::kVoicesPerChannel);
        const float voiceNorm = 1.0f / std::sqrt (static_cast<float> (voices));

        // Rate/Depth/Phase/Mix are read once per block by the caller, so a knob
        // move would otherwise step the delay-tap read position (Depth, Phase)
        // or the dry/wet balance (Mix) instantly - audible as a click. Smoothed
        // per sample instead; only the un-modulated base delays and the LFO's
        // own spread are fixed. The first block after prepare()/reset() snaps
        // straight to whatever was set, so a fresh setup does not fade in.
        if (needsSnap)
        {
            rateHzSmooth.setCurrentAndTargetValue (rateHz);
            depthMsSmooth.setCurrentAndTargetValue (depthMs);
            phaseOffsetSmooth.setCurrentAndTargetValue (phaseOffsetCycles);
            mixSmooth.setCurrentAndTargetValue (mix);
            needsSnap = false;
        }
        else
        {
            rateHzSmooth.setTargetValue (rateHz);
            depthMsSmooth.setTargetValue (depthMs);
            phaseOffsetSmooth.setTargetValue (phaseOffsetCycles);
            mixSmooth.setTargetValue (mix);
        }

        float baseL[config::kMaxChorusVoices];
        float baseR[config::kMaxChorusVoices];
        float voiceOffset[config::kMaxChorusVoices];
        for (int v = 0; v < voices; ++v)
        {
            baseL[v] = config::kBaseDelayMsLeft[v]  * 1.0e-3f * (float) sampleRate;
            baseR[v] = config::kBaseDelayMsRight[v] * 1.0e-3f * (float) sampleRate;
            // Spread the voices across the LFO, but never exactly antiphase.
            voiceOffset[v] = (static_cast<float> (v) / static_cast<float> (voices))
                             * config::kVoicePhaseSpreadCycles;
        }

        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = inL[n];
            const float dryR = inR != nullptr ? inR[n] : inL[n];
            const float mono = 0.5f * (dryL + dryR);

            const float curRateHz = rateHzSmooth.getNextValue();
            const float curDepthSamples =
                depthMsSmooth.getNextValue() * 1.0e-3f * static_cast<float> (sampleRate);
            const float curPhaseOffsetCycles = phaseOffsetSmooth.getNextValue();
            const float curMix = mixSmooth.getNextValue();
            const float dryGain = 1.0f - curMix;
            const float wetGain = curMix;

            const float phase = static_cast<float> (lfoPhase);
            float wetL = 0.0f;
            float wetR = 0.0f;

            for (int v = 0; v < voices; ++v)
            {
                const float modL = std::sin ((phase + voiceOffset[v]) * twoPi);
                const float modR = std::sin ((phase + voiceOffset[v]
                                              + curPhaseOffsetCycles) * twoPi);

                auto& lineL = lines[0][static_cast<size_t> (v)];
                auto& lineR = lines[1][static_cast<size_t> (v)];

                lineL.write (mono);
                lineR.write (mono);

                wetL += lineL.read (baseL[v] + curDepthSamples * modL);
                wetR += lineR.read (baseR[v] + curDepthSamples * modR);

                lineL.advance();
                lineR.advance();
            }

            wetL *= voiceNorm;
            wetR *= voiceNorm;

            // One-pole high-pass then low-pass on each wet channel.
            hpZ[0] += hpCoeff * (wetL - hpZ[0]);  wetL -= hpZ[0];
            hpZ[1] += hpCoeff * (wetR - hpZ[1]);  wetR -= hpZ[1];
            lpZ[0] += lpCoeff * (wetL - lpZ[0]);  wetL  = lpZ[0];
            lpZ[1] += lpCoeff * (wetR - lpZ[1]);  wetR  = lpZ[1];

            outL[n] = dryL * dryGain + wetL * wetGain;
            outR[n] = dryR * dryGain + wetR * wetGain;

            lfoPhase += static_cast<double> (curRateHz) / sampleRate;
            if (lfoPhase >= 1.0)
                lfoPhase -= std::floor (lfoPhase);
        }
    }

private:
    float onePoleCoeff (float hz) const noexcept
    {
        return 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                * hz / static_cast<float> (sampleRate));
    }

    double sampleRate = 44100.0;
    double lfoPhase = 0.0;

    float rateHz  = config::kDefaultRateHz;
    float depthMs = config::kDefaultDepthPct * 0.01f * config::kDepthMaxMs;
    float phaseOffsetCycles =
        (config::kDefaultPhaseDeg / config::kMaxPhaseDeg) * config::kPhaseSpanCycles;
    float mix     = config::kDefaultMixPct * 0.01f;

    juce::SmoothedValue<float> rateHzSmooth;
    juce::SmoothedValue<float> depthMsSmooth;
    juce::SmoothedValue<float> phaseOffsetSmooth;
    juce::SmoothedValue<float> mixSmooth;
    bool needsSnap = true;

    float hpCoeff = 0.01f;
    float lpCoeff = 0.5f;
    std::array<float, 2> hpZ { { 0.0f, 0.0f } };
    std::array<float, 2> lpZ { { 0.0f, 0.0f } };

    // lines[channel][voice]
    std::array<std::array<ModDelayLine, config::kMaxChorusVoices>, 2> lines;
};

} // namespace ee::dsp
