#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace ee::dsp
{

/** A dual-tap crossfaded delay-line pitch shifter for exact ratio shifts -
    octaves, in practice. The classic "two overlapping grains" technique, but
    calibrated for the whole ratio range rather than the small-angle
    approximation a general semitone shifter takes: a tap's read position
    falls behind (or catches up to) the write position at a constant rate,
    and the *effective* playback speed that gives is 1 minus that rate - true
    symmetrically for going down (ratio < 1, delay growing) and up (ratio > 1,
    delay shrinking) alike, so `setRatio` needs no direction flag or special
    case. A shifter built the other way - reusing the up-shift rate and only
    flipping a sign - is only accurate within a couple of semitones of unison;
    at a full octave down it comes out to an effective rate of exactly zero,
    silence dressed as a pitch shift.

    Two read taps 180 degrees out of phase sweep the line; each resets when
    it has swept the whole window, and the two are crossfaded with an
    equal-power (sine) window so one tap's reset is masked by the other
    sitting at full gain. Every field here is a plain value this class zeroes
    in reset() itself, so - unlike a shifter built from a library class with
    private state - there is nothing left uninitialised for this object to
    inherit garbage through from whatever used its memory before
    construction. */
class OctaveShifter
{
public:
    void prepare (double sampleRate, float windowMs) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        windowSamples = std::clamp (static_cast<int> (windowMs * 0.001 * sr), 64, kLineCapacity - 4);
        lineLen = windowSamples + 4;
        setRatio (ratio);
        reset();
    }

    void reset() noexcept
    {
        line.fill (0.0f);
        writePos = 0;
        phase = 0.0f;
    }

    /** `r` is the desired playback speed relative to real time - 0.25 for two
        octaves down, 0.5 for one down, 2.0 for one up. Never called with
        r == 1: the caller substitutes its own dry signal for that case
        instead (see Rust's Uni step), since a ratio of 1 needs no shifting
        and this class's own crossfade would only add grain artefacts to it
        for nothing. */
    void setRatio (float r) noexcept
    {
        ratio = r;
        phaseInc = windowSamples > 0 ? (1.0f - ratio) / static_cast<float> (windowSamples) : 0.0f;
    }

    float process (float in) noexcept
    {
        line[static_cast<size_t> (writePos)] = in;

        const float phase2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float windowF = static_cast<float> (windowSamples);

        const float out = tapGain (phase) * readTap (phase * windowF)
                         + tapGain (phase2) * readTap (phase2 * windowF);

        phase += phaseInc;
        phase -= std::floor (phase);

        if (++writePos >= lineLen)
            writePos = 0;

        return out;
    }

private:
    static constexpr float kPi = 3.14159265359f;
    static constexpr int kLineCapacity = 16384;

    static float tapGain (float p) noexcept { return std::sin (p * kPi); }

    float readTap (float delaySamples) const noexcept
    {
        float rp = static_cast<float> (writePos) - delaySamples;
        while (rp < 0.0f)
            rp += static_cast<float> (lineLen);

        const int i0 = static_cast<int> (rp);
        const float frac = rp - static_cast<float> (i0);
        const int i1 = i0 + 1 >= lineLen ? 0 : i0 + 1;

        return line[static_cast<size_t> (i0)]
             + frac * (line[static_cast<size_t> (i1)] - line[static_cast<size_t> (i0)]);
    }

    double sr = 44100.0;
    int windowSamples = 2048;
    int lineLen = kLineCapacity;

    float ratio = 1.0f;
    float phaseInc = 0.0f;
    float phase = 0.0f;
    int writePos = 0;

    std::array<float, kLineCapacity> line {};
};

} // namespace ee::dsp
