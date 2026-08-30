#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/Chorus.h"

class EasyChorusProcessor : public juce::AudioProcessor
{
public:
    EasyChorusProcessor();
    ~EasyChorusProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Text under the Rate knob - the LFO speed in Hz. Read by the editor's
        live readout closure. */
    juce::String rateReadout() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int kMaxChannels = 2;

    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* phaseParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    ee::dsp::Chorus chorus;

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> scratchBuffer;   // discarded right channel for a mono output bus
    juce::SmoothedValue<float> wetMix;        // 1 = processed, 0 = clean dry (bypass)

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyChorusProcessor)
};
