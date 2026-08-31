#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/AutoWah.h"

class PeakWahProcessor : public juce::AudioProcessor
{
public:
    PeakWahProcessor();
    ~PeakWahProcessor() override = default;

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

    /** Text under the Freq knob - the heel centre frequency in Hz. */
    juce::String freqReadout() const;

    /** Text under the Time knob - the note value when synced, the LFO period in
        milliseconds when free. */
    juce::String timeReadout() const;

    /** Text under the Type knob - "Low" / "Band" / "High". */
    juce::String typeReadout() const;

    /** Live LFO phase [0, 1) and effective depth (Amount * gate), for the scope
        in the editor. Written from the audio thread, read on the message thread. */
    std::atomic<float> lfoPhaseUi { 0.0f };
    std::atomic<float> lfoDepthUi { 0.0f };

    /** Called by the editor when the Sync button is clicked: parks the Time knob
        where the mode being left had it and recalls the new mode's spot. */
    void onSyncToggled();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int kMaxChannels = 2;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* freqParam = nullptr;
    std::atomic<float>* qParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* stereoParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* typeParam = nullptr;
    std::atomic<float>* randomParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    ee::dsp::AutoWah wah;

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> wetMix;   // 1 = processed, 0 = clean dry (bypass)

    // The Time knob is shared between free and synced modes; each mode's last
    // position is remembered so flipping Sync restores where that mode was left.
    std::atomic<float> storedSyncRate01 { 0.5f };
    std::atomic<float> storedFreeRate01 { 0.5f };

    // The LFO free-runs on this phase; when synced it is aligned to the host
    // timeline (a hard snap on a transport jump, a gentle pull otherwise).
    double expectedPpq = 0.0;
    bool haveExpectedPpq = false;
    bool wasPlaying = false;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakWahProcessor)
};
