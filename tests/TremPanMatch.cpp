// Renders a fixed battery of settings through the whole Peak Trem & Pan
// processor and prints a checksum per pass, so the pedal's output can be
// compared byte for byte across a change that is supposed to change nothing.
//
// Written for the extraction of ee::dsp::Tremolo out of this processor: that is
// a move, not a rewrite, so every number below must come back identical. Run it
// before the change, keep the output, run it after, diff.
//
// It generates its own input rather than taking a file, for the same reason it
// prints checksums rather than only writing a wav: the comparison has to be
// something anyone can re-run from a clean checkout with nothing to fetch.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <cstring>

#include "PluginProcessor.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 3;
constexpr int kLength = static_cast<int> (kSampleRate) * kSeconds;

/** The test signal: two steady tones a long way apart, plus a click every half
    second. The tones show the tremolo's gain envelope and the bias stage's
    harmonics; the clicks show whether anything steps at a block boundary. Fully
    deterministic - no noise, no random seed, so a rendered pass is reproducible
    across machines. */
void fillTestSignal (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();

    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / kSampleRate;
        const float low = 0.45f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t));
        const float high = 0.15f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * t));
        const bool click = (i % static_cast<int> (kSampleRate / 2)) < 24;

        // The two channels differ so a panning pass cannot look correct by
        // accident on a signal that is the same on both sides.
        buffer.setSample (0, i, low + high + (click ? 0.6f : 0.0f));
        buffer.setSample (1, i, low * 0.7f - high + (click ? -0.6f : 0.0f));
    }
}

/** A transport that plays at a fixed tempo and jumps once, a third of the way
    in - the two branches of the processor's sync alignment (the gentle
    per-block pull, and the hard snap after a relocate). Without a playhead an
    offline render never reaches either, which is exactly the code an extraction
    is most likely to break. */
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    explicit FakePlayHead (double bpm) : tempo (bpm) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (tempo);
        info.setIsPlaying (true);
        info.setPpqPosition (ppq);
        return info;
    }

    void advance (int numSamples)
    {
        ppq += (tempo / 60.0) * (static_cast<double> (numSamples) / kSampleRate);
    }

    void jump (double toPpq) { ppq = toPpq; }

private:
    double tempo;
    double ppq = 0.0;
};

void setPercent (juce::AudioProcessorValueTreeState& state, const char* id, float percent)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, percent * 0.01f));
}

void setNormalised (juce::AudioProcessorValueTreeState& state, const char* id, float value01)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value01));
}

void setFlag (juce::AudioProcessorValueTreeState& state, const char* id, bool on)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

struct Pass
{
    const char* name;
    float amount;  // %
    float rate01;  // the Rate knob's own position
    float shape;   // %
    float bias;    // %
    bool panning;
    bool synced;
    bool engaged;
};

// Every branch the processor has: each of the five LFO shape anchors, the bias
// stage in and out, the panning law, the bypass crossfade, and a depth of zero.
const Pass kPasses[] = {
    { "trem shape 0 (exp decay)", 70.0f, 0.50f, 0.0f, 0.0f, false, false, true },
    { "trem shape 25 (ramp)", 70.0f, 0.50f, 25.0f, 0.0f, false, false, true },
    { "trem shape 50 (triangle)", 70.0f, 0.50f, 50.0f, 0.0f, false, false, true },
    { "trem shape 75", 70.0f, 0.50f, 75.0f, 0.0f, false, false, true },
    { "trem shape 100 (square)", 70.0f, 0.50f, 100.0f, 0.0f, false, false, true },
    { "trem bias 50", 40.0f, 0.35f, 50.0f, 50.0f, false, false, true },
    { "trem bias 100, full depth", 100.0f, 0.62f, 50.0f, 100.0f, false, false, true },
    { "panning", 80.0f, 0.45f, 50.0f, 0.0f, true, false, true },
    { "panning, bias ignored", 80.0f, 0.45f, 50.0f, 80.0f, true, false, true },
    { "synced, transport jump", 75.0f, 0.55f, 40.0f, 20.0f, false, true, true },
    { "synced panning", 75.0f, 0.30f, 60.0f, 0.0f, true, true, true },
    { "bypassed", 70.0f, 0.50f, 50.0f, 50.0f, false, false, false },
    { "amount 0", 0.0f, 0.50f, 50.0f, 50.0f, false, false, true },
};

