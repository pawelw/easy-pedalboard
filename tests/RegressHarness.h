#pragma once

// Shared parts of the "prove this change changed nothing" harnesses.
//
// A *_regress tool renders a fixed battery of settings through a whole
// processor and prints a checksum per pass. Run it before a refactor, keep the
// output, run it after, diff. Sample-exact is the bar - "sounds the same" is
// not, because a move that alters one sample has altered the code path.
//
// Distinct from the *_match tools (ee_delay_match, ee_spring_match,
// ee_tape_render), which render a real input file so a voicing can be A/B'd
// against a reference recording by ear. Those answer "is this the sound we
// want"; these answer "is this the same sound as before".
//
// Everything here is deterministic: no noise, no random seed, no input file, so
// a run is reproducible from a clean checkout with nothing to fetch.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstring>

namespace ee::regress
{

/** The test signal: two steady tones a long way apart, plus a click every half
    second. The tones show a gain envelope and any harmonics an engine adds; the
    clicks show whether anything steps at a block boundary, and give a delay
    something with a sharp edge to repeat.

    The two channels differ deliberately, so a stereo effect - a pan law, a
    ping-pong routing - cannot look correct by accident on a signal that is the
    same on both sides. */
inline void fillTestSignal (juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const int n = buffer.getNumSamples();
    const int clickPeriod = juce::jmax (1, static_cast<int> (sampleRate / 2));

    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const float low = 0.45f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t));
        const float high = 0.15f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * t));
        const bool click = (i % clickPeriod) < 24;

        buffer.setSample (0, i, low + high + (click ? 0.6f : 0.0f));
        if (buffer.getNumChannels() > 1)
            buffer.setSample (1, i, low * 0.7f - high + (click ? -0.6f : 0.0f));
    }
}

/** A transport that plays at a fixed tempo and can be relocated - the two
    branches of a phase- or tempo-locked engine's alignment. Without a playhead
    an offline render never reaches either, which is exactly the code a
    refactor is most likely to break. */
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    FakePlayHead (double bpm, double sampleRateIn) : tempo (bpm), sr (sampleRateIn) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (tempo);
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        return info;
    }

    void advance (int numSamples) { ppq += (tempo / 60.0) * (static_cast<double> (numSamples) / sr); }
    void jump (double toPpq) { ppq = toPpq; }
    void setPlaying (bool shouldPlay) { playing = shouldPlay; }

private:
    double tempo;
    double sr;
    double ppq = 0.0;
    bool playing = true;
};

/** A checksum over finished audio. FNV-1a over the raw sample bits, so a single
    changed bit anywhere in the render shows up - which is the whole point.
    Rounded values would let a "close enough" refactor pass. */
inline juce::String checksum (const juce::AudioBuffer<float>& buffer)
{
    uint64_t hash = 1469598103934665603ULL;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            uint32_t bits;
            std::memcpy (&bits, &data[i], sizeof (bits));

            // -0.0f and +0.0f are the same sample and must hash the same, or a
            // harmless sign difference on a silent passage reads as a failure.
            if (bits == 0x80000000u)
                bits = 0u;

            for (int byte = 0; byte < 4; ++byte)
            {
                hash ^= (bits >> (byte * 8)) & 0xffu;
                hash *= 1099511628211ULL;
            }
        }
    }

    return juce::String::toHexString (static_cast<juce::int64> (hash)).paddedLeft ('0', 16);
}

/** Ragged on purpose. A processor that only ever sees one block size can hide a
    per-block error, and anything that runs once a block - a transport
    alignment, a parameter read, a placement crossfade - runs a different number
    of times when the blocks are uneven. */
inline int nextBlockSize (int index)
{
    static const int sizes[] = { 512, 64, 480, 128, 331, 1024, 17 };
    return sizes[index % (sizeof (sizes) / sizeof (sizes[0]))];
}

/** Every rendered sample finite. A NaN latched into a feedback path is the one
    failure that is worse than a wrong number, so it is checked separately from
    the checksum rather than being left to show up as a mismatch. */
inline bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (ch, i)))
            {
                std::printf ("  NON-FINITE at channel %d sample %d\n", ch, i);
                return false;
            }

    return true;
}

inline void setPercent (juce::AudioProcessorValueTreeState& state, const char* id, float percent)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, percent * 0.01f));
}

/** The parameter's own 0..1 position, for a control whose range is not a
    percentage (a Time knob, a Rate knob). */
inline void setNormalised (juce::AudioProcessorValueTreeState& state, const char* id, float value01)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value01));
}

inline void setFlag (juce::AudioProcessorValueTreeState& state, const char* id, bool on)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

inline void setChoice (juce::AudioProcessorValueTreeState& state, const char* id, int index)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
}

} // namespace ee::regress
