#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/Grainer.h"

#if EE_GRAIN_TRACE
#include "GrainTrace.h"
#endif

class PeakGrainProcessor : public juce::AudioProcessor
{
public:
    PeakGrainProcessor();
    ~PeakGrainProcessor() override = default;

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
    static constexpr int kMaxChannels = 2;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::String densityReadout() const;

    /** Stretch reads as a dash while playing live - it only bites once the
        buffer is frozen - and as a signed percent, or "hold" at the detent,
        when it does. */
    juce::String stretchReadout() const;

    /** The single Reverb knob drives the network's decay as well as its mix -
        one control, because the two are never usefully set apart. */
    static float reverbDecaySeconds (float percent) noexcept;

    ee::dsp::Grainer grainer;
    ee::dsp::FdnReverb reverb;

    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* stretchParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* scatterParam = nullptr;
    std::atomic<float>* reverseParam = nullptr;
    std::atomic<float>* stereoParam = nullptr;
    std::atomic<float>* detuneParam = nullptr;
    std::atomic<float>* pitchLowParam = nullptr;
    std::atomic<float>* pitchUnisonParam = nullptr;
    std::atomic<float>* pitchHighParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    juce::SmoothedValue<float> dryGain;
    juce::SmoothedValue<float> wetGain;
    juce::SmoothedValue<float> inputGain;
    juce::SmoothedValue<float> grainGain;
    juce::SmoothedValue<float> verbGain;

    // The grain cloud, its mono sum for the reverb send, and the reverb's own
    // stereo return. Sized once in prepareToPlay.
    juce::AudioBuffer<float> grainBuffer;
    juce::AudioBuffer<float> monoBuffer;
    juce::AudioBuffer<float> verbBuffer;
    int maxBlock = 512;

#if EE_GRAIN_TRACE
    std::unique_ptr<GrainTrace> trace;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakGrainProcessor)
};
