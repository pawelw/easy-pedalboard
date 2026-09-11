// Renders a file through the whole Peak Sympathy processor - parameters, the
// Mix/duck stage and the bypass crossfade included - so it can be checked
// against ee_sympathy_match's engine-only render and against the plugin's own
// defaults. See RegressHarness.h's note on *_match vs *_regress: this answers
// "does the finished pedal sound like the engine says it should", which
// ee_sympathy_match (drives ee::dsp::ResonatorBank directly, no processor) does
// not.
//
//   ee_sympathy_plugin_match <in.wav> <out.wav>
//       [mode 0-5=3] [key 0-11=9] [octave -2..2=0]
//       [decay 0-100=45] [damping 0-100=55] [coupling 0-100=25]
//       [spread 0-100=25] [bloom 0-100=0] [sensitivity 0-100=55]
//       [mix 0-100=50] [freeze 0/1=0] [on 0/1=1]
#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.h"

namespace
{
void setParam (juce::AudioProcessorValueTreeState& state, const char* id, float percent)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, percent * 0.01f));
}

void setChoice (juce::AudioProcessorValueTreeState& state, const char* id, int index)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
}

void setFlag (juce::AudioProcessorValueTreeState& state, const char* id, bool on)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::printf ("usage: %s <in.wav> <out.wav> [mode 0-5] [key 0-11] [octave -2..2]"
                     " [decay] [damping] [coupling] [spread] [bloom] [sensitivity]"
                     " [mix] [freeze] [on]\n",
                     argv[0]);
        return 1;
    }

    const juce::File inFile { juce::String (argv[1]) };
    const juce::File outFile { juce::String (argv[2]) };

    const auto arg = [argc, argv] (int index, float fallback)
    { return index < argc ? juce::String (argv[index]).getFloatValue() : fallback; };

    const int mode = juce::jlimit (0, 5, static_cast<int> (arg (3, 3.0f)));
    const int key = juce::jlimit (0, 11, static_cast<int> (arg (4, 9.0f)));
    const int octave = juce::jlimit (-2, 2, static_cast<int> (arg (5, 0.0f)));
    const float decay = arg (6, 45.0f);
    const float damping = arg (7, 55.0f);
    const float coupling = arg (8, 25.0f);
    const float spread = arg (9, 25.0f);
    const float bloom = arg (10, 0.0f);
    const float sensitivity = arg (11, 55.0f);
    const float mix = arg (12, 50.0f);
    const bool freeze = arg (13, 0.0f) > 0.5f;
    const bool on = arg (14, 1.0f) > 0.5f;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("could not read %s\n", inFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const int numSamples = static_cast<int> (reader->lengthInSamples);

    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();
    reader->read (&buffer, 0, numSamples, 0, true, true);
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    PeakSympathyProcessor processor;

    setChoice (processor.apvts, "tune.mode", mode);
    setChoice (processor.apvts, "tune.key", key);
    setChoice (processor.apvts, "octave", octave);
    setParam (processor.apvts, "decay", decay);
    setParam (processor.apvts, "damping", damping);
    setParam (processor.apvts, "coupling", coupling);
    setParam (processor.apvts, "spread", spread);
    setParam (processor.apvts, "bloom", bloom);
    setParam (processor.apvts, "sensitivity", sensitivity);
    setParam (processor.apvts, "mix", mix);
    setFlag (processor.apvts, "freeze", freeze);
    setFlag (processor.apvts, "on", on);

    constexpr int block = 512;
    processor.setPlayConfigDetails (2, 2, reader->sampleRate, block);
    processor.prepareToPlay (reader->sampleRate, block);

    juce::MidiBuffer midi;
    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int chunk = juce::jmin (block, numSamples - offset);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, offset, chunk);
        processor.processBlock (slice, midi);
    }

    float peak = 0.0f;
    double sumSq = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            peak = juce::jmax (peak, std::abs (d[i]));
            sumSq += static_cast<double> (d[i]) * d[i];
        }
    }

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    if (auto stream = outFile.createOutputStream())
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), reader->sampleRate, 2, 24, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    }

    std::printf ("wrote %s (%d samples @ %.0f Hz)\n"
                 "  mode %d  key %d  octave %+d  decay %.0f%%  damping %.0f%%  coupling %.0f%%\n"
                 "  spread %.0f%%  bloom %.0f%%  sensitivity %.0f%%  mix %.0f%%  freeze %s  on %s\n"
                 "  peak %.5f  rms %.5f  tail latency %d\n",
                 outFile.getFullPathName().toRawUTF8(), numSamples, reader->sampleRate, mode, key, octave, decay,
                 damping, coupling, spread, bloom, sensitivity, mix, freeze ? "on" : "off", on ? "on" : "off", peak,
                 std::sqrt (sumSq / (2.0 * numSamples)), processor.getLatencySamples());
    return 0;
}
