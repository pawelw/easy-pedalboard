#pragma once

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Allpass.h"
#include "ModDelayLine.h"
#include "SpaceConfig.h"

namespace ee::dsp
{

/** BitBit Reverb's Modern engine: stereo in, stereo out.

    Two discrete early echoes per input, then a sixteen-line feedback network
    whose loss filters are sized from the Decay and Damping knobs, then a
    Low Cut / Hi Cut pair on the finished wet. Voiced to NI Raum's Airy mode -
    see SpaceConfig.h for what that measured as and how.

    No shimmer: that is the Shimmer engine's job (FdnReverb), which this one
    sits beside in ee::fx::ReverbModule.
*/
class SpaceReverb
{
public:
    static constexpr int kLines = SpaceVoicing::kLines;

    static constexpr float kMinDecay = 0.5f;
    static constexpr float kMaxDecay = 8.0f;
    static constexpr float kMinLowCutHz = 20.0f;
    static constexpr float kMaxLowCutHz = 800.0f;
    static constexpr float kMinHighCutHz = 1000.0f;
    static constexpr float kMaxHighCutHz = 20000.0f;
    static constexpr float kMinPredelayMs = 0.0f;
    static constexpr float kMaxPredelayMs = 60.0f;

    void prepare (double sampleRate);
    void reset();

    /** RT60 of the low mids, in seconds. */
    void setDecayTime (float seconds) noexcept;

    /** 0..1. How much faster than the Decay the top end dies. */
    void setDamping (float amount01) noexcept;

    /** Added ahead of the whole wet path, early echoes included. */
    void setPredelay (float ms) noexcept;

    /** 12 dB/oct highpass on the wet. kMinLowCutHz is off. */
    void setLowCut (float hz) noexcept;

    /** 12 dB/oct lowpass on the wet. kMaxHighCutHz is off. */
    void setHighCut (float hz) noexcept;

    const SpaceVoicing& getVoicing() const noexcept { return voicing; }
    /** For the offline fit tool. Re-prepares, so not for the audio thread. */
    void setVoicing (const SpaceVoicing& newVoicing);

    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    float getTailSeconds() const noexcept { return decaySeconds * 1.2f + 0.1f + predelayMs * 0.001f; }

private:
    /** Zavalishin's TPT state-variable filter, one channel, Butterworth Q. */
    struct Svf
    {
        float g = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
        void setCutoff (float hz, double sampleRate) noexcept;
        void reset() noexcept { ic1 = ic2 = 0.0f; }
        void process (float x, float& lp, float& hp) noexcept;
    };

    /** First-order high shelf, unity at DC - the loss filter of one trip. */
    struct Shelf
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        /** Loses lowDb at lowHz and highDb at highHz, as near as a first-order
            section can when the ratio of the two is out of its reach. */
        void design (float lowDb, float highDb, float lowHz, float highHz, double sampleRate) noexcept;
        /** dB this section loses at hz. */
        float lossDb (float hz, double sampleRate) const noexcept;
        void reset() noexcept { x1 = y1 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    /** Two shelves in series: the loss of one trip. Pinned at the table's two
        frequencies and at their geometric middle - see designTrip. */
    struct TripFilter
    {
        Shelf low, high;
        void reset() noexcept
        {
            low.reset();
            high.reset();
        }
        float process (float x) noexcept { return high.process (low.process (x)); }
    };

    void designTrip (TripFilter& filter, float seconds, float atDecaySeconds) const noexcept;
    void updateDerived() noexcept;
    float dampAt (const SpaceVoicing::DampTable& table, float atDecaySeconds) const noexcept;

    SpaceVoicing voicing;
    double sr = 44100.0;
    bool dirty = true;

    float decaySeconds = 2.0f;
    float damping = 0.25f;
    float predelayMs = 0.0f;
    float lowCutHz = kMinLowCutHz;
    float highCutHz = kMaxHighCutHz;

    // Input history, per channel: the Pre-delay, the early echoes and the
    // late network's feed all read it at their own offsets.
    ModDelayLine inputL, inputR;

    // After a reset the input is faded into that history over a few ms. The
    // first thing this engine says is a discrete echo - a delayed copy of the
    // input - so started cold mid-waveform (an engine switch, a transport
    // reset) it arrived as a step. The old FDN smeared its onset and never
    // needed this.
    int warmupSamples = 0;
    int warmupLength = 1;
    juce::SmoothedValue<float> predelaySamples;

    std::array<float, 3> earlySamples {};  // same side, L->R, R->L
    std::array<TripFilter, 2> earlyShelf;

    // The lines are read with first-order allpass interpolation, not the
    // delay line's own Hermite: Hermite loses top end on a fractional read,
    // and inside a loop that runs thirty times a second that loss alone
    // halved the 16 kHz decay. An allpass has none.
    std::array<ModDelayLine, kLines> lines;
    std::array<float, kLines> lineSamples {};
    std::array<float, kLines> lineGain {};
    std::array<TripFilter, kLines> lineShelf;
    std::array<float, kLines> allpassState {};
    std::array<Allpass, SpaceVoicing::kFeedDiffusers> feedDiffuserMid, feedDiffuserSide;
    float bandwidthCoeff = 1.0f;
    float bandwidthStateL = 0.0f, bandwidthStateR = 0.0f;
    std::array<float, kLines> lfoPhase {};
    std::array<float, kLines> lfoInc {};
    float modDepthSamples = 0.0f;

    juce::SmoothedValue<float> lowCutSmooth, highCutSmooth;
    std::array<Svf, 2> lowCutFilter, highCutFilter;
};

} // namespace ee::dsp
