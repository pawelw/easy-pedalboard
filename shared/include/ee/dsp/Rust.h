#pragma once

#include "ee/dsp/RustConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ee::dsp
{

/** Rust: degradation with a memory.

    A per-channel "wear" state tracks the recent input level - an attack/release
    envelope follower on |x| drives it up while a note sounds and it heals back
    toward zero when it stops - and drives a corrosion chain whose depth is that
    wear times Grind:

      - a short modulated delay (steady sine + slow random walk) blends a
        pitch/time-warped copy of the pre-corrosion input back in - wow/flutter;
      - a tanh grit stage whose drive climbs with wear*grind;
      - bit-depth quantisation and integer sample-and-hold that deepen with
        wear*grind;
      - (Contact only) a hard-clip threshold that wear pulls down, and a deeper
        sample-rate crumble.

    Every stage shapes the signal - nothing is layered on top of it. There is no
    crackle, hiss or dropout generator, so the output falls silent the instant
    the input does, in both voicings.

    Two knobs: Grind (the amount of crumble at full wear) and Tone (a post
    low-pass). Wear and its recovery are fixed at the top of their range - the
    corrosion is always allowed to reach full, and heals in ~1.5 s.

    Chain per channel: warble blend -> grit -> quantise/decimate ->
    (Contact clip) -> post low-pass (Tone, darkened by wear in Oxide).

    Two voicings picked by Mode:
      - Oxide: full warble, and wear darkens the post low-pass. Soft, wobbly.
      - Contact: little warble, no wear darkening, but wear squares the peaks
        with a hard clip and the crumble bites harder - and it is much brighter,
        so the Tone knob is scaled down before the map (kContactToneScale): the
        same knob position lands darker than it would in Oxide.

    At a silent input the wear state stays at zero, the whole chain is skipped,
    and with Tone fully up (in Oxide) the engine is a pass-through. The owning
    module's engine crossfade covers selecting it.

    The only randomness is the warble's slow random walk, a per-channel xorshift
    re-seeded to a fixed value in reset(): a render is reproducible and can be
    checksummed, and the two channels seed differently so the wow decorrelates
    L/R. That warble wet path wanders in time, so under heavy Oxide wear a
    partial module Mix combs it against the dry - that movement is the intent
    here (it is wow), not the tremolo artefact `engineUsesMix` guards Tape
    against.
*/
class Rust
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        envAtk = onePoleCoeff (1000.0f / rust::kEnvAttackMs, sr);
        envRel = onePoleCoeff (1000.0f / rust::kEnvReleaseMs, sr);
        smoothCoeff = onePoleCoeff (1000.0f / rust::kSmoothMs,
                                    sr / static_cast<double> (rust::kControlBlock));

        const float ctrlDt = static_cast<float> (rust::kControlBlock) / static_cast<float> (sr);
        wearAttack  = 1.0f - std::exp (-ctrlDt / (rust::kWearAttackMs * 0.001f));
        wearRelease = 1.0f - std::exp (-ctrlDt / rust::kWearHealSec);

        warbleInc = rust::kWarbleRateHz / static_cast<float> (sr);
        msToSamples = static_cast<float> (sr) * 0.001f;

        lineLen = std::min<int> (kLineCapacity,
                                 std::max (4, static_cast<int> (rust::kWarbleLineMs * 0.001f * sr) + 4));

        applyTone();
        currentToneHz = targetToneHz;
        reset();
    }

    void reset() noexcept
    {
        uint32_t seed = 0x9e3779b9u;

        for (auto& ch : channels)
        {
            ch.line.fill (0.0f);
            ch.writePos = 0;

            ch.env  = 0.0f;
            ch.wear = 0.0f;

            ch.warbleWalk = 0.0f;
            ch.hold = 0;
            ch.held = 0.0f;

            ch.lpZ1 = 0.0f;
            ch.lpZ2 = 0.0f;

            ch.quantStep = 0.0f; // 0 = quantiser off for the block
            ch.decimateN = 1;

            ch.rng = seed;
            seed = seed * 1664525u + 1013904223u;

            updateChannelLp (ch, currentToneHz);
        }

        warblePhase  = 0.0f;
        blockCounter = 0;
    }

    // -------------------------------------------------------------- the knobs

    /** Character of the corrosion at full wear, 0..1: grime to full breakup. */
    void setGrind01 (float g) noexcept { grind = std::clamp (g, 0.0f, 1.0f); }

    /** 0 = Oxide (full warble, wear darkens the tone), 1 = Contact (little
        warble, wear pulls a hard clip down, deeper crumble, and the Tone knob
        is scaled darker). Anything else is Oxide. Neither adds noise. */
    void setMode (int m) noexcept
    {
        mode = (m == rust::kModeContact) ? rust::kModeContact : rust::kModeOxide;
        applyTone();
    }

    /** Post low-pass position, 0..1. In Contact the knob is scaled down by
        kContactToneScale before the map, so the same position lands darker. */
    void setTone01 (float knob01) noexcept
    {
        toneKnob = std::clamp (knob01, 0.0f, 1.0f);
        applyTone();
    }

    // -------------------------------------------------------------- the audio

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };

        const bool  contact      = mode == rust::kModeContact;
        const float warbleWeight = contact ? rust::kWarbleWeightContact : rust::kWarbleWeightOxide;

        for (int i = 0; i < numSamples; ++i)
        {
            const bool ctrlTick = blockCounter == 0;
            if (ctrlTick)
                currentToneHz += smoothCoeff * (targetToneHz - currentToneHz);
            if (++blockCounter >= rust::kControlBlock)
                blockCounter = 0;

            warblePhase += warbleInc;
            warblePhase -= std::floor (warblePhase);
            const float warbleSine = std::sin (warblePhase * kTwoPi);

            for (size_t c = 0; c < channels.size(); ++c)
            {
                auto& ch = channels[c];
                float x = io[c][i];

                // Attack/release envelope on |x| - fast up, slow down - so wear
                // reads "a note is sounding", not the instantaneous sample.
                const float ax = std::fabs (x);
                ch.env += (ax > ch.env ? envAtk : envRel) * (ax - ch.env);

                if (ctrlTick)
                    updateChannelBlock (ch, contact);

                // The warble line always tracks the clean input, so it stays
                // continuous whether or not wear is reading from it.
                ch.line[static_cast<size_t> (ch.writePos)] = x;

                const float w = ch.wear;

                if (w > 1.0e-4f)
                {
                    const float wWt = warbleWeight * w;
                    if (wWt > 1.0e-4f)
                    {
                        float modS = rust::kWarbleBaseMs
                                   + rust::kWarbleDepthMs * wWt
                                         * (0.5f * warbleSine + 0.5f * ch.warbleWalk);
                        modS = std::clamp (modS * msToSamples, 1.0f,
                                           static_cast<float> (lineLen - 3));
                        x += wWt * (readLine (ch, modS) - x);
                    }

                    const float gw = w * grind;

                    if (gw > 1.0e-4f)
                    {
                        const float drive = 1.0f + rust::kGritDrive * gw;
                        x = std::tanh (drive * x) * (1.0f - rust::kGritTrim * gw);
                    }

                    if (ch.decimateN > 1)
                    {
                        if (ch.hold <= 0)
                        {
                            ch.held = x;
                            ch.hold = ch.decimateN;
                        }
                        x = ch.held;
                        --ch.hold;
                    }

                    if (ch.quantStep > 0.0f)
                        x = ch.quantStep * std::floor (x / ch.quantStep + 0.5f);

                    // Contact squares off the peaks as wear climbs - pure signal
                    // shaping, no tail.
                    if (contact)
                    {
                        const float clipT = 1.0f - (1.0f - rust::kContactClipMin) * w;
                        x = std::clamp (x, -clipT, clipT);
                    }
                }

                if (! ch.bypassLp)
                    x = svfLowpass (ch.lpZ1, ch.lpZ2, ch.lp1, ch.lp2, ch.lp3, x);

                io[c][i] = x;

                if (++ch.writePos >= lineLen)
                    ch.writePos = 0;
            }
        }
    }

