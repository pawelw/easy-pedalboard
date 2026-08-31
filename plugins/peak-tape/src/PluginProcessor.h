#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/TapeMachine.h"

class PeakTapeProcessor : public juce::AudioProcessor
{
public:
    PeakTapeProcessor();
    ~PeakTapeProcessor() override = default;

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

    std::atomic<float>* wearParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* stereoParam = nullptr;
    std::atomic<float>* noiseParam = nullptr;
    std::atomic<float>* saturationParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    ee::dsp::TapeMachine machine;

    /** The tape floor recording, decoded once and looped by the engine. */
    void loadNoiseSample();

    juce::AudioBuffer<float> noiseSample;
    std::vector<const float*> noiseChannels;
    double noiseSampleRate = 44100.0;

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> wetMix;   // 1 = through the machine, 0 = clean dry (bypass)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakTapeProcessor)
};
