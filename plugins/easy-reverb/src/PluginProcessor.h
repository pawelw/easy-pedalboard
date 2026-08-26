#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/FdnReverb.h"

class EasyReverbProcessor : public juce::AudioProcessor
{
public:
    EasyReverbProcessor();
    ~EasyReverbProcessor() override = default;

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ee::dsp::FdnReverb reverb;

    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    juce::SmoothedValue<float> dryGain;
    juce::SmoothedValue<float> wetGain;
    juce::SmoothedValue<float> inputGain;

    juce::AudioBuffer<float> monoBuffer;
    juce::AudioBuffer<float> wetBuffer;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyReverbProcessor)
};
