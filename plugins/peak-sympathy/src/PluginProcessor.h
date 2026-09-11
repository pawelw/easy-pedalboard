#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/ResonatorBank.h"
#include "ee/plugin/PresetStore.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

/**
 * Peak Sympathy: a sympathetic-resonance effect. The player's signal barely
 * passes through; instead it excites a bank of sixteen tuned string resonators
 * that ring, bloom and beat against each other - a piano with the sustain
 * pedal down, a sitar's sympathetic strings.
 *
 * The processor is parameters and plumbing. All the sound - the exciter, the
 * per-string energy limiting and bank soft limiter, the envelope-driven dry
 * duck, the coupling matrix and the tuning tables - is ee::dsp::ResonatorBank.
 * The one thing the processor does to the audio itself is the Mix / bypass
 * crossfade and, while Key-Learn is held, the pitch tracker that steers the
 * bank's root.
 */
class PeakSympathyProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    PeakSympathyProcessor();
    ~PeakSympathyProcessor() override;

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
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Both preset banks. Public because the editor's preset bar takes a
        reference - see ee/plugin/PresetStore.h. */
    ee::plugin::PresetStore presets { apvts, "Peak Sympathy", EE_FACTORY_PRESETS };

    /** The key-name the bank is actually tuned to right now: the Key knob's
        choice normally, or the pitch tracker's estimate while Learn is held.
        Read by the editor's live readout under the Key knob. */
    juce::String keyReadout() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& id, float newValue) override;

    /** Every route a whole APVTS tree can arrive by. Bare replaceState; the
        hook exists for symmetry with the other pedals on this store. */
    void installState (const juce::ValueTree& tree);

    /** Autocorrelation pitch tracker for Key-Learn. Fed the block's mono mix;
        returns a semitone (0..11, C-based) once an estimate has held steady,
        or -1 while it has not. */
    int trackKey (const float* mono, int numSamples);

    static constexpr int kMaxChannels = 2;

    ee::dsp::ResonatorBank bank;

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;
    std::vector<float> dryGain;

    juce::SmoothedValue<float> wetMix; // 1 = effect in, 0 = bypassed (dry)

    double sampleRate = 44100.0;

    // Key-Learn state (audio thread writes, message thread copies out on the
    // Learn button releasing).
    std::atomic<int> learnedKey { -1 };
    std::vector<float> pitchRing;
    std::vector<float> pitchWork; // unwrapped copy for the tracker, preallocated
    int pitchRingWrite = 0;
    int learnCandidate = -1;
    int learnHoldSamples = 0;

    std::atomic<float>* onParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* keyParam = nullptr;
    std::atomic<float>* octaveParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* couplingParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* bloomParam = nullptr;
    std::atomic<float>* sensParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;
    std::atomic<float>* learnParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakSympathyProcessor)
};