private:
    // 24 ms at 192 kHz, rounded up with headroom; prepare() clamps lineLen to
    // this and the real sample rate.
    static constexpr int kLineCapacity = 8192;

    struct Channel
    {
        std::array<float, kLineCapacity> line {};
        int writePos = 0;

        float env  = 0.0f; // attack/release |x| envelope that drives wear
        float wear = 0.0f; // the memory, 0..1

        float warbleWalk = 0.0f;
        int   hold = 0;
        float held = 0.0f;

        float lpZ1 = 0.0f, lpZ2 = 0.0f;
        float lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f;
        bool  bypassLp = true;

        float quantStep = 0.0f;
        int   decimateN = 1;

        uint32_t rng = 1u;
    };

    static constexpr float kPi    = 3.14159265359f;
    static constexpr float kTwoPi = 6.28318530718f;
    static constexpr float kQ     = 0.70710678f; // Butterworth

    static float onePoleCoeff (float cornerHz, double sampleRate) noexcept
    {
        const float w = 2.0f * kPi * cornerHz / static_cast<float> (sampleRate);
        return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
    }

    /** xorshift32 in [0, 1) - the same generator BitCrusher / TapeCharacter use. */
    static float nextUniform (uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state) * 2.3283064365386963e-10f;
    }

    static float smoothstep (float a, float b, float v) noexcept
    {
        const float t = std::clamp ((v - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void applyTone() noexcept
    {
        float k = toneKnob;
        if (mode == rust::kModeContact)
            k *= rust::kContactToneScale;
        targetToneHz = rust::toneHzFor (std::clamp (k, 0.0f, 1.0f));
    }

    void updateChannelBlock (Channel& ch, bool contact) noexcept
    {
        // Wear chases a target set by the input envelope through a soft knee -
        // full once the envelope is past kWearKnee, nothing below kWearFloor -
        // and rises slowly toward it, healing slowly back down.
        const float target = smoothstep (rust::kWearFloor, rust::kWearKnee, ch.env);
        const float coeff = target > ch.wear ? wearAttack : wearRelease;
        ch.wear += coeff * (target - ch.wear);

        ch.warbleWalk = std::clamp (ch.warbleWalk + (nextUniform (ch.rng) * 2.0f - 1.0f) * rust::kWarbleWalk,
                                    -1.0f, 1.0f);

        const float gw = ch.wear * grind;

        float decF = static_cast<float> (rust::kMaxDecimate - 1) * gw;
        if (contact)
            decF *= rust::kContactDecimateMul;
        ch.decimateN = gw > 1.0e-3f ? 1 + static_cast<int> (std::lround (decF)) : 1;

        const float bitsF = 16.0f - (16.0f - rust::kMinBits) * gw;
        ch.quantStep = (gw > 1.0e-3f && bitsF < 15.5f) ? 2.0f / std::pow (2.0f, bitsF) : 0.0f;

        float effHz = currentToneHz;
        if (! contact)
            effHz *= 1.0f - rust::kOxideDarken * ch.wear;
        updateChannelLp (ch, effHz);
    }

    void updateChannelLp (Channel& ch, float hz) noexcept
    {
        ch.bypassLp = hz >= rust::kToneBypassHz;
        if (ch.bypassLp)
            return;

        const float nyq = 0.49f * static_cast<float> (sr);
        const float fc  = std::clamp (hz, 10.0f, nyq);
        const float g   = std::tan (kPi * fc / static_cast<float> (sr));
        const float k   = 1.0f / kQ;

        ch.lp1 = 1.0f / (1.0f + g * (g + k));
        ch.lp2 = g * ch.lp1;
        ch.lp3 = g * ch.lp2;
    }

    static float svfLowpass (float& z1, float& z2, float a1, float a2, float a3, float v0) noexcept
    {
        const float v3 = v0 - z2;
        const float v1 = a1 * z1 + a2 * v3;
        const float v2 = z2 + a2 * z1 + a3 * v3;
        z1 = 2.0f * v1 - z1;
        z2 = 2.0f * v2 - z2;
        return v2;
    }

    float readLine (const Channel& ch, float delaySamples) const noexcept
    {
        float rp = static_cast<float> (ch.writePos) - delaySamples;
        while (rp < 0.0f)
            rp += static_cast<float> (lineLen);

        const int i0 = static_cast<int> (rp);
        const float frac = rp - static_cast<float> (i0);
        const int i1 = i0 + 1 >= lineLen ? 0 : i0 + 1;

        return ch.line[static_cast<size_t> (i0)]
             + frac * (ch.line[static_cast<size_t> (i1)] - ch.line[static_cast<size_t> (i0)]);
    }

    double sr = 44100.0;

    float grind    = 0.0f;
    float toneKnob = 1.0f;
    int   mode     = rust::kModeOxide;

    float targetToneHz  = rust::kToneMaxHz;
    float currentToneHz = rust::kToneMaxHz;

    float envAtk      = 1.0f;
    float envRel      = 1.0f;
    float smoothCoeff = 1.0f;
    float wearAttack  = 1.0f;
    float wearRelease = 1.0f;

    float warblePhase = 0.0f;
    float warbleInc   = 0.0f;
    float msToSamples = 44.1f;

    int lineLen = kLineCapacity;
    int blockCounter = 0;

    std::array<Channel, 2> channels;
};

} // namespace ee::dsp