/** A checksum over the finished audio. FNV-1a over the raw sample bits, so a
    single changed bit anywhere in three seconds of stereo shows up - which is
    the whole point. Bit patterns rather than rounded values: "sounds the same"
    is not the bar for a move. */
juce::String checksum (const juce::AudioBuffer<float>& buffer)
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
    per-block error; the sync alignment in particular runs once a block, so how
    long a block is changes how often it runs. */
int nextBlockSize (int index)
{
    static const int sizes[] = { 512, 64, 480, 128, 331, 1024, 17 };
    return sizes[index % (sizeof (sizes) / sizeof (sizes[0]))];
}

bool runPass (const Pass& pass, const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    PeakTremPanProcessor processor;

    setPercent (processor.apvts, "amount", pass.amount);
    setNormalised (processor.apvts, "rate", pass.rate01);
    setPercent (processor.apvts, "shape", pass.shape);
    setPercent (processor.apvts, "bias", pass.bias);
    setFlag (processor.apvts, "mode", pass.panning);
    setFlag (processor.apvts, "sync", pass.synced);
    setFlag (processor.apvts, "on", pass.engaged);

    FakePlayHead playHead { 128.0 };
    processor.setPlayHead (&playHead);

    processor.setPlayConfigDetails (2, 2, kSampleRate, 1024);
    processor.prepareToPlay (kSampleRate, 1024);

    output.makeCopyOf (input);

    juce::MidiBuffer midi;
    const int jumpAt = kLength / 3;
    bool jumped = false;
    int blockIndex = 0;

    for (int offset = 0; offset < kLength;)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), kLength - offset);

        if (! jumped && offset >= jumpAt)
        {
            // Relocate: the processor should snap its phase rather than crawl
            // towards the new position over several seconds.
            playHead.jump (64.0);
            jumped = true;
        }

        juce::AudioBuffer<float> slice (output.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);

        playHead.advance (chunk);
        offset += chunk;
    }

    processor.setPlayHead (nullptr);

    for (int ch = 0; ch < output.getNumChannels(); ++ch)
        for (int i = 0; i < output.getNumSamples(); ++i)
            if (! std::isfinite (output.getSample (ch, i)))
            {
                std::printf ("  NON-FINITE at channel %d sample %d\n", ch, i);
                return false;
            }

    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    // Optional: a folder to drop one wav per pass into, for listening to a
    // difference the checksums have already found.
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    juce::AudioBuffer<float> input (2, kLength);
    fillTestSignal (input);

    juce::AudioBuffer<float> output (2, kLength);
    juce::WavAudioFormat wav;
    bool ok = true;

    std::printf ("Peak Trem & Pan - %d passes at %.0f Hz, %d s each, ragged blocks\n\n",
                 static_cast<int> (sizeof (kPasses) / sizeof (kPasses[0])), kSampleRate, kSeconds);

    for (const auto& pass : kPasses)
    {
        if (! runPass (pass, input, output))
        {
            std::printf ("%-30s FAILED\n", pass.name);
            ok = false;
            continue;
        }

        std::printf ("%-30s %s  peak %.6f  rms %.6f\n", pass.name, checksum (output).toRawUTF8(),
                     output.getMagnitude (0, kLength), output.getRMSLevel (0, 0, kLength));

        if (outDir != juce::File())
        {
            outDir.createDirectory();
            const auto file = outDir.getChildFile (juce::String (pass.name).replaceCharacter (' ', '-') + ".wav");
            file.deleteFile();

            if (auto stream = file.createOutputStream())
                if (std::unique_ptr<juce::AudioFormatWriter> writer {
                        wav.createWriterFor (stream.release(), kSampleRate, 2, 24, {}, 0) })
                    writer->writeFromAudioSampleBuffer (output, 0, kLength);
        }
    }

    std::printf ("\n%s\n", ok ? "all passes finite" : "FAILURES ABOVE");
    return ok ? 0 : 1;
}
