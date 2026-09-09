#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "ee/fx/ArtifactModule.h"
#include "ee/plugin/PresetStore.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

/**
 * Peak Artifact: one switchable module - Ring Mod / Bit Crush / Filter - as a
 * pedal of its own, the way Peak Delay is its own plugin as well as a module
 * inside Peak Alpine.
 *
 * Only Filter is voiced. The other two engines are selectable and pass audio
 * through untouched - see ee::fx::ArtifactModule. The processor is parameters
 * and plumbing: it reads the knobs, turns the Time knob into an LFO period the
 * way Peak Wah does, and hands the module a global bypass.
 */
class PeakArtifactProcessor : public juce::AudioProcessor
{
public:
    PeakArtifactProcessor();
    ~PeakArtifactProcessor() override;

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

    /** Both preset banks. Public because the editor's bridge takes a reference
        to it - see ee/plugin/PresetBridge.h. */
    ee::plugin::PresetStore presets { apvts, "Peak Artifact", EE_FACTORY_PRESETS };

    /** Text under the Filter Time knob - the LFO period in ms when free, the
        note value when synced. The web view has no Sync pill or host tempo, so
        the processor answers this rather than the parameter's own text. */
    juce::String timeReadout() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Reads the knobs and pushes them to the module in real units. */
    void pushSettings (double bpm) noexcept;

    /** Every route a whole APVTS tree can arrive by goes through this - a host
        restoring a session and the preset store both. Bare replaceState here;
        the hook exists so a later linked control could stand down for it. */
    void installState (const juce::ValueTree& tree);

    static constexpr int kMaxChannels = 2;

    ee::fx::ArtifactModule module;

    double sampleRate = 44100.0;

    // LFO transport alignment, as Peak Wah does it.
    double expectedPpq = 0.0;
    bool haveExpectedPpq = false;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakArtifactProcessor)
};
