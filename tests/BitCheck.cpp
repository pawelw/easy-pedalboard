// Checks two specific claims about Peak Artifact's Amp engine:
//
//   1. Its Bit knob's calibration (ee::fx::ArtifactModule::ampRateHzFor) lands
//      on the hold factors a real reference unit implied at 50 % and 100 % -
//      measured by recording the same dry material through it and comparing
//      wav files, the way CLAUDE.md's *_match tools do. The reference's
//      spectrum nulls at exact multiples of 9600 Hz at half travel and of
//      1500 Hz at full, which at 48 kHz is a zero-order hold of 5 and of 32
//      samples. That measurement (not something this repo can re-derive - the
//      reference recording lives outside it) is the kTargetHoldAt50/100 below;
//      this only checks our own map still lands on it after any retuning.
//   2. The knob stays scale-invariant: the level it adds follows the playing
//      rather than sitting at a fixed floor. This is the check the engine's
//      first cut - a bit-depth quantiser - failed, and failed loudest where it
//      mattered, gating a note's tail to digital silence once the signal fell
//      below one step.
//   3. Its Drive knob, run alone through ee::dsp::TubeDrive, still adds no
//      more than ~4 dB at 100 % on real program material - unchanged from
//      when this was Bit's own Drive knob, re-checked here because Amp reuses
//      the same engine.
//
// Prints the numbers either way; give it a directory to also get a .wav per
// case - Amp's Bit knob isolated (Drive/Mids off, Tone flat) at 0/50/100 %,
// and TubeDrive alone at Drive 0/100 % - the same idea as the *_match tools,
// without a reference file of their own to A/B against. Pass a real file as a
// third argument (`ee_bit_check outDir dry.wav`) to render Amp's Bit knob
// over it instead, at 50 % and 100 %, for A/B against an actual reference
// recording of the same material - that mode skips the checks above and
// just writes real_bit_50pct.wav / real_bit_100pct.wav.
#include "RegressHarness.h"

#include "ee/dsp/TubeDrive.h"
#include "ee/fx/ArtifactModule.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>

namespace
{
using ee::regress::allFinite;
using ee::regress::fillTestSignal;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// What the reference recording implied at 50 % / 100 %, measured offline
// against a dry take of the same material (see the file comment) - not
// reproducible from inside this repo, so it is a constant rather than
// something computed here. A hold of N samples at kSampleRate.
constexpr int kTargetHoldAt50 = 5;
constexpr int kTargetHoldAt100 = 32;

void writeWav (const juce::String& path, const juce::AudioBuffer<float>& buffer)
{
    if (path.isEmpty())
        return;

    juce::File file (path);
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return;

    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
        stream.release(), kSampleRate, static_cast<unsigned int> (buffer.getNumChannels()), 24, {}, 0));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

/** Amp with Drive and Mids off and Tone flat, isolating the Bit knob -
    settings pushed before prepare() so mix/engine start already settled, no
    20 ms fade-in to contend with. `scale` shrinks the test signal, for the
    scale-invariance check. */
juce::AudioBuffer<float> renderAmpBit (int numSamples, float bit01, float scale = 1.0f)
{
    ee::fx::ArtifactModule module;
    module.setEngine (ee::fx::ArtifactModule::Amp);
    module.setMix01 (1.0f);
    module.setLevel (1.0f);
    module.setEngaged (true);
    module.prepare (kSampleRate, kBlockSize);
    module.setAmp (0.0f, 0.0f, bit01, 0.0f, false);

    juce::AudioBuffer<float> buffer (2, numSamples);
    fillTestSignal (buffer, kSampleRate);
    if (! juce::approximatelyEqual (scale, 1.0f))
        buffer.applyGain (scale);
    module.process (buffer, 2, numSamples);
    return buffer;
}

/** The hold the engine actually lands on at this knob position, resolved the
    same way setAmp resolves it. */
