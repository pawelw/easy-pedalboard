#pragma once

#include "ee/dsp/Aa4.h"
#include "ee/dsp/TubeDriveConfig.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ee::dsp
{

/** Amp's single-knob drive stage, matched to a real reference unit - see
    TubeDriveConfig.h for what the recording showed and how it was fitted.
    Still deliberately not ee::dsp::Overdrive's diode clipper.

    Chain per channel:

      1. pre-emphasis shelf, lifting the top ahead of the curve;
      2. gain and bias, then a logarithmic curve (asymmetric asinh), run 2x
         oversampled;
      3. level make-up, so the knob does not change the volume (see
         TubeDriveConfig.h's LEVEL section);
      4. de-emphasis shelf, taking the lift back down after the curve;
      5. DC blocker.

    Both shelves are first-order bilinear sections without prewarping - the
    form the voicing was fitted in, so the corner frequencies in the config
    mean exactly what they meant to the fit.

    The knob glides: setDrive01 only sets a target, and while it is moving the
    chain is re-voiced from the glided position every sample - any coarser and
    a fast turn steps the shelves and the gain audibly. Leaving 0 and arriving
    back at it crossfade against the dry input over tubedrive::kFadeMs, and
    once both have settled at 0 process() is a bit-exact pass-through, the
    same convention ee::dsp::BitCrusher documents for its own rest position.
    prepare() and reset() snap both onto the target.

    Pure DSP: no JUCE audio-processor types.
*/
class TubeDrive
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
        const double osRate = sampleRate * tubedrive::kOversampleFactor;

        for (auto& c : channels)
        {
            c.aaUp.setup (tubedrive::kOversampleCutoffHz, static_cast<float> (osRate));
            c.aaDown.setup (tubedrive::kOversampleCutoffHz, static_cast<float> (osRate));
        }

        smoothCoeff = static_cast<float> (1.0 - std::exp (-1000.0 / (tubedrive::kSmoothMs * sampleRate)));
        fadeStep = static_cast<float> (1000.0 / (tubedrive::kFadeMs * sampleRate));

        dcBlocker.setHighPass (tubedrive::kDcBlockerHz, sampleRate);
        reset();
    }

    /** Clears every filter and snaps the glide onto the target. */
    void reset() noexcept
    {
        for (auto& c : channels)
            resetChannel (c);

        leading = current = target;
        wet = target > 0.0f ? 1.0f : 0.0f;
        stale = false;
        updateDrive();
    }

    /** 0 = an exact pass-through. 1 = the fitted reference. Glides. */
    void setDrive01 (float drive01) noexcept { target = std::clamp (drive01, 0.0f, 1.0f); }

    /** In-place, one channel per pointer. `right` may be null for mono. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        if (target <= 0.0f && leading <= 0.0f && current <= 0.0f && wet <= 0.0f)
        {
            // Bit-exact pass-through. The filters hold whatever they last saw,
            // so they are cleared before the stage next wakes rather than
            // replaying that into the fade-in.
            stale = true;
            return;
        }

        if (stale)
        {
            for (auto& c : channels)
                resetChannel (c);
            stale = false;
        }

        float* io[2] = { left, right };
        const int numCh = right != nullptr ? 2 : 1;

        for (int start = 0; start < numSamples;)
        {
            // One sample at a time while the knob or the fade is moving, the
            // rest of the block in one go once both have settled.
            int n = numSamples - start;
            if (leading != target || current != target || wet != wetTarget())
            {
                glide();
                n = 1;
            }

            for (int c = 0; c < numCh; ++c)
                processChannel (channels[static_cast<size_t> (c)], io[c] + start, n);

            start += n;
        }
    }

private:
    /** y = b0 x + z;  z = b1 x - a1 y. */
    struct FirstOrder
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;

        /** (1 + s/wz) / (1 + s/wp), bilinear. Unity at DC, pole/zero at the top. */
        void setShelf (double zeroHz, double poleHz, double fs) noexcept
        {
            const double k = 2.0 * fs;
            const double kz = k / (2.0 * 3.14159265358979 * zeroHz);
            const double kp = k / (2.0 * 3.14159265358979 * poleHz);
            const double d0 = 1.0 + kp;
            b0 = static_cast<float> ((1.0 + kz) / d0);
            b1 = static_cast<float> ((1.0 - kz) / d0);
            a1 = static_cast<float> ((1.0 - kp) / d0);
        }

        /** (s/w) / (1 + s/w), bilinear. */
        void setHighPass (double hz, double fs) noexcept
        {
            const double kw = 2.0 * fs / (2.0 * 3.14159265358979 * hz);
            const double d0 = 1.0 + kw;
            b0 = static_cast<float> (kw / d0);
            b1 = static_cast<float> (-kw / d0);
            a1 = static_cast<float> ((1.0 - kw) / d0);
        }

        inline float process (float x, float& z) const noexcept
        {
            const float y = b0 * x + z;
            z = b1 * x - a1 * y;
            return y;
        }
    };

    struct Channel
    {
        float pre = 0.0f;
        float post = 0.0f;
        float dc = 0.0f;
        Aa4 aaUp, aaDown;
    };

    /** The asymmetric logarithmic curve, before gain, bias or offset. */
    static inline float curve (float v) noexcept
    {
        return v >= 0.0f ? std::asinh (v) : -tubedrive::kNegScale * std::asinh (-v / tubedrive::kNegScale);
    }

    /** The fade heads for dry only once the glide is nearly down, so turning
        the knob to 0 still sweeps the voicing out rather than crossfading
        straight from wherever it was. */
    float wetTarget() const noexcept
    {
        return target > 0.0f || current > tubedrive::kFadeDrive01 ? 1.0f : 0.0f;
    }

    /** One sample of the knob glide and the dry fade. */
    void glide() noexcept
    {
        const float toWet = wetTarget();
        wet = toWet > wet ? std::min (toWet, wet + fadeStep) : std::max (toWet, wet - fadeStep);

        if (leading != target || current != target)
        {
            leading += smoothCoeff * (target - leading);
            current += smoothCoeff * (leading - current);
            if (std::abs (target - leading) < 1.0e-5f && std::abs (target - current) < 1.0e-5f)
                leading = current = target;

            updateDrive();
        }
    }

    void updateDrive() noexcept
    {
        const float t = tubedrive::voicingFor (current);

        gain = tubedrive::gainFor (current);
        bias = tubedrive::kBias * t;
        restOffset = curve (bias);

        const double prePole = tubedrive::kPreShelfZeroHz
                               * std::pow (tubedrive::kPreShelfPoleHz / tubedrive::kPreShelfZeroHz, t);
        const double postZero = tubedrive::kPostShelfPoleHz
                                * std::pow (tubedrive::kPostShelfZeroHz / tubedrive::kPostShelfPoleHz, t);
        preShelf.setShelf (tubedrive::kPreShelfZeroHz, prePole, sampleRate);
        postShelf.setShelf (postZero, tubedrive::kPostShelfPoleHz, sampleRate);

        // Level - see TubeDriveConfig.h. What the stage does to a small signal
        // is the gain into the curve times its slope at the bias point (the
        // bias is never positive, so that is the negative side's slope);
        // divide it out, then trim for what real playing does beyond that.
        const float b = bias / tubedrive::kNegScale;
        const float smallSignalGain = gain / std::sqrt (1.0f + b * b);
        makeup = std::pow (10.0f, tubedrive::levelTrimDbFor (current) / 20.0f) / smallSignalGain;
    }

    inline float shape (float x) const noexcept { return curve (gain * x + bias) - restOffset; }

    void processChannel (Channel& ch, float* buf, int numSamples) noexcept
    {
        if (! std::isfinite (ch.pre) || ! std::isfinite (ch.post) || ! std::isfinite (ch.dc))
            resetChannel (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = buf[i];
            if (! std::isfinite (x))
                x = 0.0f;

            const float emphasised = preShelf.process (x, ch.pre);

            float y;
            if (tubedrive::kOversampleFactor == 2)
            {
                const float u0 = ch.aaUp.process (2.0f * emphasised);
                const float u1 = ch.aaUp.process (0.0f);
                y = ch.aaDown.process (shape (u0));
                ch.aaDown.process (shape (u1));
            }
            else
            {
                y = shape (emphasised);
            }

            y = postShelf.process (y * makeup, ch.post);
            y = dcBlocker.process (y, ch.dc);

            if (! std::isfinite (y))
            {
                y = 0.0f;
                resetChannel (ch);
            }

            buf[i] = wet < 1.0f ? x + wet * (y - x) : y;
        }
    }

    static void resetChannel (Channel& ch) noexcept
    {
        ch.pre = ch.post = ch.dc = 0.0f;
        ch.aaUp.reset();
        ch.aaDown.reset();
    }

    double sampleRate = 44100.0;

    float target = tubedrive::kDefaultDrivePct * 0.01f;
    float leading = target; // first of the glide's two poles; `current` is the second
    float current = target;
    float wet = target > 0.0f ? 1.0f : 0.0f;
    float smoothCoeff = 1.0f;
    float fadeStep = 1.0f;
    bool stale = false;

    float gain = tubedrive::kDriveMinGain;
    float bias = 0.0f;
    float restOffset = 0.0f;
    float makeup = 1.0f;
    FirstOrder preShelf, postShelf, dcBlocker;

    std::array<Channel, 2> channels;
};

} // namespace ee::dsp
