#pragma once

#include "ee/dsp/Aa4.h"
#include "ee/dsp/ModDelayLine.h"
#include "ee/dsp/TapeCharacter.h"
#include "ee/dsp/TapeMachineConfig.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** A tape machine as a pedal, with every part of it on its own knob.

    Signal path, per sample, per channel:

      1. the whole signal is read off a delay line whose length wanders - a
         ~2 Hz sine, and the slower one the Stereo switch opens (Flutter);
      2. record head: drive into an asymmetric tanh, 2x oversampled, wrapped in
         a record-EQ shelf and its exact inverse so the treble hits the head
         hotter than the bass and distorts first (Saturation);
      3. the tape itself: TapeCharacter, the same stage Peak Delay's Tape knob
         drives, voiced against a reference machine (Wear);
      4. the tape floor: a recording of one, played in a constant loop (Noise);
      5. tilt tone control around a fixed pivot, on a centre-detented knob (Tone);
      6. DC blocker, engaged with the saturation that can leave an offset.

    Wear is deliberately not its own model. It is the delay's tape stage, driven
    by this knob, so the two pedals cannot drift apart: retuning
    ee/dsp/TapeTuning.h moves both.

    The noise bed is a recording of a real machine's floor, handed to the engine
    by the pedal and looped with a crossfaded seam. It is not gated and does not
    ride the programme: a floor that ducks when you play is a noise gate, not a
    tape, so it is there whether anything is playing or not - which is what a
    machine with the transport running actually sounds like. With no recording
    supplied the stage falls back to synthesised band-limited hiss.

    Every other stage is bypassed exactly at its resting knob position, and the
    delay read lands on a whole sample when the transport is still - so with
    Saturation, Wear, Flutter and Noise at 0, Tone centred and Stereo off, the
    machine is bit-exact pass-through with a fixed latency, rather than an
    almost-clean one.

    Both channels share one transport (a capstan wobbles the whole machine, and
    giving each side its own turns the effect into a chorus) - which is exactly
    what the Stereo switch opts into, and why it is a switch rather than part of
    Flutter. Only the noise is seeded per channel, the way two tracks of a real
    tape are.

    Pure DSP: no JUCE audio-processor types, so it unit-tests headless. All
    voicing lives in ee/dsp/TapeMachineConfig.h, bar Wear's, which is the
    delay's own in ee/dsp/TapeTuning.h.
