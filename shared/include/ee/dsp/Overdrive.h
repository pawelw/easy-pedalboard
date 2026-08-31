#pragma once

#include "OverdriveConfig.h"

#include <chowdsp_wdf/chowdsp_wdf.h>

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace ee::dsp
{

/** A diode-clipper overdrive built on a Wave Digital Filter.

    The clipping stage is the circuit a Boss SD-1 / Tube Screamer clips with,
    solved sample-accurately with chowdsp_wdf: a driven voltage source feeds a
    series resistor into a node holding a capacitor and an anti-parallel silicon
    diode pair to ground. The diode I-V curve supplies the soft knee; the cap
    across the diodes rolls the buzz off the distortion the way the op-amp
    feedback cap does in the real pedal. A small pre-clip asymmetry adds the
    even-harmonic warmth an SD-1 gets from its uneven diode legs.

    Signal path, per sample, per channel:

      1. one-pole high-pass (config::kPreClipHighpassHz) - keeps the bottom
         octave out of the clipper so chords stay defined;
      2. gain stage - config::kDriveMinGain..kDriveMaxGain, swept exponentially
         (stands in for the op-amp's non-inverting gain);
      3. --- 2x oversampled from here through step 5 ---
      4. asymmetry shaper (y - kClipAsym * y * |y|), then the WDF diode clipper;
      5. make-up gain;
      6. --- back to host rate ---
      7. tilt tone control - split at config::kToneTiltPivotHz, the Tone knob
         crossfades the low and high band weights;
      8. fixed low-pass (config::kPostLowpassHz) and a DC blocker.

    The WDF is unconditionally stable and the asymmetry shaper is exactly zero
    at zero, so a silent input stays silent with no DC to chase. A non-finite
    sample can still only arrive from outside; it is caught and the channel
    flushed at the top of process().

    Pure DSP: no JUCE audio-processor types, so it unit-tests headless. All
    voicing lives in ee/dsp/OverdriveConfig.h; the Level knob is applied by the
    processor, not here.
*/
class Overdrive
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
        const double osRate = sampleRate * config::kOversampleFactor;

        preHpCoeff  = onePoleCoeff (config::kPreClipHighpassHz, sampleRate);
        tiltCoeff   = onePoleCoeff (config::kToneTiltPivotHz, sampleRate);
        postLpCoeff = onePoleCoeff (config::kPostLowpassHz, sampleRate);
        dcCoeff     = onePoleCoeff (config::kDcBlockerHz, sampleRate);

        for (auto& c : channels)
        {
            c.clipper.prepare (static_cast<float> (osRate));
            c.aaUp.setup (config::kOversampleCutoffHz, static_cast<float> (osRate));
            c.aaDown.setup (config::kOversampleCutoffHz, static_cast<float> (osRate));
        }

        updateDrive();
        updateTone();
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.preLp = c.tiltLp = c.postLp = c.dc = 0.0f;
            c.clipper.reset();
            c.aaUp.reset();
            c.aaDown.reset();
        }
    }

    /** 0 = barely tickling the diodes, 1 = slammed. Exponential between. */
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
            processChannel (channels[static_cast<size_t> (c)], io[c], numSamples);
    }

