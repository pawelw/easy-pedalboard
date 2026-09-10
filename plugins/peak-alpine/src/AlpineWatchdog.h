#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

/**
 * Peak Alpine's output-safety black box - dev builds only (EE_ALPINE_WATCHDOG,
 * see plugins/peak-alpine/CMakeLists.txt). The safety net itself
 * (PeakAlpineProcessor::sanitizeOutput) ships in every build; this is only the
 * flight recorder behind it.
 *
 * Every processBlock writes one small frame into a ring buffer: the peak level
 * at the output of each stage in the chain, the final output peak, and a flag
 * byte saying whether the safety net had to touch this block. When the net
 * first trips, the whole ring plus a snapshot of every parameter is frozen into
 * `pending` and `sequence` is bumped. The editor's 45 Hz timer notices, appends
 * the frozen record to a log file and offers it on a Copy button.
 *
 * One writer (the audio thread), one reader (the message-thread timer). The
 * frozen record is published with a seqlock: an odd `sequence` means a write is
 * in progress, so the reader just waits for the next tick rather than copying a
 * half-written record. Incidents are seconds apart at the very least, so a
 * missed read is harmless.
 */
struct AlpineWatchdog
{
    enum Stage
    {
        stageInput = 0,
        stageArtifact,
        stageModulation,
        stageDelay,
        stageReverb,
        numStages
    };

    static constexpr int kRingFrames = 256;
    static constexpr int kMaxParams = 96;

    struct Frame
    {
        juce::uint32 block = 0;
        std::array<float, numStages> stagePeak {};
        float outPeak = 0.0f;
        juce::uint8 flags = 0; // bit0 non-finite, bit1 clamped, bit2 module reset
    };

    struct Incident
    {
        juce::int64 timeMs = 0;
        double sampleRate = 0.0;
        int blockSize = 0;
        double bpm = 0.0;
        bool playing = false;
        juce::uint32 atBlock = 0;

        int frameCount = 0; // valid frames in `ring`
        int ringHead = 0;   // one past the newest frame
        std::array<Frame, kRingFrames> ring {};

        int numParams = 0;
        std::array<float, kMaxParams> paramValue {}; // normalised 0..1, in getParameters() order
    };

    // ---------------------------------------------------------------- audio thread

    /** Build the parameter list once (it never changes after construction, so
        this is race-free against the reader), and arm the ring. */
    void prepare (juce::AudioProcessor& processor, double sr, int blockSize)
    {
        sampleRate = sr;
        currentBlockSize = blockSize;

        if (! built)
        {
            for (auto* ap : processor.getParameters())
            {
                if (static_cast<int> (paramPtrs.size()) >= kMaxParams)
                    break;

                paramPtrs.push_back (ap);
            }
            built = true;
        }

        head = 0;
        filled = 0;
        blockCounter = 0;
    }

    void beginBlock() noexcept
    {
        cur = Frame {};
        cur.block = blockCounter++;
    }

    void setStagePeak (Stage stage, const juce::AudioBuffer<float>& buffer, int numCh, int numSamples) noexcept
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));

        cur.stagePeak[static_cast<size_t> (stage)] = peak;
    }

    /** Commit this block's frame. The four flags come straight from the safety
        net's verdict. When `firstBad` is set this also freezes a full incident. */
    void endBlock (float outPeak,
                   bool nonFinite,
                   bool clamped,
                   bool didReset,
                   bool firstBad,
                   juce::AudioPlayHead* playHead) noexcept
    {
        cur.outPeak = outPeak;
        cur.flags = static_cast<juce::uint8> ((nonFinite ? 1 : 0) | (clamped ? 2 : 0) | (didReset ? 4 : 0));

        ring[static_cast<size_t> (head)] = cur;
        head = (head + 1) % kRingFrames;
        if (filled < kRingFrames)
            ++filled;

        if (! firstBad)
            return;

        sequence.fetch_add (1, std::memory_order_release); // -> odd: writing

        pending.timeMs = juce::Time::currentTimeMillis();
        pending.sampleRate = sampleRate;
        pending.blockSize = currentBlockSize;
        pending.atBlock = cur.block;
        pending.bpm = 0.0;
        pending.playing = false;

        if (playHead != nullptr)
            if (const auto pos = playHead->getPosition())
            {
                if (const auto b = pos->getBpm())
                    pending.bpm = *b;
                pending.playing = pos->getIsPlaying();
            }

        pending.frameCount = filled;
        pending.ringHead = head;
        pending.ring = ring;

        pending.numParams = static_cast<int> (paramPtrs.size());
        for (size_t i = 0; i < paramPtrs.size(); ++i)
            pending.paramValue[i] = paramPtrs[i]->getValue();

        sequence.fetch_add (1, std::memory_order_release); // -> even: done
    }

    // -------------------------------------------------------------- message thread

    /** Copy the frozen incident if there is one newer than `lastSeen` and it can
        be read without a writer mid-flight. Advances `lastSeen` on success. */
    bool readIncident (juce::uint32& lastSeen, Incident& out) const
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const juce::uint32 s1 = sequence.load (std::memory_order_acquire);

            if ((s1 & 1u) != 0u)
                continue; // a write is in progress

            if (s1 == lastSeen)
                return false; // nothing new

            out = pending;

            if (sequence.load (std::memory_order_acquire) == s1)
            {
                lastSeen = s1;
                return true;
            }
        }

        return false;
    }

    static const char* stageName (int stage) noexcept
    {
        switch (stage)
        {
        case stageInput:
            return "in";
        case stageArtifact:
            return "artifact";
        case stageModulation:
            return "mod";
        case stageDelay:
            return "delay";
        case stageReverb:
            return "reverb";
        default:
            return "?";
        }
    }

private:
    double sampleRate = 48000.0;
    int currentBlockSize = 0;
    juce::uint32 blockCounter = 0;

    Frame cur {};
    std::array<Frame, kRingFrames> ring {};
    int head = 0;
    int filled = 0;

    bool built = false;
    std::vector<juce::AudioProcessorParameter*> paramPtrs;

    Incident pending {};
    std::atomic<juce::uint32> sequence { 0 };
};
