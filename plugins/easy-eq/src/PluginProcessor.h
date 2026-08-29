#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

class EasyEqProcessor : public juce::AudioProcessor
{
public:
    EasyEqProcessor();
    ~EasyEqProcessor() override = default;

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

    /** Fixed centre frequencies, low to high - the Boss GE-7 band set. */
    static constexpr int kNumBands = 7;
    static constexpr std::array<float, kNumBands> kBandFrequencies { {
        100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f
    } };

    // Low/high cut sweep limits. At the far end each stage is bypassed and its
    // knob reads infinity.
    static constexpr float kLoCutMinHz = 20.0f;
    static constexpr float kLoCutMaxHz = 1200.0f;
    static constexpr float kHiCutMinHz = 1200.0f;
    static constexpr float kHiCutMaxHz = 20000.0f;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateFilters (bool force);

    static constexpr int kMaxChannels = 2;

    using BandFilter = juce::dsp::IIR::Filter<float>;

    // One peak filter per band, per channel. filters[channel][band].
    std::array<std::array<BandFilter, kNumBands>, kMaxChannels> filters;

    // Cut stages, per channel.
    std::array<BandFilter, kMaxChannels> hiPass;   // driven by the low-cut knob
    std::array<BandFilter, kMaxChannels> loPass;   // driven by the high-cut knob

    std::array<std::atomic<float>*, kNumBands> bandParams {};
    std::array<float, kNumBands> bandGainDb {};

    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* loCutParam = nullptr;
    std::atomic<float>* hiCutParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    float loCutHz = kLoCutMinHz;
    float hiCutHz = kHiCutMaxHz;
    bool hiPassActive = false;
    bool loPassActive = false;

    double sampleRate = 44100.0;

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> levelGain;   // linear make-up gain
    juce::SmoothedValue<float> wetMix;      // 1 = processed, 0 = clean dry

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyEqProcessor)
};
