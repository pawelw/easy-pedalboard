#pragma once

#include "ee/dsp/ModDelayLine.h"
#include "ee/dsp/TapeMachineConfig.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** A tape transport: the whole signal read off a delay line whose length
    wanders, which is what a real capstan does to pitch.

    This is BitBit Tape's Flutter stage, lifted out of TapeMachine so BitBit Delay's
    Flutter knob drives the very same code rather than a second model of it -
    the same reason BitBit Tape's Wear is TapeCharacter and not a copy of it. The
    voicing is ee/dsp/TapeMachineConfig.h's TRANSPORT and STEREO blocks, so
    retuning either moves both pedals together.

    Both channels share one wow oscillator - a capstan wobbles the whole
    machine, and giving each side its own turns the effect into a chorus. That
    is exactly what `setStereo01` opts into, which is why it is a separate
    control rather than part of Flutter. BitBit Delay leaves it at 0 and gets one
    transport under both sides; BitBit Tape puts it on its Stereo switch.

    At Flutter and Stereo 0 the read lands on a whole sample, so the stage is
    bit-exact pass-through with a fixed latency rather than an almost-clean one.
*/
class TapeTransport
{
public:
    /** The transport's delay in whole samples, at a sample rate and in either
        mode. Static so it can be worked out without a prepared transport. */
    static int latencyFor (double sampleRate, bool lowLatency) noexcept
    {
        return static_cast<int> (nominalSamplesFor (sampleRate > 0.0 ? sampleRate : 44100.0, lowLatency));
    }

    void prepare (double sampleRateIn) noexcept
    {
        sr = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        // The lines are sized for the longer of the two modes, always.
        const float nominalSeconds = tape::kNominalDelayMs * 0.001f;

        for (auto& c : channels)
            c.prepare (sr, nominalSeconds * 3.0f);

        paramCoeff = onePoleCoeff (tape::kParamSmoothingHz, sr);

        wowInc    = static_cast<float> (tape::kWowRateHz / sr);
        stereoInc = static_cast<float> (tape::kStereoRateHz / sr);

        setNoiseFilter (wowJitterCoeff, wowJitterNorm, tape::kWowJitterHz);

        const float perMs = static_cast<float> (sr) * 0.001f;
        wowDepthSamples    = tape::kWowDepthMs * perMs;
        stereoDepthSamples = tape::kStereoDepthMs * perMs;

        applyNominal();
        snapSmoothing();
        reset();
    }

    /** Shortens the transport's line to tape::kLowLatencyNominalDelayMs. A mode
        an owner chooses once, before prepare() - BitBit Delay's tape and
        BitBit Modulation's run it, BitBit Tape does not. Changing it while
        audio is running is a splice in the signal, so nothing here does. */
    void setLowLatency (bool v) noexcept
    {
        if (lowLatency == v)
            return;

        lowLatency = v;
        applyNominal();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
            c.reset();

        modRng = 0x2545f491u;
        wowPhase = 0.0f;
        stereoPhase = 0.0f;
        wowJitterState = 0.0f;
    }

    /** Depth of the wow riding the transport. */
    void setFlutter01 (float v) noexcept { flutterTarget = juce::jlimit (0.0f, 1.0f, v); }

    /** Opens the two channels onto different points of a second, slower
        modulation, which widens the image the way a chorus does. */
    void setStereo01 (float v) noexcept  { stereoTarget = juce::jlimit (0.0f, 1.0f, v); }

    /** Drops the knob smoothing onto its targets, for the first block after a
        state load. prepare() already does it. */
    void snapSmoothing() noexcept
    {
        flutter = flutterTarget;
        stereo = stereoTarget;
    }

    /** Constant, so the host can compensate it whatever the knobs are doing. */
    int getLatencySamples() const noexcept { return static_cast<int> (nominalSamples); }

    /** In-place, one channel per pointer. `right` may be null for mono. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        const int numCh = right != nullptr ? 2 : 1;

        for (int i = 0; i < numSamples; ++i)
        {
            advanceSmoothing();

            float wobble[2];
            offsets (wobble);

            for (int c = 0; c < numCh; ++c)
            {
                auto& line = channels[static_cast<size_t> (c)];

                float x = io[c][i];
                if (! std::isfinite (x))
                    x = 0.0f;

                line.write (x);
                io[c][i] = line.read (nominalSamples + wobble[c]);
                line.advance();
            }
        }
    }

private:
    static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    /** Rounded to a whole sample so that with the transport still the line
        reads a stored sample rather than an interpolated one. */
    static float nominalSamplesFor (double sampleRate, bool low) noexcept
    {
        const float nominalSeconds = (low ? tape::kLowLatencyNominalDelayMs : tape::kNominalDelayMs) * 0.001f;
        return std::round (nominalSeconds * static_cast<float> (sampleRate));
    }

