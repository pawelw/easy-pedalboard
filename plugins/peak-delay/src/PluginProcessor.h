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

    /** Text under a Time knob: the division label ("1/8") normally, or that
        division's length at the current host tempo in milliseconds when the
        ms button is on. Re-derives the label rather than reading it off the
        parameter, so it is correct however this is called. */
    juce::String timeReadout (const std::atomic<float>* timeParam) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void mirrorDivision (const juce::String& from, const juce::String& to);

    /** Host tempo, clamped the same way processBlock's own lookup is. Safe off
        the audio thread - this is only ever called from the editor. */
    double currentBpm() const;

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