*/
class TapeMachine
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sr = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        // Rounded to a whole sample so that with the transport still the line
        // reads a stored sample rather than an interpolated one, and the stage
        // is bit exact.
        const float nominalSeconds = tape::kNominalDelayMs * 0.001f;
        nominalSamples = std::round (nominalSeconds * static_cast<float> (sr));
        wobbleLimit = tape::kWobbleLimit * nominalSamples;

        const double osRate = sr * tape::kOversampleFactor;

        for (auto& c : channels)
        {
            c.line.prepare (sr, nominalSeconds * 3.0f);
            c.aaUp.setup (tape::kOversampleCutoffHz, static_cast<float> (osRate));
            c.aaDown.setup (tape::kOversampleCutoffHz, static_cast<float> (osRate));
        }

        wearStage.prepare (sr);

        // The record EQ runs inside the oversampled region, so its corner is
        // set at the oversampled rate.
        emphasisCoeff = onePoleCoeff (tape::kEmphasisPivotHz, osRate);

        toneCoeff    = onePoleCoeff (tape::kTonePivotHz, sr);
        noiseHpCoeff = onePoleCoeff (tape::kHissHighpassHz, sr);
        noiseLpCoeff = onePoleCoeff (tape::kHissLowpassHz, sr);
        dcCoeff      = onePoleCoeff (tape::kDcBlockerHz, sr);
        paramCoeff   = onePoleCoeff (tape::kParamSmoothingHz, sr);

        wowInc    = static_cast<float> (tape::kWowRateHz / sr);
        stereoInc = static_cast<float> (tape::kStereoRateHz / sr);

        setNoiseFilter (wowJitterCoeff, wowJitterNorm, tape::kWowJitterHz);

        const float perMs = static_cast<float> (sr) * 0.001f;
        wowDepthSamples    = tape::kWowDepthMs * perMs;
        stereoDepthSamples = tape::kStereoDepthMs * perMs;

        updateNoiseLoop();

        snapSmoothing();
        updateControls();
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.line.reset();
            c.aaUp.reset();
            c.aaDown.reset();
            c.preEmphasisLp = 0.0f;
            c.deX1 = c.deY1 = 0.0f;
            c.toneLp = 0.0f;
            c.noiseHp = c.noiseLp = c.noiseLp2 = 0.0f;
            c.dc = 0.0f;
        }

        wearStage.reset();

        uint32_t seed = 0x9e3779b9u;
        for (auto& c : channels)
        {
            c.rng = seed;
            seed = seed * 1664525u + 1013904223u;
        }

        for (size_t c = 0; c < channels.size(); ++c)
            channels[c].noiseRead = c == 1 && noiseTableChannels < 2
                                        ? noiseLoopLength * 0.5   // decorrelate a mono floor
                                        : 0.0;

        modRng = 0x2545f491u;
        wowPhase = 0.0f;
        stereoPhase = 0.0f;
        wowJitterState = 0.0f;
        scratch.fill (0.0f);
    }

    /** How tired the tape is. Drives TapeCharacter - the same stage, and the
        same voicing, as Peak Delay's Tape knob. */
    void setWear01 (float v) noexcept       { wearTarget = juce::jlimit (0.0f, 1.0f, v); }

    /** Depth of the wow, flutter and scrape riding the transport. */
    void setFlutter01 (float v) noexcept    { flutterTarget = juce::jlimit (0.0f, 1.0f, v); }

    /** -1 = dark, 0 = flat and bypassed, +1 = bright. A tilt around the pivot. */
    void setTone (float v) noexcept         { toneTarget = juce::jlimit (-1.0f, 1.0f, v); }

    /** Opens the two channels onto different points of a slow modulation, which
        widens the image the way a chorus does. A switch, not a knob: 0 is one
        transport for both sides, 1 is full width. */
    void setStereo01 (float v) noexcept     { stereoTarget = juce::jlimit (0.0f, 1.0f, v); }

    /** The tape floor. Constant: it does not ride the programme. */
    void setNoise01 (float v) noexcept      { noiseTarget = juce::jlimit (0.0f, 1.0f, v); }

    /** Hands the machine a recording of a tape floor to loop, instead of the
        synthesised fallback. One pointer per channel, not owned - the caller
        keeps the samples alive for as long as the machine runs. A mono sample
        feeds both channels from different points of the loop, so the floor is
        decorrelated the way two tracks of a real tape are; a stereo one is used
        as it was recorded. Pass nullptr to go back to the fallback.

        Safe to call before prepare(); the read rate is worked out from both. */
    void setNoiseSample (const float* const* channelData, int numChannels,
                         int numSamples, double sampleRateOfSample) noexcept
    {
        noiseTable = channelData;
        noiseTableChannels = channelData != nullptr ? juce::jmin (2, numChannels) : 0;
        noiseTableLength = channelData != nullptr ? numSamples : 0;
        noiseTableRate = sampleRateOfSample > 0.0 ? sampleRateOfSample : 44100.0;

        updateNoiseLoop();
        updateControls();   // the floor's gain law depends on which source it is
    }

    /** How hard the record head is driven. */
    void setSaturation01 (float v) noexcept { satTarget = juce::jlimit (0.0f, 1.0f, v); }

    /** Constant, so the host can compensate it whatever the knobs are doing.
        The transport line plus the tape stage's own. */
    int getLatencySamples() const noexcept
    {
        return static_cast<int> (nominalSamples) + wearStage.getLatencySamples();
    }

    /** In-place, one channel per pointer. `right` may be null for mono. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        const int numCh = right != nullptr ? 2 : 1;

        for (int c = 0; c < numCh; ++c)
            scrubIfBroken (channels[static_cast<size_t> (c)]);

        // 1-2: transport, then the record head.
        for (int i = 0; i < numSamples; ++i)
        {
            advanceSmoothing();
            refreshControls();

            float wobble[2];
            transportOffset (wobble);

            for (int c = 0; c < numCh; ++c)
            {
                auto& ch = channels[static_cast<size_t> (c)];

                float x = io[c][i];
                if (! std::isfinite (x))
                    x = 0.0f;

                ch.line.write (x);
                float y = ch.line.read (nominalSamples + wobble[c]);
                ch.line.advance();

                if (satEngaged)
                    y = saturate (ch, y);

                io[c][i] = y;
            }
        }

        // 3: the tape. TapeCharacter always works in pairs, so a mono call gets
        // a throwaway right channel rather than a special case in the engine.
        wearStage.setAmount (wear);

        if (numCh == 2)
        {
            wearStage.process (left, right, numSamples);
        }
        else
        {
            for (int done = 0; done < numSamples; done += kScratchSamples)
            {
                const int chunk = juce::jmin (kScratchSamples, numSamples - done);
                std::fill_n (scratch.begin(), chunk, 0.0f);
                wearStage.process (left + done, scratch.data(), chunk);
            }
        }

        // 4-6: floor, tone, DC.
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                auto& ch = channels[static_cast<size_t> (c)];
                float y = io[c][i];

                if (noiseEngaged)
                    y += floorSample (ch, c);

                if (toneEngaged)
                {
                    ch.toneLp += toneCoeff * (y - ch.toneLp);
                    y = ch.toneLp * toneLowGain + (y - ch.toneLp) * toneHighGain;
                }

                // Only the shaper's bias asymmetry can leave an offset behind,
                // so the blocker arrives with it rather than sitting on a signal
                // nothing has touched.
                if (satEngaged)
                {
                    ch.dc += dcCoeff * (y - ch.dc);
                    y -= ch.dc;
                }

                if (! std::isfinite (y))
                {
                    scrub (ch);
                    y = 0.0f;
                }

                io[c][i] = y;
            }
        }
    }

private:
    //==========================================================================
    struct Channel
    {
        ModDelayLine line;
        Aa4 aaUp, aaDown;

        float preEmphasisLp = 0.0f;
        float deX1 = 0.0f, deY1 = 0.0f;

        float toneLp = 0.0f;

        float noiseHp = 0.0f, noiseLp = 0.0f, noiseLp2 = 0.0f;
        double noiseRead = 0.0;

        float dc = 0.0f;

        uint32_t rng = 1u;
    };

    static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
    static constexpr int kScratchSamples = 256;

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

    //==========================================================================
    static inline void smooth (float& value, float target, float coeff) noexcept
    {
        value += coeff * (target - value);

        // Snap once the move is inaudible, so a stage that should be at rest
        // reaches its resting value exactly and is bypassed rather than left
        // running at a millionth of its depth.
        if (std::abs (target - value) < 1.0e-5f)
            value = target;
    }

    void snapSmoothing() noexcept
    {
        wear = wearTarget;
        flutter = flutterTarget;
        tone = toneTarget;
        stereo = stereoTarget;
        noise = noiseTarget;
        sat = satTarget;
    }

    void advanceSmoothing() noexcept
    {
        smooth (wear, wearTarget, paramCoeff);
        smooth (flutter, flutterTarget, paramCoeff);
        smooth (tone, toneTarget, paramCoeff);
        smooth (stereo, stereoTarget, paramCoeff);
        smooth (noise, noiseTarget, paramCoeff);
        smooth (sat, satTarget, paramCoeff);
    }

    /** The derived values only move when a knob does, and the shaper's are not
        cheap - so they are recomputed when the smoothing has actually changed
        something, not once a sample regardless. */
    void refreshControls() noexcept
    {
        if (sat == lastSat && tone == lastTone && noise == lastNoise)
            return;

        updateControls();
    }

    void updateControls() noexcept
    {
        lastSat = sat;
        lastTone = tone;
        lastNoise = noise;

        // --- record head -----------------------------------------------------
        satEngaged = sat > 0.0f;
        driveGain = tape::kSatMinDrive + sat * (tape::kSatMaxDrive - tape::kSatMinDrive);
        satBias = sat * tape::kSatBias;
        satBiasOffset = std::tanh (satBias);
        emphasisGain = 1.0f + sat * (tape::kEmphasisGain - 1.0f);

        // Level-matched where it matters: normalise the curve's response to a
        // reference level against what the same level would have read with the
        // knob down, so pushing the head adds harmonics and squash rather than
        // taking level away. Exactly 1 at Saturation 0, whatever the voicing.
        const float reference = std::tanh (tape::kSatMakeupLevel);
        const float driven = std::tanh (driveGain * tape::kSatMakeupLevel + satBias) - satBiasOffset;
        satMakeup = tape::kSatMakeupTrim * reference / std::max (1.0e-4f, driven);

        // Exact inverse of the pre-emphasis shelf: with L the one-pole low-pass,
        // the shelf is H = g + (1 - g) L, so the replay side runs 1 / H.
        const float g = emphasisGain;
        const float c = emphasisCoeff;
        emphasisX1 = 1.0f - c;
        emphasisB = g * (1.0f - c);
        emphasisInvA = 1.0f / std::max (1.0e-6f, g * (1.0f - c) + c);

        // --- tape floor ------------------------------------------------------
        noiseEngaged = noise > 0.0f;

        // Straight linear on the recording, up to the ceiling the voicing sets -
        // at 1.0 that is the floor exactly as it was recorded. The synthesised
        // fallback carries its own scale.
        hissGain = noiseTable != nullptr ? noise * tape::kMaxFloorGain
                                         : tape::kMaxHiss * noise * noise;

        // --- tone ------------------------------------------------------------
        // Exactly centred is flat and bypassed; a hair off it is not, so there
        // is no dead band around the middle. The knob snaps onto the centre in
        // the UI, which is what makes that usable.
        toneEngaged = tone != 0.0f;

        const float tilt = 0.5f + 0.5f * tone;   // -1..1 -> 0..1
        toneLowGain  = juce::jmap (tilt, tape::kToneLowGainDark,  tape::kToneLowGainBright);
        toneHighGain = juce::jmap (tilt, tape::kToneHighGainDark, tape::kToneHighGainBright);
    }

    //==========================================================================
    /** Fills the per-channel read offsets, in samples. */
    void transportOffset (float (&out)[2]) noexcept
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
        const float common = flutter * std::sin (kTwoPi * wowPhase) * wowDepthSamples;

        // Width: the two sides read the same slow modulation a third of a cycle
        // apart, so they pull away from each other without ever mirroring.
        const float widthDepth = stereo * stereoDepthSamples;

        for (int c = 0; c < 2; ++c)
        {
            const float phase = stereoPhase + (c == 1 ? tape::kStereoPhaseSpanCycles : 0.0f);
            const float offset = common + widthDepth * std::sin (kTwoPi * phase);
            out[c] = juce::jlimit (-wobbleLimit, wobbleLimit, offset);
        }
    }

    float shape (Channel& ch, float v) noexcept
    {
        // Record EQ: lift the top end going into the head, so the treble
        // saturates before the bass does.
        ch.preEmphasisLp += emphasisCoeff * (v - ch.preEmphasisLp);
        const float pre = ch.preEmphasisLp + emphasisGain * (v - ch.preEmphasisLp);

        // The bias is what puts the even harmonics in; subtracting tanh(bias)
        // keeps the curve through the origin so quiet passages stay quiet.
        const float shaped = (std::tanh (pre + satBias) - satBiasOffset) * satMakeup;

        // Replay EQ: the exact inverse of the shelf above, which takes the lift
        // back out and leaves the extra harmonics behind.
        const float out = (shaped - emphasisX1 * ch.deX1 + emphasisB * ch.deY1) * emphasisInvA;
        ch.deX1 = shaped;
        ch.deY1 = out;
        return out;
    }

    float saturate (Channel& ch, float x) noexcept
    {
        const float driven = driveGain * x;

        if (tape::kOversampleFactor == 2)
        {
            const float u0 = ch.aaUp.process (2.0f * driven);
            const float u1 = ch.aaUp.process (0.0f);
            const float y0 = shape (ch, u0);
            const float y1 = shape (ch, u1);
            const float y = ch.aaDown.process (y0);
            ch.aaDown.process (y1);
            return y;
        }

        return shape (ch, driven);
    }

    /** The tape floor, from the recording if there is one. */
    float floorSample (Channel& ch, int channelIndex) noexcept
    {
        if (noiseTable == nullptr || noiseLoopLength <= 1.0)
            return syntheticHiss (ch) * hissGain;

        const float* table = noiseTable[static_cast<size_t> (
            juce::jmin (channelIndex, noiseTableChannels - 1))];

        if (table == nullptr)
            return 0.0f;

        const double position = ch.noiseRead;
        float value = readTable (table, position);

        // Seam: the head of the loop crossfades with the tail that follows it,
        // so the wrap cannot tick however the recording was trimmed.
        if (position < noiseFadeLength && noiseFadeLength > 0.0)
        {
            const float t = static_cast<float> (position / noiseFadeLength);
            value = value * t + readTable (table, position + noiseLoopLength) * (1.0f - t);
        }

        ch.noiseRead += noiseIncrement;
        if (ch.noiseRead >= noiseLoopLength)
            ch.noiseRead -= noiseLoopLength;

        return value * hissGain;
    }

    /** Linear read; exact when the recording and the session share a rate. */
    float readTable (const float* table, double position) const noexcept
    {
        const int i0 = static_cast<int> (position);
        if (i0 < 0 || i0 >= noiseTableLength)
            return 0.0f;

        const int i1 = juce::jmin (i0 + 1, noiseTableLength - 1);
        const float frac = static_cast<float> (position - i0);
        return table[i0] + (table[i1] - table[i0]) * frac;
    }

    /** Band-limited white noise, for when there is no recording to play. */
    float syntheticHiss (Channel& ch) noexcept
    {
        float n = whiteNoise (ch.rng);
        ch.noiseHp += noiseHpCoeff * (n - ch.noiseHp);
        n -= ch.noiseHp;
        ch.noiseLp += noiseLpCoeff * (n - ch.noiseLp);
        ch.noiseLp2 += noiseLpCoeff * (ch.noiseLp - ch.noiseLp2);

        return ch.noiseLp2;
    }

    void updateNoiseLoop() noexcept
    {
        noiseIncrement = noiseTableRate / sr;

        const double fade = tape::kNoiseLoopFadeMs * 0.001 * noiseTableRate;

        // The loop is the file minus the crossfade tail, which is what the head
        // fades in against.
        noiseFadeLength = juce::jlimit (0.0, noiseTableLength * 0.25, fade);
        noiseLoopLength = juce::jmax (0.0, noiseTableLength - noiseFadeLength - 1.0);

        for (auto& c : channels)
            if (c.noiseRead >= noiseLoopLength)
                c.noiseRead = 0.0;
    }

    static void scrub (Channel& ch) noexcept
    {
        ch.aaUp.reset();
        ch.aaDown.reset();
        ch.preEmphasisLp = ch.deX1 = ch.deY1 = 0.0f;
        ch.toneLp = 0.0f;
        ch.noiseHp = ch.noiseLp = ch.noiseLp2 = 0.0f;
        ch.dc = 0.0f;
    }

    /** A non-finite sample can only arrive from outside, but the filters store
        it once it does. Caught at the top of every block as well as per sample,
        so nothing can ring on forever. */
    static void scrubIfBroken (Channel& ch) noexcept
    {
        const bool ok = std::isfinite (ch.deY1) && std::isfinite (ch.toneLp)
                        && std::isfinite (ch.dc) && std::isfinite (ch.preEmphasisLp);

        if (! ok)
            scrub (ch);
    }

    //==========================================================================
    double sr = 44100.0;

    std::array<Channel, 2> channels;
    TapeCharacter wearStage;
    std::array<float, kScratchSamples> scratch {};

    // Knob targets and their smoothed values.
    float wearTarget = 0.0f, flutterTarget = 0.0f, toneTarget = 0.0f;
    float stereoTarget = 0.0f, noiseTarget = 0.0f, satTarget = 0.0f;
    float wear = 0.0f, flutter = 0.0f, tone = 0.0f;
    float stereo = 0.0f, noise = 0.0f, sat = 0.0f;
    float paramCoeff = 0.0f;

    // What the derived values below were last computed from.
    float lastSat = -1.0f, lastTone = -2.0f, lastNoise = -1.0f;

    // Transport.
    float nominalSamples = 198.0f;
    float wobbleLimit = 0.0f;
    float wowPhase = 0.0f, wowInc = 0.0f;
    float stereoPhase = 0.0f, stereoInc = 0.0f;
    float wowJitterState = 0.0f, wowJitterCoeff = 0.0f, wowJitterNorm = 1.0f;
    float wowDepthSamples = 0.0f, stereoDepthSamples = 0.0f;
    uint32_t modRng = 0x2545f491u;

    // Record head.
    bool satEngaged = false;
    float driveGain = 1.0f, satBias = 0.0f, satBiasOffset = 0.0f, satMakeup = 1.0f;
    float emphasisCoeff = 0.0f, emphasisGain = 1.0f;
    float emphasisX1 = 0.0f, emphasisB = 0.0f, emphasisInvA = 1.0f;

    // Tape floor.
    bool noiseEngaged = false;
    float hissGain = 0.0f;
    float noiseHpCoeff = 0.0f, noiseLpCoeff = 0.0f;

    const float* const* noiseTable = nullptr;   // not owned
    int noiseTableChannels = 0;
    int noiseTableLength = 0;
    double noiseTableRate = 44100.0;
    double noiseIncrement = 1.0;
    double noiseLoopLength = 0.0;
    double noiseFadeLength = 0.0;

    // Tone.
    bool toneEngaged = false;
    float toneCoeff = 0.0f, toneLowGain = 1.0f, toneHighGain = 1.0f;

    float dcCoeff = 0.0f;
};

} // namespace ee::dsp
