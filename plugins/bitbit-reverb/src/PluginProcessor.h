#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "ee/fx/ReverbModule.h"
#include "ee/plugin/PresetStore.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

/**
 * BitBit Reverb: BitBit Alpine's Reverb module - Space / Spring - as a pedal of its
 * own, the way BitBit Artifact is Alpine's first module.
 *
 * Space is the FDN this pedal used to be on its own, Spring is BitBit Spring's
 * tank; both run inside ee::fx::ReverbModule, the same object Alpine runs, so
 * the same knob position sounds the same in both. The processor is parameters
 * and plumbing: it reads the knobs and hands the module a global bypass.
 */
class BitBitReverbProcessor : public juce::AudioProcessor
{
public:
    BitBitReverbProcessor();
    ~BitBitReverbProcessor() override;

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
    double getTailLengthSeconds() const override { return module.tailSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Both preset banks. Public because the editor's bridge takes a reference
        to it - see ee/plugin/PresetBridge.h. */
    ee::plugin::PresetStore presets { apvts, "BitBit Reverb", EE_FACTORY_PRESETS };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Reads the knobs and pushes them to the module in real units. */
    void pushSettings() noexcept;

    /** Every route a whole APVTS tree can arrive by goes through this - a host
        restoring a session and the preset store both. Bare replaceState here;
        the hook exists so a later linked control could stand down for it. */
    void installState (const juce::ValueTree& tree);

    static constexpr int kMaxChannels = 2;

    ee::fx::ReverbModule module;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBitReverbProcessor)
};
