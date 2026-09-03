#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

/** Development-only runaway logger for Peak Grain.

    Built only when EE_GRAIN_TRACE is on. The pedal is stable on the bench under
    every input, block size, sample rate and knob movement that can be
    constructed offline, so when it misbehaves inside a host the question is
    which side of the plugin the energy is on:

      - input already loud when the output is loud -> something is feeding the
        pedal its own output, and the loop is outside the plugin
      - input quiet, output loud                   -> the pedal is generating
        it, and this log says at exactly which settings

    The audio thread only writes two atomics. A timer on the message thread does
    the file I/O, so the log itself can never be the cause of a glitch.
*/
class GrainTrace : private juce::Timer
{
public:
    /** Output peak, in absolute sample value, that counts as a runaway. Roughly
        twice the loudest the pedal reaches on the bench at any setting. */
    static constexpr float kThreshold = 3.0f;

    explicit GrainTrace (juce::AudioProcessorValueTreeState& stateToRead) : state (stateToRead)
    {
        logger.reset (juce::FileLogger::createDefaultAppLogger ("PeakGrain", "trace.log", "Peak Grain trace"));
        startTimer (250);
    }

    ~GrainTrace() override { stopTimer(); }

    /** Called from processBlock. Lock-free, allocation-free. */
    void observe (float inputPeak, float outputPeak) noexcept
    {
        auto raise = [] (std::atomic<float>& slot, float value)
        {
            float current = slot.load (std::memory_order_relaxed);
            while (value > current && ! slot.compare_exchange_weak (current, value, std::memory_order_relaxed))
                ;
        };

        raise (peakIn, inputPeak);
        raise (peakOut, outputPeak);
    }

private:
    void timerCallback() override
    {
        const float in = peakIn.exchange (0.0f, std::memory_order_relaxed);
        const float out = peakOut.exchange (0.0f, std::memory_order_relaxed);

        if (out < kThreshold || logger == nullptr)
            return;

        // One line per second at most, or a runaway fills the disk with the
        // same message.
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastLogMs < 1000)
            return;
        lastLogMs = now;

        juce::String line;
        line << "peak in " << juce::String (in, 3) << "  peak out " << juce::String (out, 3) << "  |";

        for (const char* id : { "size",  "ssync",   "density", "dsync",  "time",   "feedback", "stretch", "freeze",
                                "shape", "scatter", "reverse", "stereo", "detune", "plow",     "puni",    "phigh",
                                "dtime", "dtsync",  "dfb",     "dmix",   "decay",  "rmix",     "mix",     "on" })
            if (auto* parameter = state.getParameter (id))
                line << " " << id << "=" << parameter->getCurrentValueAsText();

        logger->logMessage (line);
    }

    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<juce::FileLogger> logger;

    std::atomic<float> peakIn { 0.0f };
    std::atomic<float> peakOut { 0.0f };
    juce::uint32 lastLogMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainTrace)
};
