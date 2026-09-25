// Runs BitBit Alpine's tuner (ee/dsp/Tuner.h) over a real recording, on the
// editor's 45 Hz clock, and prints what the needle would show frame by frame -
// the *_match idea applied to a meter: play the same file into Live's Tuner and
// compare by eye.
//
//   ee_tuner_match in.wav            every frame: time, level, raw estimate, needle
//   ee_tuner_match in.wav --summary  just the per-note summary
//
// The summary is what matters for stability: for each run of frames on one
// note, how far the needle wandered (frame-to-frame and overall) and where it
// settled.
#include "ee/dsp/Tuner.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <memory>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: ee_tuner_match in.wav [--summary]\n");
        return 2;
    }

    const bool summaryOnly = argc > 2 && juce::String (argv[2]) == "--summary";

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (argv[1])));
    if (reader == nullptr)
    {
        std::printf ("cannot read %s\n", argv[1]);
        return 1;
    }

    const double sr = reader->sampleRate;
    const int numCh = static_cast<int> (reader->numChannels);
    juce::AudioBuffer<float> audio (numCh, static_cast<int> (reader->lengthInSamples));
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);

    ee::dsp::TunerCapture capture;
    capture.prepare (sr);
    capture.setActive (true);
    ee::dsp::TunerAnalyser analyser;
    analyser.reset();

    constexpr int kBlock = 256;
    constexpr double kFrame = 1.0 / 45.0;
    double nextFrame = kFrame;

    struct Run
    {
        int note = -1;
        double start = 0.0, end = 0.0;
        int frames = 0;
        float min = 1e9f, max = -1e9f, last = 0.0f;
        double sumStep = 0.0;
    };
    std::vector<Run> runs;
    Run cur;

    std::vector<float> window (ee::dsp::tuner::kWindow), scratch, filtered;

    if (! summaryOnly)
        std::printf ("    time   level    fund Hz  whole Hz  aper   needle\n");

    for (int n = 0; n < audio.getNumSamples(); n += kBlock)
    {
        const int len = juce::jmin (kBlock, audio.getNumSamples() - n);
        capture.process (audio.getReadPointer (0, n), numCh > 1 ? audio.getReadPointer (1, n) : nullptr, len);

        const double t = static_cast<double> (n + len) / sr;
        while (t >= nextFrame)
        {
            nextFrame += kFrame;
            const auto r = analyser.update (capture, kFrame);

            if (! summaryOnly)
            {
                ee::dsp::PitchEstimate est, whole;
                if (capture.copyLatest (window.data(), ee::dsp::tuner::kWindow))
                {
                    est = ee::dsp::detectPitch (window.data(), ee::dsp::tuner::kWindow, capture.sampleRate(), scratch,
                                                filtered, true);
                    whole = ee::dsp::detectPitch (window.data(), ee::dsp::tuner::kWindow, capture.sampleRate(),
                                                  scratch, filtered, false);
                }
                std::printf ("  %6.3f  %5.1f dB  %8.2f  %8.2f  %.3f   %s\n", t,
                             juce::Decibels::gainToDecibels (est.rms, -100.0f), est.hz, whole.hz, est.aperiodicity,
                             r.signal ? (juce::String (ee::dsp::TunerReading::noteName (r.midiNote))
                                         + juce::String (ee::dsp::TunerReading::octave (r.midiNote)) + " "
                                         + juce::String (r.cents, 1) + " ct" + (r.holding ? " (hold)" : ""))
                                            .toRawUTF8()
                                      : "-");
            }

            const bool live = r.signal && ! r.holding;
            if (live && r.midiNote == cur.note)
            {
                cur.sumStep += std::abs (r.cents - cur.last);
                cur.last = r.cents;
                cur.min = std::min (cur.min, r.cents);
                cur.max = std::max (cur.max, r.cents);
                cur.end = t;
                ++cur.frames;
            }
            else
            {
                if (cur.frames > 0)
                    runs.push_back (cur);
                cur = {};
                if (live)
                {
                    cur.note = r.midiNote;
                    cur.start = cur.end = t;
                    cur.frames = 1;
                    cur.min = cur.max = cur.last = r.cents;
                }
            }
        }
    }
    if (cur.frames > 0)
        runs.push_back (cur);

    std::printf ("\nper note run (>= 5 frames):\n     from      to  note  frames  range ct   mean |step| ct  final ct\n");
    for (const auto& run : runs)
    {
        if (run.frames < 5)
            continue;
        std::printf ("  %6.2f  %6.2f  %-4s  %6d  %8.2f  %14.2f  %8.2f\n", run.start, run.end,
                     (juce::String (ee::dsp::TunerReading::noteName (run.note))
                      + juce::String (ee::dsp::TunerReading::octave (run.note)))
                         .toRawUTF8(),
                     run.frames, run.max - run.min, run.sumStep / std::max (1, run.frames - 1), run.last);
    }
    return 0;
}
