#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <vector>

#include "ee/fx/ModulationControls.h"
#include "ee/fx/ModulationModule.h"
#include "ee/plugin/PresetStore.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

/**
 * BitBit Modulation: BitBit Alpine's Modulation module - Tape / Tremolo / Chorus /
 * Phaser / Filter - as a pedal of its own, the way BitBit Artifact is Alpine's
 * first module.
 *
 * The engines are ee::fx::ModulationModule, the same object Alpine runs, and
 * the knob maps are ee/fx/ModulationControls.h, which Alpine reads too - so the
 * same knob position sounds the same in both. The processor is parameters and
 * plumbing: it reads the knobs, hands the module the tape floor recording and
 * the host transport, and reports the module's latency.
 */
class BitBitModulationProcessor : public juce::AudioProcessor
{
public:
    BitBitModulationProcessor();
    ~BitBitModulationProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /** The host's own bypass button. JUCE's default for it is a straight
        pass-through with no delay, which pulls the signal ahead of the latency
        this plugin reports - a host that has just compensated for it. So it is
        the ordinary block with the plugin's power forced off instead: the same
        crossfade to a dry path held back by the reported latency, and none of
        the clicks a hard switch between two code paths would make. The DSP
        keeps running while bypassed, which is what lets the tails ring out and
        the switch back in be seamless. */
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    ee::plugin::PresetStore presets { apvts, "BitBit Modulation", EE_FACTORY_PRESETS };

    /** The Filter engine's Time knob and the Tremolo's Rate knob as the face
        prints them: the LFO period in ms when free, the note value when synced.
        The web view has no Sync switch or host tempo of its own, so the
        processor answers. */
    juce::String filterTimeReadout() const;
    juce::String tremoloRateReadout() const;

    /** The Filter engine's live cutoff-sweep exponent per channel, for the
        face's response scope. Written from the audio thread, read by the
        editor's Timer as one "filterMod" event - the feed BitBit Alpine and BitBit
        Wah push under the same name. */
    std::atomic<float> filterModL { 0.0f };
    std::atomic<float> filterModR { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Reads the knobs and pushes them to the module in real units. */
    void pushSettings (double bpm) noexcept;

    /** Every route a whole APVTS tree can arrive by goes through this - a host
        restoring a session and the preset store both. Bare replaceState here;
        the hook exists so a later linked control could stand down for it. */
    void installState (const juce::ValueTree& tree);

    /** Reads the host's tempo off the playhead into `bpm`. ONLY from
        processBlock/prepareToPlay - see ee::fx::modulation::HostSync. */
    double readPlayHeadBpm();

    /** Decodes the embedded tape floor once and hands it to the tape engine, so
        its Noise knob plays the recording BitBit Tape does rather than the
        synthesised hiss fallback. The buffer is a member: the engine loops from
        these samples and the pointers must outlive it. */
    void loadTapeNoiseSample();

    juce::AudioBuffer<float> tapeNoiseSample;
    std::vector<const float*> tapeNoiseChannels;
    double tapeNoiseSampleRate = 44100.0;

    static constexpr int kMaxChannels = 2;

    ee::fx::ModulationModule module;

    /** True only for the duration of processBlockBypassed - see there. */
    bool hostBypassed = false;
    ee::fx::modulation::HostSync hostSync;

    double sampleRate = 44100.0;
    double bpm = 120.0; // audio thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBitModulationProcessor)
};
