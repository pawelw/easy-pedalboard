#pragma once

/*
    Adapted from daisysp::PitchShifter (DaisySP v1.0.0, Effects/pitchshifter.h).

    Copyright (c) 2020 Electrosmith, Corp
    Used under the MIT licence; see the DaisySP LICENSE, which the third-party
    notices file carries.

    Why a copy rather than the original. Two things in it make a shimmered
    render differ from run to run, and one is a data race:

      - `myrand()` is one function-local `static uint32_t seed`, advanced from
        inside Process(). Every instance in the process shares it, so a render's
        modulation depends on what else has run before it, and two plugin
        instances on different audio threads write it concurrently.
      - The constructor is user-provided and empty, so `prev_phs_a_/b_`,
        `mod_a_amt_/b_`, `slewed_mod_`, `mod_coeff_` and `transpose_` are never
        set by Init() and start as whatever the allocation held. The delay lines
        are large enough that they sit past the range a sanitizer's allocation
        fill covers, so nothing flags it.

    This version gives each instance its own generator, seeded in Init(), and
    sets every member there. Everything else - the delay lines, the crossfade,
    the transposition maths (bar the downward ratio, see SetTransposition) - is
    unchanged, so the voicing is too; only the
    sequence of random values differs from what the shared generator produced,
    and that sequence was never reproducible to begin with.
*/

#include "Control/phasor.h"
#include "Utility/delayline.h"
#include "Utility/dsp.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ee::dsp
{

class ShimmerPitchShifter
{
public:
    /** Shift can be 30-100 ms; 0.050 * SR = 2400 samples at 48 kHz. */
    static constexpr uint32_t kShiftBufferSize = 16384;

    /** Different seeds give different (but each reproducible) modulation, so the
        left and right shifters of one reverb do not flutter in lock-step. */
    explicit ShimmerPitchShifter (uint32_t seedIn = 1u) noexcept
        : seed (seedIn != 0u ? seedIn : 1u), rng (seed) {}

    void Init (float sr)
    {
        force_recalc_ = false;
        sr_ = sr;
        mod_freq_ = 5.0f;
        SetSemitones();

        for (uint8_t i = 0; i < 2; ++i)
        {
            gain_[i] = 0.0f;
            mod_[i] = 0.0f;
            slewed_mod_[i] = 0.0f;
            mod_coeff_[i] = 0.0f;
            d_[i].Init();
            phs_[i].Init (sr, 50, i == 0 ? 0 : PI_F);
        }

        shift_up_ = true;
        transpose_ = 0.0f;
        pitch_shift_ = 0.0f;
        del_size_ = kShiftBufferSize;
        mod_a_amt_ = 0.0f;
        mod_b_amt_ = 0.0f;
        prev_phs_a_ = 0.0f;
        prev_phs_b_ = 0.0f;
        rng = seed;

        SetDelSize (del_size_);
        fun_ = 0.0f;
    }

    float Process (float& in)
    {
        float val, fade1, fade2;

        fade1 = phs_[0].Process();
        fade2 = phs_[1].Process();

        if (prev_phs_a_ > fade1)
        {
            mod_a_amt_ = fun_ * ((float) (nextRandom() % 255) / 255.0f) * (del_size_ * 0.5f);
            mod_coeff_[0] = 0.0002f + (((float) (nextRandom() % 255) / 255.0f) * 0.001f);
        }

        if (prev_phs_b_ > fade2)
        {
            mod_b_amt_ = fun_ * ((float) (nextRandom() % 255) / 255.0f) * (del_size_ * 0.5f);
            mod_coeff_[1] = 0.0002f + (((float) (nextRandom() % 255) / 255.0f) * 0.001f);
        }

        slewed_mod_[0] += mod_coeff_[0] * (mod_a_amt_ - slewed_mod_[0]);
        slewed_mod_[1] += mod_coeff_[1] * (mod_b_amt_ - slewed_mod_[1]);
        prev_phs_a_ = fade1;
        prev_phs_b_ = fade2;

        if (shift_up_)
        {
            fade1 = 1.0f - fade1;
            fade2 = 1.0f - fade2;
        }

        mod_[0] = fade1 * (del_size_ - 1);
        mod_[1] = fade2 * (del_size_ - 1);
        gain_[0] = sinf (fade1 * PI_F);
        gain_[1] = sinf (fade2 * PI_F);

        d_[0].Write (in);
        d_[1].Write (in);

        d_[0].SetDelay (mod_[0] + slewed_mod_[0]);
        d_[1].SetDelay (mod_[1] + slewed_mod_[1]);

        val = 0.0f;
        val += (d_[0].Read() * gain_[0]);
        val += (d_[1].Read() * gain_[1]);
        return val;
    }

    void SetTransposition (const float& transpose)
    {
        if (transpose_ != transpose || force_recalc_)
        {
            transpose_ = transpose;
            const uint8_t idx = (uint8_t) fabsf (transpose);
            float ratio = semitone_ratios_[idx % 12];
            ratio *= (uint8_t) (fabsf (transpose) / 12) + 1;
            shift_up_ = transpose > 0.0f;

            // The read head moves at 1 + (sweep rate) going up and 1 - (sweep
            // rate) going down, so a shift down to 1/ratio needs a sweep of
            // (1 - 1/ratio), not (ratio - 1). DaisySP used (ratio - 1) for both
            // and never corrected it: -12 semitones swept at the +12 rate, the
            // read head stood still (speed 0) and the "octave down" came out as
            // a frozen, sub-audible smear rather than a note an octave lower.
            const float sweep = shift_up_ ? ratio - 1.0f : 1.0f - 1.0f / ratio;
            mod_freq_ = (sweep * sr_) / del_size_;
            if (mod_freq_ < 0.0f)
                mod_freq_ = 0.0f;

            phs_[0].SetFreq (mod_freq_);
            phs_[1].SetFreq (mod_freq_);

            if (force_recalc_)
                force_recalc_ = false;
        }
    }

    void SetDelSize (uint32_t size)
    {
        del_size_ = size < kShiftBufferSize ? size : kShiftBufferSize;
        force_recalc_ = true;
        SetTransposition (transpose_);
    }

    /** An amount of internal random modulation; sounds a little like tape flutter. */
    void SetFun (float f) noexcept { fun_ = f; }

private:
    uint32_t nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    void SetSemitones()
    {
        for (size_t i = 0; i < 12; ++i)
            semitone_ratios_[i] = powf (2.0f, (float) i / 12);
    }

    using ShiftDelay = daisysp::DelayLine<float, kShiftBufferSize>;

    ShiftDelay d_[2];
    float pitch_shift_ = 0.0f, mod_freq_ = 0.0f;
    uint32_t del_size_ = kShiftBufferSize;

    bool force_recalc_ = false;
    float sr_ = 48000.0f;
    bool shift_up_ = true;
    daisysp::Phasor phs_[2];
    float gain_[2] = { 0.0f, 0.0f }, mod_[2] = { 0.0f, 0.0f }, transpose_ = 0.0f;
    float fun_ = 0.0f, mod_a_amt_ = 0.0f, mod_b_amt_ = 0.0f, prev_phs_a_ = 0.0f, prev_phs_b_ = 0.0f;
    float slewed_mod_[2] = { 0.0f, 0.0f }, mod_coeff_[2] = { 0.0f, 0.0f };
    float semitone_ratios_[12] = {};

    uint32_t seed;
    uint32_t rng;
};

} // namespace ee::dsp