    /** What follows from the line's length: the read point and the ceiling on
        the excursion. How far the depths must give way to that ceiling is worked
        out per sample, from where the knobs actually are - see offsets(). */
    void applyNominal() noexcept
    {
        nominalSamples = nominalSamplesFor (sr, lowLatency);
        wobbleLimit = tape::kWobbleLimit * nominalSamples;
    }

    void advanceSmoothing() noexcept
    {
        smooth (flutter, flutterTarget, paramCoeff);
        smooth (stereo, stereoTarget, paramCoeff);
    }

    static float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = kTwoPi * cornerHz / static_cast<float> (sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    /** A one-pole on white noise has variance c / (2 - c); the normaliser undoes
        it, so a depth setting in milliseconds means what it says. */
    void setNoiseFilter (float& coeff, float& norm, float cornerHz) const noexcept
    {
        coeff = onePoleCoeff (std::max (0.02f, cornerHz), sr);
        norm = std::sqrt ((2.0f - coeff) / coeff);
    }

    static float whiteNoise (uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * 4.6566129e-10f;
    }

    static inline void smooth (float& value, float target, float coeff) noexcept
    {
        value += coeff * (target - value);

        // Snap once the move is inaudible, so a stage that should be at rest
        // reaches its resting value exactly and is bypassed rather than left
        // running at a millionth of its depth.
        if (std::abs (target - value) < 1.0e-5f)
            value = target;
    }

    /** Fills the per-channel read offsets, in samples. */
    void offsets (float (&out)[2]) noexcept
    {
        // The capstan's own rate wanders, so the wow never locks into an LFO.
        wowJitterState += wowJitterCoeff * (whiteNoise (modRng) - wowJitterState);
        const float jitter = 1.0f + tape::kWowRateJitter * wowJitterState * wowJitterNorm;

        wowPhase += wowInc * jitter;
        wowPhase -= std::floor (wowPhase);

        stereoPhase += stereoInc;
        stereoPhase -= std::floor (stereoPhase);

        // Shared by both channels: one transport under the whole machine. The
        // sine and nothing else - noise riding it roughens the vibe rather than
        // deepening it.
        float common = flutter * std::sin (kTwoPi * wowPhase) * wowDepthSamples;

        // Width: the two sides read the same slow modulation a third of a cycle
        // apart, so they pull away from each other without ever mirroring.
        float widthDepth = stereo * stereoDepthSamples;

        // In low latency the line is short, and the two depths together can ask
        // for more swing than it has. They are pulled back by the same factor,
        // exactly as far as it takes for their sum - the most the two sines can
        // ever add up to - to sit inside the ceiling, and no further: at any
        // setting that fits, the scale is not applied at all. Never in the
        // normal mode, whose line is long enough for both at full.
        if (lowLatency)
        {
            const float widest = flutter * wowDepthSamples + stereo * stereoDepthSamples;

            if (widest > wobbleLimit)
            {
                const float scale = wobbleLimit / widest;
                common *= scale;
                widthDepth *= scale;
            }
        }

        for (int c = 0; c < 2; ++c)
        {
            const float phase = stereoPhase + (c == 1 ? tape::kStereoPhaseSpanCycles : 0.0f);
            const float offset = common + widthDepth * std::sin (kTwoPi * phase);
            out[c] = juce::jlimit (-wobbleLimit, wobbleLimit, offset);
        }
    }

    double sr = 44100.0;

    std::array<ModDelayLine, 2> channels;

    float flutterTarget = 0.0f, stereoTarget = 0.0f;
    float flutter = 0.0f, stereo = 0.0f;
    float paramCoeff = 0.0f;

    float nominalSamples = 198.0f;
    float wobbleLimit = 0.0f;
    float wowPhase = 0.0f, wowInc = 0.0f;
    float stereoPhase = 0.0f, stereoInc = 0.0f;
    float wowJitterState = 0.0f, wowJitterCoeff = 0.0f, wowJitterNorm = 1.0f;
    float wowDepthSamples = 0.0f, stereoDepthSamples = 0.0f;
    bool lowLatency = false;
    uint32_t modRng = 0x2545f491u;
};

} // namespace ee::dsp
