#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/Overdrive.h"

class EasyOverdriveProcessor : public juce::AudioProcessor
{
public:
    EasyOverdriveProcessor();
    ~EasyOverdriveProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

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

    static constexpr int kMaxChannels = 2;

    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    ee::dsp::Overdrive overdrive;

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> levelGain;   // linear output make-up gain
    juce::SmoothedValue<float> wetMix;      // 1 = processed, 0 = clean dry (bypass)

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyOverdriveProcessor)
};