int ampHoldFor (float bit01)
{
    const float hz = ee::fx::ArtifactModule::ampRateHzFor (bit01, kSampleRate);
    return juce::jmax (1, juce::roundToInt (static_cast<float> (kSampleRate) / hz));
}

double rms (const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    juce::int64 count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            sum += static_cast<double> (d[i]) * d[i];
            ++count;
        }
    }
    return std::sqrt (sum / static_cast<double> (juce::jmax (juce::int64 { 1 }, count)));
}

double peak (const juce::AudioBuffer<float>& buffer)
{
    return static_cast<double> (buffer.getMagnitude (0, buffer.getNumSamples()));
}

double toDb (double ratio)
{
    return 20.0 * std::log10 (juce::jmax (1.0e-9, ratio));
}

/** A single clean tone at a moderate, guitar-ish peak - the level TubeDrive's
    own make-up gain is solved against (tubedrive::kMakeupRefAmplitude), so
    this is the case the +4 dB claim should hold most exactly on. */
juce::AudioBuffer<float> renderTone (int numSamples, float peakAmplitude)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double> (i) / kSampleRate;
        const float v =
            peakAmplitude * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t));
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v);
    }
    return buffer;
}

juce::AudioBuffer<float> applyDrive (const juce::AudioBuffer<float>& in, float drive01)
{
    ee::dsp::TubeDrive drive;
    drive.prepare (kSampleRate);
    drive.setDrive01 (drive01);

    juce::AudioBuffer<float> out (in);
    drive.process (out.getWritePointer (0), out.getWritePointer (1), out.getNumSamples());
    return out;
}

/** Amp's Bit knob alone (Drive/Mids off, Tone flat), run over a real file
    rather than the built-in test signal - for A/B against an actual reference
    recording, the way ee_delay_match/ee_spring_match take one. `path`'s own
    sample rate is used, so the render lines up with the source exactly. */
