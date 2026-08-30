#pragma once

#include "OverdriveConfig.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace ee::dsp
{

/** A soft-clipping overdrive: one exponential gain stage into an asymmetric
    tanh clipper, with the filtering around it that makes it sound like a pedal
    instead of a broken op-amp.

    Signal path, per sample, per channel:

      1. one-pole high-pass (config::kPreClipHighpassHz) - keeps the bottom
         octave out of the clipper so chords stay defined;
      2. gain stage - config::kDriveMinGain..kDriveMaxGain, swept exponentially;
      3. tanh clip with a small DC bias for even harmonics, then the bias
         subtracted back off so silence stays silent;
      4. the removed sub-bass folded back in at config::kLowKeep;
      5. tilt tone control - split at config::kToneTiltPivotHz, the Tone knob
         crossfades the weight of the low and high bands;
      6. fixed low-pass (config::kPostLowpassHz) to tame the clip buzz;
      7. DC blocker.

    tanh bounds the output to well under +/-1 before the trims, so the stage is
    unconditionally stable - a NaN can only enter from outside, and one is caught
    and flushed at the top of process().

    No oversampling. The clipper is soft and, on any real instrument level, fed
    nowhere near a hard corner; the fixed post low-pass takes care of the rest.
    Kept deliberately in step with ee::dsp::TapeCharacter's drive stage.

    Pure DSP: no JUCE audio-processor types, so it unit-tests headless the same
    way FdnReverb does. All voicing lives in ee/dsp/OverdriveConfig.h; the Level
    knob is applied by the processor, not here.
*/
class Overdrive
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        preHpCoeff  = onePoleCoeff (config::kPreClipHighpassHz);
        tiltCoeff   = onePoleCoeff (config::kToneTiltPivotHz);
        postLpCoeff = onePoleCoeff (config::kPostLowpassHz);
        dcCoeff     = onePoleCoeff (config::kDcBlockerHz);

        updateDrive();
        updateTone();
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
            c = Channel {};
    }

    /** 0 = always-a-little-hair, 1 = slammed. Exponential between the two. */
    void setDrive01 (float drive01In) noexcept
    {
        const float n = juce::jlimit (0.0f, 1.0f, drive01In);
        if (n != drive01)
        {
            drive01 = n;
            updateDrive();
        }
    }

    /** 0 = dark, 1 = bright, ~0.5 = flat. A tilt around the pivot frequency. */
    void setTone01 (float tone01In) noexcept
    {
        const float n = juce::jlimit (0.0f, 1.0f, tone01In);
        if (n != tone01)
        {
            tone01 = n;
            updateTone();
        }
    }

    /** In-place, one channel per pointer. `right` may be null for mono. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        float* io[2] = { left, right };
        const int numCh = right != nullptr ? 2 : 1;

        for (int c = 0; c < numCh; ++c)
        {
            auto& s = channels[static_cast<size_t> (c)];

            // A single non-finite sample would otherwise stick in the filter
            // states and roar; clear the channel and carry on.
            if (! std::isfinite (s.preLp) || ! std::isfinite (s.tiltLp)
                || ! std::isfinite (s.postLp) || ! std::isfinite (s.dc))
                s = Channel {};

            for (int i = 0; i < numSamples; ++i)
            {
                float x = io[c][i];
                if (! std::isfinite (x))
                    x = 0.0f;

                // 1. pre-clip high-pass (one-pole HP = input minus its low-pass)
                s.preLp += preHpCoeff * (x - s.preLp);
                const float hp = x - s.preLp;

                // 2 + 3. gain into the asymmetric soft clip, bias subtracted back
                // off so the curve still passes through the origin
                const float clipped =
                    (std::tanh (driveGain * hp + config::kClipBias) - biasOffset) * driveMakeup;

                // 4. fold the removed sub-bass back in, unclipped
                float y = clipped + s.preLp * config::kLowKeep;

                // 5. tilt tone: weight the split bands by the Tone knob
                s.tiltLp += tiltCoeff * (y - s.tiltLp);
                const float low  = s.tiltLp;
                const float high = y - low;
                y = low * tiltLowGain + high * tiltHighGain;

                // 6. fixed post low-pass
                s.postLp += postLpCoeff * (y - s.postLp);
                y = s.postLp;

                // 7. DC blocker
                s.dc += dcCoeff * (y - s.dc);
                y -= s.dc;

                io[c][i] = y;
            }
        }
    }

private:
    struct Channel
    {
        float preLp  = 0.0f;
        float tiltLp = 0.0f;
        float postLp = 0.0f;
        float dc     = 0.0f;
    };

    float onePoleCoeff (float hz) const noexcept
    {
        const float w = juce::MathConstants<float>::twoPi * hz / static_cast<float> (sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    void updateDrive() noexcept
    {
        driveGain = config::kDriveMinGain
                    * std::pow (config::kDriveMaxGain / config::kDriveMinGain, drive01);

        // tanh's slope at the bias point sets how much clean gain a small signal
        // sees; back most of it out so winding Drive up trades headroom for
        // saturation rather than volume. The Level knob trims the remainder.
        driveMakeup = config::kOutputTrim / std::pow (driveGain, config::kMakeupExponent);
    }

    void updateTone() noexcept
    {
        tiltLowGain  = juce::jmap (tone01, config::kToneLowGainDark,  config::kToneLowGainBright);
        tiltHighGain = juce::jmap (tone01, config::kToneHighGainDark, config::kToneHighGainBright);
    }

    double sampleRate = 44100.0;

    float drive01 = config::kDefaultDrive01;
    float tone01  = config::kDefaultTone01;

    float driveGain   = 1.0f;
    float driveMakeup = 1.0f;
    const float biasOffset = std::tanh (config::kClipBias);

    float tiltLowGain  = 1.0f;
    float tiltHighGain = 1.0f;

    float preHpCoeff  = 0.0f;
    float tiltCoeff   = 0.0f;
    float postLpCoeff = 0.0f;
    float dcCoeff     = 0.0f;

    std::array<Channel, 2> channels;
};

} // namespace ee::dsp
