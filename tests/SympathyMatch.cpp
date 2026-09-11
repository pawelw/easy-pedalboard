// Renders a dry file through Peak Sympathy's resonator engine so a voicing can
// be A/B'd by ear against the dry take. Drives ee::dsp::ResonatorBank directly
// (no plugin), every control on the command line.
//
//   ee_sympathy_match <in.wav> <out.wav>
//       [mode 0-5=3] [key 0-11=0] [octave -2..2=0]
//       [decay 0-100=45] [damping 0-100=55] [coupling 0-100=25]
//       [spread 0-100=25] [bloom 0-100=0] [sensitivity 0-100=55]
//       [mix 0-100=50] [freeze 0/1=0] [wetOnly 0/1=0]
//
// mode: 0 octaves  1 fifths  2 harmonic  3 major(JI)  4 minor(JI)  5 chroma
//
// Feed a dry DI guitar. `mix` blends dry (ducked) and wet; `wetOnly 1` writes
// just the bank. Pad the input with silence to hear the ring decay.
#include <juce_audio_formats/juce_audio_formats.h>

#include "ee/dsp/ResonatorBank.h"

#include <cmath>

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::printf ("usage: %s <in.wav> <out.wav> [mode 0-5] [key 0-11] [octave -2..2]"
                     " [decay] [damping] [coupling] [spread] [bloom] [sensitivity]"
                     " [mix] [freeze] [wetOnly]\n",
                     argv[0]);
        return 1;
    }

    const juce::File inFile { juce::String (argv[1]) };
    const juce::File outFile { juce::String (argv[2]) };

    const auto arg = [argc, argv] (int index, float fallback)
    {
        return index < argc ? juce::String (argv[index]).getFloatValue() : fallback;
    };

    const int   mode    = juce::jlimit (0, 5, static_cast<int> (arg (3, 3.0f)));
    const int   key     = juce::jlimit (0, 11, static_cast<int> (arg (4, 0.0f)));
    const int   octave  = juce::jlimit (-2, 2, static_cast<int> (arg (5, 0.0f)));
    const float decay   = juce::jlimit (0.0f, 1.0f, arg (6, 45.0f) * 0.01f);
    const float damping = juce::jlimit (0.0f, 1.0f, arg (7, 55.0f) * 0.01f);
    const float couple  = juce::jlimit (0.0f, 1.0f, arg (8, 25.0f) * 0.01f);
    const float spread  = juce::jlimit (0.0f, 1.0f, arg (9, 25.0f) * 0.01f);
    const float bloom   = juce::jlimit (0.0f, 1.0f, arg (10, 0.0f) * 0.01f);
    const float sens    = juce::jlimit (0.0f, 1.0f, arg (11, 55.0f) * 0.01f);
    const float mix     = juce::jlimit (0.0f, 1.0f, arg (12, 50.0f) * 0.01f);
    const bool  freeze  = arg (13, 0.0f) > 0.5f;
    const bool  wetOnly = arg (14, 0.0f) > 0.5f;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("could not read %s\n", inFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const int numSamples = static_cast<int> (reader->lengthInSamples);
    const int numChannels = static_cast<int> (reader->numChannels);

    juce::AudioBuffer<float> in (juce::jmax (2, numChannels), numSamples);
    in.clear();
    reader->read (&in, 0, numSamples, 0, true, true);
    if (numChannels == 1)
        in.copyFrom (1, 0, in, 0, 0, numSamples);

    juce::AudioBuffer<float> wet (2, numSamples);
    wet.clear();
    std::vector<float> dryGain (static_cast<size_t> (juce::jmax (1, numSamples)), 1.0f);

    ee::dsp::ResonatorBank bank;
    bank.prepare (reader->sampleRate);
    bank.reset();
    bank.setTuning (static_cast<ee::dsp::sympathy::TuningMode> (mode), key);
    bank.setOctave (octave);
    bank.setDecay01 (decay);
    bank.setDamping01 (damping);
    bank.setCoupling01 (couple);
    bank.setSpread01 (spread);
    bank.setBloom01 (bloom);
    bank.setSensitivity01 (sens);
    bank.setFreeze (freeze);

    constexpr int kBlock = 512;
    for (int pos = 0; pos < numSamples; pos += kBlock)
    {
        const int nn = juce::jmin (kBlock, numSamples - pos);
        bank.updateBlock (nn);
        bank.render (in.getReadPointer (0, pos), in.getReadPointer (1, pos),
                     wet.getWritePointer (0, pos), wet.getWritePointer (1, pos),
                     dryGain.data() + pos, nn);
    }

    // Mix: the dry side is scaled per-sample by (1 - envelope duck), so the
    // wash steps forward while the player digs in and recovers after.
    double wetSumSq = 0.0;
    float wetPeak = 0.0f;
    juce::AudioBuffer<float> out (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* d = in.getReadPointer (ch);
        const float* w = wet.getReadPointer (ch);
        float* o = out.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            wetSumSq += static_cast<double> (w[i]) * w[i];
            wetPeak = juce::jmax (wetPeak, std::abs (w[i]));
            const float dry = d[i] * dryGain[static_cast<size_t> (i)];
            o[i] = wetOnly ? w[i] : (1.0f - mix) * dry + mix * w[i];
        }
    }
    const double wetRms = numSamples > 0 ? std::sqrt (wetSumSq / (2.0 * numSamples)) : 0.0;

    outFile.deleteFile();
    juce::WavAudioFormat wavFmt;
    if (auto stream = outFile.createOutputStream())
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFmt.createWriterFor (stream.release(), reader->sampleRate, 2, 24, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (out, 0, numSamples);
    }

    static const char* kModeName[] = { "octaves", "fifths", "harmonic", "major", "minor", "chroma" };
    std::printf ("wrote %s (%d samples @ %.0f Hz)\n"
                 "  mode %s  key %d  octave %+d  decay %.0f%%  damping %.0f%%  coupling %.0f%%\n"
                 "  spread %.0f%%  bloom %.0f%%  sensitivity %.0f%%  mix %.0f%%  freeze %s%s\n"
                 "  exciter fired %d time(s)   wet rms %.5f  wet peak %.4f  bank peak %.4f\n"
                 "  string 0: %.2f Hz gain %.5f   string 8: %.2f Hz gain %.5f\n",
                 outFile.getFullPathName().toRawUTF8(), numSamples, reader->sampleRate,
                 kModeName[mode], key, octave, decay * 100.0f, damping * 100.0f, couple * 100.0f,
                 spread * 100.0f, bloom * 100.0f, sens * 100.0f, mix * 100.0f,
                 freeze ? "on" : "off", wetOnly ? "  (wet only)" : "",
                 bank.triggerCount(), wetRms, wetPeak, bank.lastPeak(),
                 bank.voiceF0 (0), bank.voiceLoopGain (0), bank.voiceF0 (8), bank.voiceLoopGain (8));
    return 0;
}