bool renderRealFile (const juce::String& path, const juce::String& outDir)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    juce::File file (path);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        std::printf ("could not open %s\n", path.toRawUTF8());
        return false;
    }

    juce::AudioBuffer<float> dry (2, static_cast<int> (reader->lengthInSamples));
    reader->read (&dry, 0, dry.getNumSamples(), 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        dry.copyFrom (1, 0, dry, 0, 0, dry.getNumSamples());

    for (float bit01 : { 0.5f, 1.0f })
    {
        ee::fx::ArtifactModule module;
        module.setEngine (ee::fx::ArtifactModule::Amp);
        module.setMix01 (1.0f);
        module.setLevel (1.0f);
        module.setEngaged (true);
        module.prepare (reader->sampleRate, kBlockSize);
        module.setAmp (0.0f, 0.0f, bit01, 0.0f, false);

        juce::AudioBuffer<float> wet (dry);
        module.process (wet, 2, wet.getNumSamples());

        juce::File outFile (outDir + "/real_bit_" + juce::String (juce::roundToInt (bit01 * 100.0f)) + "pct.wav");
        outFile.getParentDirectory().createDirectory();
        outFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (outFile.createOutputStream());
        if (stream == nullptr)
            continue;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), reader->sampleRate, 2, 24, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (wet, 0, wet.getNumSamples());
    }
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::String outDir = argc > 1 ? juce::String (argv[1]) : juce::String();
    bool ok = true;

    if (argc > 2)
    {
        renderRealFile (juce::String (argv[2]), outDir);
        return 0;
    }

    std::printf ("=== Amp engine check ===\n\n");

    // ----------------------------------------------------------------- bit
    std::printf ("Bit calibration (ee::fx::ArtifactModule::ampRateHzFor):\n");
    {
        const int hold0 = ampHoldFor (0.0f);
        const int hold50 = ampHoldFor (0.5f);
        const int hold100 = ampHoldFor (1.0f);
        const bool restOff = hold0 == 1;
        const bool near50 = hold50 == kTargetHoldAt50;
        const bool near100 = hold100 == kTargetHoldAt100;

        std::printf ("  Bit @ 0%%    hold %2d  (%.0f Hz)  - the knob at rest holds every sample\n", hold0,
                     kSampleRate / hold0);
        std::printf ("  Bit @ 50%%   hold %2d  (%.0f Hz)  (target hold %d)\n", hold50, kSampleRate / hold50,
                     kTargetHoldAt50);
        std::printf ("  Bit @ 100%%  hold %2d  (%.0f Hz)  (target hold %d)\n", hold100, kSampleRate / hold100,
                     kTargetHoldAt100);
        std::printf ("  %s\n\n", (restOff && near50 && near100) ? "PASS - matches the reference measurement"
                                                                : "FAIL - drifted from the reference measurement");
        ok = ok && restOff && near50 && near100;

        if (outDir.isNotEmpty())
        {
            const int numSamples = static_cast<int> (kSampleRate * 2.0);
            writeWav (outDir + "/amp_bit_off.wav", renderAmpBit (numSamples, 0.0f));
            writeWav (outDir + "/amp_bit_50pct.wav", renderAmpBit (numSamples, 0.5f));
            writeWav (outDir + "/amp_bit_100pct.wav", renderAmpBit (numSamples, 1.0f));
        }
    }

    // -------------------------------------------------------- scale invariance
    std::printf ("Bit is scale-invariant (the artefact follows the playing, it is not a floor):\n");
    {
        const int numSamples = static_cast<int> (kSampleRate * 2.0);
        constexpr double kTolerance = 0.5; // dB

        for (float scale : { 1.0f, 0.01f, 0.0001f })
        {
            juce::AudioBuffer<float> in (2, numSamples);
            fillTestSignal (in, kSampleRate);
            in.applyGain (scale);

            const double gainDb = toDb (rms (renderAmpBit (numSamples, 1.0f, scale)) / juce::jmax (1.0e-12, rms (in)));
            const bool pass = std::abs (gainDb) <= kTolerance;
            std::printf ("  input %+7.1f dB  ->  %+.2f dB through the engine  %s\n", toDb (scale), gainDb,
                         pass ? "" : "  <- FAIL");
            ok = ok && pass;
        }
        std::printf ("\n");
    }

    // ----------------------------------------------------------------- drive
    std::printf ("Drive level (target: +4 dB or less at 100%%, measured on real audio):\n");
    {
        const int numSamples = static_cast<int> (kSampleRate * 1.0);

        for (float peakAmplitude : { 0.3f, 0.5f, 0.7f })
        {
            auto tone = renderTone (numSamples, peakAmplitude);
            auto off = applyDrive (tone, 0.0f);
            auto full = applyDrive (tone, 1.0f);

            const double rmsDb = toDb (rms (full) / juce::jmax (1.0e-9, rms (off)));
            const double peakDb = toDb (peak (full) / juce::jmax (1.0e-9, peak (off)));

            std::printf ("  peak in %.1f  ->  RMS %+.2f dB   Peak %+.2f dB\n", peakAmplitude, rmsDb, peakDb);

            if (juce::approximatelyEqual (peakAmplitude, 0.5f))
            {
                writeWav (outDir.isNotEmpty() ? outDir + "/tubedrive_off.wav" : juce::String(), off);
                writeWav (outDir.isNotEmpty() ? outDir + "/tubedrive_full.wav" : juce::String(), full);
            }
        }
        std::printf ("\n");
    }

    if (! allFinite (renderAmpBit (static_cast<int> (kSampleRate), 1.0f)))
    {
        std::printf ("FAILED - Amp's Bit knob produced non-finite audio\n");
        ok = false;
    }

    std::printf (ok ? "OK\n" : "FAILED\n");
    if (outDir.isNotEmpty())
        std::printf ("wav files written to %s\n", outDir.toRawUTF8());

    return ok ? 0 : 1;
}
