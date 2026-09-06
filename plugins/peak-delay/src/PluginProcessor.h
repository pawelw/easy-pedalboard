#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/TapeCharacter.h"
#include "ee/dsp/TapeDelay.h"

class PeakDelayProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    PeakDelayProcessor();
    ~PeakDelayProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Text under a Time knob: the division label ("1/8") when synced, or the
        free-running time ("333 ms", "1.50 s") when the ms button is on.
        Re-derives the text from the knob position rather than reading it off
        the parameter, so it is correct however this is called. */
    juce::String timeReadout (const std::atomic<float>* timeParam) const;

    /** The millisecond reading regardless of the ms toggle - the onyx face's
        Readout shows this as a small, constant figure alongside timeReadout's
        toggle-aware one (which swaps to the very same reading when ms is on),
        rather than only being able to see the ms conversion by turning ms on
        and losing the division label. */
    juce::String timeMsReadout (const std::atomic<float>* timeParam) const;

    /** Thin wrappers over timeReadout()/timeMsReadout() for the web editor,
        which - unlike the old ee::ui editor - isn't a member of this class
        and so can't reach leftTimeParam/rightTimeParam directly. */
    juce::String leftTimeReadout() const { return timeReadout (leftTimeParam); }
    juce::String rightTimeReadout() const { return timeReadout (rightTimeParam); }
    juce::String leftTimeMsReadout() const { return timeMsReadout (leftTimeParam); }
    juce::String rightTimeMsReadout() const { return timeMsReadout (rightTimeParam); }

    /** The tape machine's current/default voicing, for the EE_TAPE_TUNER dev
        panel - same reason as above, the web editor needs a way to reach
        `tape` without being a member of this class. */
    const ee::dsp::TapeTuning& tapeTuning() const noexcept { return tape.getTuning(); }
    void setTapeTuning (const ee::dsp::TapeTuning& t) noexcept { tape.setTuning (t); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void mirrorTime (const juce::String& from, const juce::String& to);

    /** Host tempo, clamped the same way processBlock's own lookup is. */
    double currentBpm() const;

    /** Whether the Time knobs read as note divisions. The "ms" parameter is
        the pill's own sense - true means free-running - so this is its
        inverse, named for what the knobs are actually doing. */
    bool isSynced() const;

    ee::dsp::TapeCharacter tape;
    ee::dsp::TapeDelay delay;

    std::atomic<float>* leftTimeParam = nullptr;
    std::atomic<float>* rightTimeParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* timeUnitParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* modParam = nullptr;
    std::atomic<float>* tapeParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    /** Stops the two time parameters echoing each other forever. */
    std::atomic<bool> mirroring { false };

    juce::SmoothedValue<float> dryGain;
    juce::SmoothedValue<float> wetGain;

    /** 1 while the pedal is engaged, 0 when bypassed. Fades the tape off the
        dry path and closes the delay input, leaving the repeats to ring out. */
    juce::SmoothedValue<float> engageGain;

    juce::AudioBuffer<float> tapedBuffer;
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> wetBuffer;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakDelayProcessor)
};
