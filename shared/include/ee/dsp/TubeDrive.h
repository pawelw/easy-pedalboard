#pragma once

#include "ee/dsp/Aa4.h"
#include "ee/dsp/TubeDriveConfig.h"

#include <array>
#include <cmath>

namespace ee::dsp
{

/** A single-knob analog drive stage, voiced as a tube rather than a diode
    clipper - see ee::dsp::Overdrive for that shape, which this is deliberately
    not a tuning of.

    Chain per channel, the knee run 2x oversampled:

      1. gain stage (tubedrive::gainFor);
      2. asymmetric rational soft-clip - one polarity compresses harder than
         the other, the even-harmonic source of "tube" warmth (see
         TubeDriveConfig.h);
      3. make-up gain - not a lift, usually a cut: the shaper's own asymptote
         already puts out more than kMaxMakeupDb by itself at high Drive, so
         this is solved backwards from that target rather than swept forward
         off the gain (see TubeDriveConfig.h's OUTPUT MAKE-UP section);
      4. warmth low-pass, corner darkening as Drive rises;
      5. DC blocker.

    At Drive = 0 the gain is unity, both saturation coefficients are zero (the
    shaper is exactly `x / 1 = x`), the low-pass sits at/above its bypass point
    and the make-up is 1 - so process() is a bit-exact pass-through, the same
    convention ee::dsp::BitCrusher documents for its own rest position.

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

        dcCoeff = onePoleCoeff (tubedrive::kDcBlockerHz, sampleRate);
        updateDrive();
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.lp = 0.0f;
            c.dc = 0.0f;
            c.aaUp.reset();
            c.aaDown.reset();
        }
    }

    /** 0 = unity gain, no asymmetry, warmth low-pass bypassed - an exact
        pass-through. 1 = full tube character. */
    void setDrive01 (float drive01In) noexcept
    {
        const float n = std::clamp (drive01In, 0.0f, 1.0f);
        if (n != drive01)
        {
            drive01 = n;
            updateDrive();
        }
    }

    /** In-place, one channel per pointer. `right` may be null for mono. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        const int numCh = right != nullptr ? 2 : 1;

        if (bypass)
            return; // bit-exact pass-through

        for (int c = 0; c < numCh; ++c)
            processChannel (channels[static_cast<size_t> (c)], io[c], numSamples);
    }

private:
    struct Channel
    {
        float lp = 0.0f;
        float dc = 0.0f;
        Aa4 aaUp, aaDown;
    };

    static float onePoleCoeff (float hz, double fs) noexcept
    {
        const float w = 2.0f * 3.14159265359f * hz / static_cast<float> (fs);
        return std::clamp (1.0f - std::exp (-w), 0.0f, 1.0f);
    }

    void updateDrive() noexcept
    {
        gain = tubedrive::gainFor (drive01);

        const float satT = std::pow (drive01, tubedrive::kSatSkew);
        satPos = tubedrive::kPosSatMax * satT;
        satNeg = tubedrive::kNegSatMax * satT;

        const float warmT = std::pow (drive01, tubedrive::kWarmthSkew);
        warmthHz = tubedrive::kWarmthMaxHz * std::pow (tubedrive::kWarmthMinHz / tubedrive::kWarmthMaxHz, warmT);
        lpCoeff = onePoleCoeff (warmthHz, sampleRate);

        // Solved backwards from the target - see TubeDriveConfig.h. `refShaped`
        // is the positive-side shape() evaluated analytically at the reference
        // amplitude; dividing it back out gives the shaper's own gain there
        // with no makeup applied, and makeup is whatever closes the gap to
        // kMaxMakeupDb * drive01.
        const float refDriven = gain * tubedrive::kMakeupRefAmplitude;
        const float refShaped = refDriven / (1.0f + satPos * refDriven);
        const float naturalGain = refShaped / tubedrive::kMakeupRefAmplitude;
        const float targetGain = std::pow (10.0f, (tubedrive::kMaxMakeupDb * drive01) / 20.0f);
        makeup = naturalGain > 1.0e-6f ? targetGain / naturalGain : targetGain;

        bypass = drive01 <= 0.0f;
    }

    inline float shape (float x) const noexcept
    {
        const float driven = gain * x;
        return driven >= 0.0f ? driven / (1.0f + satPos * driven) : driven / (1.0f - satNeg * driven);
    }

    void processChannel (Channel& ch, float* buf, int numSamples) noexcept
    {
        if (! std::isfinite (ch.lp) || ! std::isfinite (ch.dc))
        {
            ch.lp = ch.dc = 0.0f;
            ch.aaUp.reset();
            ch.aaDown.reset();
        }

        const bool bypassLp = warmthHz >= tubedrive::kWarmthBypassHz;

        for (int i = 0; i < numSamples; ++i)
        {
            float x = buf[i];
            if (! std::isfinite (x))
                x = 0.0f;

            float y;
            if (tubedrive::kOversampleFactor == 2)
            {
                const float u0 = ch.aaUp.process (2.0f * x);
                const float u1 = ch.aaUp.process (0.0f);
                const float y0 = shape (u0) * makeup;
                const float y1 = shape (u1) * makeup;
                y = ch.aaDown.process (y0);
                ch.aaDown.process (y1);
            }
            else
            {
                y = shape (x) * makeup;
            }

            if (! bypassLp)
            {
                ch.lp += lpCoeff * (y - ch.lp);
                y = ch.lp;
            }

            ch.dc += dcCoeff * (y - ch.dc);
            y -= ch.dc;

            if (! std::isfinite (y))
            {
                y = 0.0f;
                ch.lp = ch.dc = 0.0f;
                ch.aaUp.reset();
                ch.aaDown.reset();
            }

            buf[i] = y;
        }
    }

    double sampleRate = 44100.0;

    float drive01 = tubedrive::kDefaultDrivePct * 0.01f;

    float gain = tubedrive::kDriveMinGain;
    float satPos = 0.0f;
    float satNeg = 0.0f;
    float warmthHz = tubedrive::kWarmthMaxHz;
    float lpCoeff = 0.0f;
    float makeup = 1.0f;
    float dcCoeff = 0.0f;
    bool bypass = true;

    std::array<Channel, 2> channels;
};

} // namespace ee::dsp
