#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "ee/fx/DelayModule.h"
#include "ee/fx/ModulationModule.h"
#include "ee/fx/ReverbModule.h"
#include "ee/plugin/InputMeter.h"
#include "ee/plugin/PresetStore.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

/**
 * Peak Alpine: three effect modules under one chrome.
 *
 * Modulation (Tape / Tremolo / Chorus / Phaser), then Delay - which is Peak
 * Delay's entire chain, the same `ee::fx::DelayModule` that pedal runs - then
 * Reverb (Space / Spring). Every engine in here is the engine its own pedal
 * uses, so nothing can drift from the pedal it came from and a fix lands in
 * both.
 *
 * The order is fixed. Nothing in the design offers to reorder it, and a router
 * would be a second and larger feature.
 *
 * This class is parameters and plumbing. It owns no DSP of its own: what it
 * does is read the knobs, turn them into the real units the three modules take,
 * and wrap the lot in a global bypass.
 */
class PeakAlpineProcessor : public juce::AudioProcessor
{
public:
    PeakAlpineProcessor();
    ~PeakAlpineProcessor() override = default;

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

    /** Both preset banks. Public because the editor's bridge takes a reference
        to it - see ee/plugin/PresetBridge.h. Declared after apvts so the tree it
        reads and writes is already built. */
    ee::plugin::PresetStore presets { apvts, "Peak Alpine", EE_FACTORY_PRESETS };

    /** The Delay module's TapScope feed - the same two numbers Peak Delay's
        face reads, from a second instance of the same meter. */
    ee::plugin::InputMeter inputMeter;

    /** The Filter section's travel, shared with the readouts and with Peak EQ,
        whose Low Cut / High Cut these are the same two filters as. */
    static constexpr float kLoCutMinHz = 20.0f;
    static constexpr float kLoCutMaxHz = 1200.0f;
    static constexpr float kHiCutMinHz = 1200.0f;
    static constexpr float kHiCutMaxHz = 20000.0f;

    /** The Delay module's readouts, for the face. Same three the Peak Delay
        processor answers, and for the same reason: what a Time knob position
        means depends on the Sync pill and, when synced, on the host tempo,
        none of which the web view knows. */
    juce::String timeReadout (const char* parameterId) const;
    juce::String timeMsReadout (const char* parameterId) const;
    float timeMs (const char* parameterId) const;

    double hostBpm() const { return currentBpm(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Every setting the three modules take, in real units, read off the
        parameters. One place rather than two: prepareToPlay and processBlock
        both need the whole set, and a control pushed from only one of them
        would be wrong until the next block. */
    void pushSettings (double bpm) noexcept;

    /** Reads the host's tempo off the playhead and caches it. ONLY safe from
        processBlock/prepareToPlay: JUCE documents getPlayHead() as callable only
        from the audio callback, and Ableton's playhead really is invalid outside
        it - reading it from the message thread segfaulted Live inside Peak
        Delay's formatKnobValue handler. */
    double readPlayHeadBpm();

    /** The last tempo readPlayHeadBpm() saw, clamped and defaulting to 120
        before the first block. Safe from any thread. */
    double currentBpm() const;

    /** Whether the Delay module's Time knobs read as note divisions. The
        "timeunit" parameter is the pill's own sense - true means free-running -
        so this is its inverse, named for what the knobs are doing. */
    bool delayIsSynced() const;

    ee::fx::ModulationModule modulation;
    ee::fx::DelayModule delay;
    ee::fx::ReverbModule reverb;

    static constexpr int kMaxChannels = 2;

    /** The host trims, and the global bypass. All three sit outside the three
        modules and inside nothing, so a bypassed plugin is unity whatever the
        trims say - see processBlock. */
    juce::SmoothedValue<float> inGain;
    juce::SmoothedValue<float> outGain;
    juce::SmoothedValue<float> engageGain;

    /** The untouched input, kept for the global bypass crossfade. */
    juce::AudioBuffer<float> dryBuffer;

    /** Written on the audio thread, read from the editor - see currentBpm(). */
    std::atomic<double> lastKnownBpm { 120.0 };

    double sr = 44100.0;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakAlpineProcessor)
};
