#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/GrainerConfig.h"
#include "ee/dsp/GrainSyncMap.h"
#include "ee/dsp/Grainer.h"
#include "ee/dsp/TapeDelay.h"

#include "GrainPresets.h"

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

    /** Host tempo, clamped to something usable, for the three tempo-synced knobs
        (grain Size, grain Density, delay Time). 120 when the host reports none. */
    double currentBpm() const;

    juce::String sizeReadout() const;
    juce::String densityReadout() const;
    juce::String delayTimeReadout() const;

    /** A Sync button was clicked on the face: stash the knob's current position
        into the mode it is leaving and push the mode it is entering back onto
        the parameter, so each mode remembers where it was left. Mirrors
        PeakTremPanProcessor::onSyncToggled. UI thread only - no listener. */
    void onSizeSyncToggled();
    void onDensitySyncToggled();
    void onDelaySyncToggled();

    void syncToggled (const char* paramID,
                      std::atomic<float>& freeSlot,
                      std::atomic<float>& syncSlot,
                      const std::atomic<float>* syncFlag);

    ee::dsp::Grainer grainer;
    ee::dsp::TapeDelay delay;
    ee::dsp::FdnReverb reverb;

    // File-backed preset store, driven by the face's preset bar.
    ee::grain::PresetStore presets { apvts };

    // 0..1 knobs whose Sync switch reinterprets them; built from GrainerConfig.
    ee::dsp::GrainSyncMap sizeMap;
    ee::dsp::GrainSyncMap densityMap;
    ee::dsp::GrainSyncMap delayMap;

    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* sizeSyncParam = nullptr;
    std::atomic<float>* densitySyncParam = nullptr;
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
    std::atomic<float>* delayTimeParam = nullptr;
    std::atomic<float>* delaySyncParam = nullptr;
    std::atomic<float>* delayFeedbackParam = nullptr;
    std::atomic<float>* delayMixParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* reverbMixParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    // One enable switch per face module: off forces that section's controls to
    // their no-op values in processBlock, leaving the knobs where they are.
    std::atomic<float>* grainOnParam = nullptr;
    std::atomic<float>* pitchOnParam = nullptr;
    std::atomic<float>* randomOnParam = nullptr;
    std::atomic<float>* delayOnParam = nullptr;
    std::atomic<float>* reverbOnParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;

    juce::SmoothedValue<float> outputGain;

    // Remembered knob positions for the mode each Sync switch is not currently
    // in, so a round trip through the switch lands back where it started.
    // Persisted as state-tree properties (see get/setStateInformation).
    std::atomic<float> sizeFree01 { ee::dsp::config::kDefaultSize01 };
    std::atomic<float> sizeSync01 { ee::dsp::config::kDefaultSize01 };
    std::atomic<float> densityFree01 { ee::dsp::config::kDefaultDensity01 };
    std::atomic<float> densitySync01 { ee::dsp::config::kDefaultDensity01 };
    std::atomic<float> delayFree01 { ee::dsp::config::kDefaultDelayTime01 };
    std::atomic<float> delaySync01 { ee::dsp::config::kDefaultDelayTime01 };

    // Each series stage is an equal-power dry/wet blend; the dry leg opens to
    // unity when the pedal is bypassed so all three tails ring out over the
    // untouched input. `engageGain` gates the three sends (grain record, delay
    // send, reverb send) to zero on bypass.
    juce::SmoothedValue<float> grainDry;
    juce::SmoothedValue<float> grainWet;
    juce::SmoothedValue<float> delayDry;
    juce::SmoothedValue<float> delayWet;
    juce::SmoothedValue<float> reverbDry;
    juce::SmoothedValue<float> reverbWet;
    juce::SmoothedValue<float> engageGain;

    // Scratch, sized once in prepareToPlay: the grain cloud, the grain-stage
    // blend fed on to the delay, a gated copy of it for the delay's input (the
    // delay line reads before it writes, so it cannot run in place), the delay's
    // own return, the mono sum sent to the reverb, and the reverb's stereo
    // return.
    juce::AudioBuffer<float> grainBuffer;
    juce::AudioBuffer<float> stageBuffer;
    juce::AudioBuffer<float> delayInBuffer;
    juce::AudioBuffer<float> delayWetBuffer;
    juce::AudioBuffer<float> monoBuffer;
    juce::AudioBuffer<float> verbBuffer;

    // One engage-ramp value per sample, filled once at the top of each chunk so
    // the three send gates below all read the same figure for a given sample.
    juce::AudioBuffer<float> engageBuffer;
    int maxBlock = 512;
    bool snapDelayNextBlock = true;

#if EE_GRAIN_TRACE
    std::unique_ptr<GrainTrace> trace;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakGrainProcessor)
};
