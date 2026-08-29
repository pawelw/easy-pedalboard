#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class EasyTremPanProcessor : public juce::AudioProcessor
{
public:
    EasyTremPanProcessor();
    ~EasyTremPanProcessor() override = default;

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

    /** Text under the Rate knob - the note value when synced, the period in
        milliseconds when free. Read by the editor's live readout closure. */
    juce::String rateReadout() const;

    /** Called by the editor when the user clicks the Sync toggle: parks the Rate
        knob where the mode being left had it and recalls the new mode's spot. */
    void onSyncToggled();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int kMaxChannels = 2;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    // The Rate knob is shared between the two modes; each mode's last position is
    // remembered so flipping the Sync switch restores where that mode was left.
    std::atomic<float> storedSyncRate01 { 0.5f };
    std::atomic<float> storedFreeRate01 { 0.5f };

    juce::SmoothedValue<float> depth;    // 0..1 LFO amount
    juce::SmoothedValue<float> wetMix;   // 1 = processed, 0 = clean dry

    juce::AudioBuffer<float> dryBuffer;
    std::vector<float> modBuffer;        // shaped LFO, one value per sample

    double lfoPhase = 0.0;               // [0, 1); free-running fallback
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyTremPanProcessor)
};
