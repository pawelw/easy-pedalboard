#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>

namespace ee::plugin
{

/** What a face's tap scope animates from: how loud the input is, and when a
 * note started.
 *
 * Both are read off the *untouched* input, before any trim - whether something
 * is being played is a fact about the playing, not about what the pedal is
 * doing with it or how far the Input fader is turned down.
 *
 * Peak Delay drives its TapScope from this; Peak Alpine drives the same scope
 * inside its Delay module from a second instance. Written from the audio
 * thread, read from the editor's timer, which is what the two atomics are for.
 *
 * Everything here is specified in *seconds* rather than as a per-block factor.
 * That is the whole difficulty of the thing: on a per-block factor a host
 * running 64-sample buffers would follow eight times faster than one running
 * 512, and the same phrase would count a different number of notes on each.
 */
class InputMeter
{
public:
    // ------------------------------------------------------------- the follower
    // Fast attack, slow release, so the level reads as a note's shape rather
    // than as its waveform. The same shape Peak Wah's signal glow uses.
    static constexpr double kAttackSeconds = 0.005;
    static constexpr double kReleaseSeconds = 0.30;

    /** The window the level is mapped over: silence at -40 dB, full at 0. */
    static constexpr float kFloorDb = -40.0f;

    // ------------------------------------------------------------ note onsets
    // What counts as a note, for the scope's playheads. A block has to clear
    // kOnsetThreshold outright - -46 dBFS, below anything played on purpose and
    // above a quiet room - and stand kOnsetOverFloor above whatever is still
    // ringing, and no second note can start for kOnsetHoldSeconds afterwards.
    //
    // 1.5 rather than a larger ratio because a phrase played evenly barely
    // rises above its own decay: 1.8 misses more than half the notes in a run at
    // 150 ms. 70 ms rather than 120 because the refractory is meant to be a
    // backstop against one note's own attack, not a speed limit on playing. A
    // held chord still counts once.
    static constexpr float kOnsetThreshold = 0.005f;
    static constexpr float kOnsetOverFloor = 1.5f;
    static constexpr double kOnsetHoldSeconds = 0.07;

    // How fast the "still ringing" floor forgets the last note. Long enough
    // that a chord's own decay doesn't retrigger on its way down, short enough
    // that the next note in a phrase has something to stand above.
    static constexpr double kOnsetFloorSeconds = 0.15;

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        peakSmoothed = 0.0f;
        onsetFloor = 0.0f;
        onsetHoldSamples = 0;
        level.store (0.0f, std::memory_order_relaxed);
    }

    /** Per block rather than per sample: a face redraws at 45 Hz and a block is
        a quarter of that, so there is nothing a per-sample follower would show
        that this one does not. */
    void process (const float* left, const float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        const float* r = right != nullptr ? right : left;

        float blockPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            blockPeak = juce::jmax (blockPeak, std::abs (left[i]), std::abs (r[i]));

        const double blockSeconds = numSamples / sr;
        const float attackCoeff = static_cast<float> (std::exp (-blockSeconds / kAttackSeconds));
        const float releaseCoeff = static_cast<float> (std::exp (-blockSeconds / kReleaseSeconds));
        const float coeff = blockPeak > peakSmoothed ? attackCoeff : releaseCoeff;

        peakSmoothed = coeff * peakSmoothed + (1.0f - coeff) * blockPeak;

        const float db = juce::Decibels::gainToDecibels (peakSmoothed, -60.0f);
        level.store (juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb), std::memory_order_relaxed);

        // The onset test runs off the raw block peak, not the smoothed level:
        // the follower's own attack would smear the very edge this is trying to
        // find. A note counts when it is both audible and clearly louder than
        // whatever is still ringing, which is what stops one long chord
        // counting itself over and over.
        onsetHoldSamples = juce::jmax (0, onsetHoldSamples - numSamples);

        // ...or when sound arrives at all after silence. Without this second
        // way in, a volume swell never counts: it grows by a fraction of a per
        // cent per block, the floor tracks every step of the rise, and nothing
        // ever stands above it - so the scope would sit dark through a whole
        // phrase that is audibly being played.
        const bool fromSilence = onsetFloor < kOnsetThreshold;

        if (onsetHoldSamples == 0 && blockPeak > kOnsetThreshold
            && (fromSilence || blockPeak > onsetFloor * kOnsetOverFloor))
        {
            strikes.fetch_add (1, std::memory_order_relaxed);
            onsetHoldSamples = static_cast<int> (kOnsetHoldSeconds * sr);
        }

        // The floor jumps to whatever just arrived and falls away over
        // kOnsetFloorSeconds.
        const float floorCoeff = static_cast<float> (std::exp (-blockSeconds / kOnsetFloorSeconds));
        onsetFloor = blockPeak > onsetFloor ? blockPeak : onsetFloor * floorCoeff;
    }

    /** 0 = silent, 1 = 0 dBFS. Safe from any thread. */
    float getLevel() const noexcept { return level.load (std::memory_order_relaxed); }

    /** A monotonic count of note onsets. The face watches it for changes rather
        than reading a value out of it - each increment starts one playhead
        sweeping the scope's time axis. */
    int getStrikes() const noexcept { return strikes.load (std::memory_order_relaxed); }

private:
    double sr = 44100.0;

    float peakSmoothed = 0.0f;
    float onsetFloor = 0.0f;
    int onsetHoldSamples = 0;

    std::atomic<float> level { 0.0f };
    std::atomic<int> strikes { 0 };
};

} // namespace ee::plugin