private:
    //==========================================================================
    /** 4th-order Butterworth low-pass (two RBJ biquad sections) for the
        oversampler's anti-imaging and anti-aliasing legs. */
    struct Aa4
    {
        struct Section { float b0, b1, b2, a1, a2, z1, z2; };
        std::array<Section, 2> s {};

        void setup (float cutoffHz, float fs) noexcept
        {
            // Butterworth 4th-order section quality factors.
            const float qs[2] = { 0.54119610f, 1.30656296f };
            const float w0 = juce::MathConstants<float>::twoPi
                             * juce::jlimit (10.0f, 0.49f * fs, cutoffHz) / fs;
            const float cw = std::cos (w0);
            const float sw = std::sin (w0);

            for (int i = 0; i < 2; ++i)
            {
                const float alpha = sw / (2.0f * qs[i]);
                const float a0 = 1.0f + alpha;
                s[static_cast<size_t> (i)].b0 = (1.0f - cw) * 0.5f / a0;
                s[static_cast<size_t> (i)].b1 = (1.0f - cw) / a0;
                s[static_cast<size_t> (i)].b2 = (1.0f - cw) * 0.5f / a0;
                s[static_cast<size_t> (i)].a1 = (-2.0f * cw) / a0;
                s[static_cast<size_t> (i)].a2 = (1.0f - alpha) / a0;
            }
            reset();
        }

        void reset() noexcept
        {
            for (auto& sec : s) { sec.z1 = 0.0f; sec.z2 = 0.0f; }
        }

        inline float process (float x) noexcept
        {
            for (auto& sec : s)
            {
                const float y = sec.b0 * x + sec.z1;
                sec.z1 = sec.b1 * x - sec.a1 * y + sec.z2;
                sec.z2 = sec.b2 * x - sec.a2 * y;
                x = y;
            }
            return x;
        }
    };

    //==========================================================================
    /** The SD-1 / Tube-Screamer clipping node as a Wave Digital Filter:
        source -> series R -> ( cap || anti-parallel diode pair ). */
    struct DiodeClipper
    {
        chowdsp::wdft::ResistiveVoltageSourceT<float> vs { config::kClipSeriesR };
        chowdsp::wdft::CapacitorT<float> cap { config::kClipCapF };
        chowdsp::wdft::WDFParallelT<float, decltype (vs), decltype (cap)> par { vs, cap };
        chowdsp::wdft::DiodePairT<float, decltype (par)> diodes {
            par, config::kDiodeIs, config::kDiodeVt, config::kDiodeCount };

        void prepare (float fs) noexcept { cap.prepare (fs); }
        void reset() noexcept { cap.reset(); }

        inline float processSample (float x) noexcept
        {
            vs.setVoltage (x);
            diodes.incident (par.reflected());
            par.incident (diodes.reflected());
            return chowdsp::wdft::voltage<float> (cap);
        }
    };

    struct Channel
    {
        float preLp = 0.0f, tiltLp = 0.0f, postLp = 0.0f, dc = 0.0f;
        Aa4 aaUp, aaDown;
        DiodeClipper clipper;
    };

    static float onePoleCoeff (float hz, double fs) noexcept
    {
        const float w = juce::MathConstants<float>::twoPi * hz / static_cast<float> (fs);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

    void updateDrive() noexcept
    {
        driveGain = config::kDriveMinGain
                    * std::pow (config::kDriveMaxGain / config::kDriveMinGain, drive01);

        // The clipped signal sits near the diode clamp voltage, which barely
        // moves with Drive; lift it back to a usable level, tilting up a touch
        // as Drive rises so the knob keeps a roughly steady loudness. Level
        // trims the rest.
        makeup = config::kMakeupLow
                 * std::pow (driveGain / config::kDriveMinGain, config::kMakeupSlope);
    }

    void updateTone() noexcept
    {
        tiltLowGain  = juce::jmap (tone01, config::kToneLowGainDark,  config::kToneLowGainBright);
        tiltHighGain = juce::jmap (tone01, config::kToneHighGainDark, config::kToneHighGainBright);
    }

    inline float clipStage (Channel& ch, float v) noexcept
    {
        // Asymmetry: squashes one half a hair harder than the other. Zero at
        // zero, no DC added for a symmetric input.
        v -= config::kClipAsym * v * std::abs (v);
        return ch.clipper.processSample (v) * makeup;
    }

    void processChannel (Channel& ch, float* buf, int numSamples) noexcept
    {
        if (! std::isfinite (ch.preLp) || ! std::isfinite (ch.tiltLp)
            || ! std::isfinite (ch.postLp) || ! std::isfinite (ch.dc))
        {
            ch.preLp = ch.tiltLp = ch.postLp = ch.dc = 0.0f;
            ch.clipper.reset();
            ch.aaUp.reset();
            ch.aaDown.reset();
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float x = buf[i];
            if (! std::isfinite (x))
                x = 0.0f;

            // 1. pre-clip high-pass (one-pole HP = input minus its low-pass)
            ch.preLp += preHpCoeff * (x - ch.preLp);
            const float hp = x - ch.preLp;

            // 2. gain stage
            const float driven = driveGain * hp;

            // 3-5. oversampled asymmetry + diode clipper + make-up
            float y;
            if (config::kOversampleFactor == 2)
            {
                const float u0 = ch.aaUp.process (2.0f * driven);
                const float u1 = ch.aaUp.process (0.0f);
                const float y0 = clipStage (ch, u0);
                const float y1 = clipStage (ch, u1);
                y = ch.aaDown.process (y0);
                ch.aaDown.process (y1);
            }
            else
            {
                y = clipStage (ch, driven);
            }

            // 7. tilt tone: weight the split bands by the Tone knob
            ch.tiltLp += tiltCoeff * (y - ch.tiltLp);
            const float low  = ch.tiltLp;
            const float high = y - low;
            y = low * tiltLowGain + high * tiltHighGain;

            // 8. fixed post low-pass, then DC blocker
            ch.postLp += postLpCoeff * (y - ch.postLp);
            y = ch.postLp;
            ch.dc += dcCoeff * (y - ch.dc);
            y -= ch.dc;

            if (! std::isfinite (y))
            {
                y = 0.0f;
                ch.clipper.reset();
                ch.aaUp.reset();
                ch.aaDown.reset();
            }

            buf[i] = y;
        }
    }

    double sampleRate = 44100.0;

    float drive01 = config::kDefaultDrive01;
    float tone01  = config::kDefaultTone01;

    float driveGain = 1.0f;
    float makeup    = 1.0f;

    float tiltLowGain  = 1.0f;
    float tiltHighGain = 1.0f;

    float preHpCoeff  = 0.0f;
    float tiltCoeff   = 0.0f;
    float postLpCoeff = 0.0f;
    float dcCoeff     = 0.0f;

    std::array<Channel, 2> channels;
};

} // namespace ee::dsp
