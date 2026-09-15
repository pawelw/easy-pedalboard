#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/GrainerConfig.h"
#include "ee/dsp/GrainSyncMap.h"
#include "ee/dsp/Grainer.h"
#include "ee/dsp/TapeDelay.h"
#include "ee/plugin/PresetStore.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

#if EE_GRAIN_TRACE
#include "GrainTrace.h"
#endif

class PeakGrainProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    PeakGrainProcessor();
    ~PeakGrainProcessor() override;

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

    /** Both preset banks. Public because the web editor's bridge takes a
        reference to it - see ee/plugin/PresetBridge.h. Declared after apvts so
        the tree it reads and writes is already built. */
    ee::plugin::PresetStore presets { apvts, "Peak Grain", EE_FACTORY_PRESETS };

    /** Text under Size/Density/Window/the two Delay time knobs: the division
        label when synced, or the free-running reading otherwise. Public -
        unlike the old ee::ui editor, PeakGrainWebEditor isn't a member of
        this class and can't reach the private *Param pointers or *Map
        members directly, the same reason Peak Delay's equivalents are public
        (plugins/peak-delay/src/PluginProcessor.h). */
    juce::String sizeReadout() const;
    juce::String densityReadout() const;
    juce::String windowReadout() const;
    juce::String leftTimeReadout() const;
    juce::String rightTimeReadout() const;

    /** The host tempo the readouts above were worked out at, for the web
        editor's timer feed - a synced knob is a note division, so its printed
        value moves whenever the host's tempo does, with nothing on the face
        changing and no parameter to listen to. Safe from any thread; see
        currentBpm()'s own note. */
    double hostBpm() const { return lastKnownBpm.load(); }

    /** The grain engine's current/default voicing, for the EE_GRAIN_TUNER dev
        panel - same reason as the readouts above, PeakGrainWebEditor isn't a
        member of this class and needs a way to reach the engine. Unconditional
        (not guarded by EE_GRAIN_TUNER) the same way Peak Delay's
        tapeTuning()/setTapeTuning() are - cheap to keep around, and it's the
        editor's own #if that decides whether anything calls them. */
    const ee::dsp::GrainerTuning& tuning() const noexcept { return grainer.getTuning(); }
    void setTuning (const ee::dsp::GrainerTuning& t) noexcept { grainer.setTuning (t); }

private:
    static constexpr int kMaxChannels = 2;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** The last tempo readPlayHeadBpm() saw, clamped to 20..300 and defaulting
        to 120 before the first block. Safe from any thread - this is what the
        four readouts above and hostBpm() use. */
    double currentBpm() const { return lastKnownBpm.load(); }

    /** Reads the host's tempo off the playhead and caches it in lastKnownBpm.
        ONLY safe from processBlock/prepareToPlay: JUCE documents getPlayHead()
        as callable only from the audio callback, and Ableton's playhead really
        is invalid outside it - reading it from the message thread segfaulted
        Live inside Peak Delay's own editor once (see
        PeakDelayProcessor::readPlayHeadBpm()'s note). */
    double readPlayHeadBpm();

    /** The Delay Type pill's three positions, read off dtype's choice index.
        Out of range falls back to Normal. */
    ee::dsp::TapeDelay::Routing routing() const noexcept;

    /** ssync/dsync/wsync flipped: stash the knob's current position into the
        mode it is leaving and push the mode it is entering back onto the
        parameter, so each mode remembers where it was left. Mirrors
        PeakTremPanProcessor::onSyncToggled. Called from parameterChanged
        below - the face's single Grain "SYNC" pill writes all three (see
        GrainFace.jsx), and each write triggers its own knob's remap
        independently, so host automation of any one flag alone remaps
        correctly too, not only a click on the pill. The delay's own
        single-knob version of this (onDelaySyncToggled) is gone now that
        Left/Right are two independent knobs with no remembered per-mode
        position, matching Peak Delay. */
    void onSizeSyncToggled();
    void onDensitySyncToggled();
    void onWindowSyncToggled();

    void syncToggled (const char* paramID,
                      std::atomic<float>& freeSlot,
                      std::atomic<float>& syncSlot,
                      const std::atomic<float>* syncFlag);

    /** One listener for every parameter that reacts to its own change rather
        than just being read each block: ssync/dsync (above) and ltime/rtime/
        dlink - PeakDelayProcessor's Sync-L/R mirror, copied onto Grain's ids
        (PluginProcessor.cpp:293-357 there). `mirroring` stops the two Time
        parameters echoing each other forever; `installingState` stands every
        case here down for the length of a whole-tree install. */
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void mirrorTime (const juce::String& from, const juce::String& to);

    /** Every route a whole APVTS tree can arrive by goes through this rather
        than calling apvts.replaceState directly - a host restoring a session,
        and the preset store, which is handed this as its install hook. */
    void installState (const juce::ValueTree& tree);

    ee::dsp::Grainer grainer;
    ee::dsp::TapeDelay delay;
    ee::dsp::FdnReverb reverb;

    // 0..1 knobs whose Sync switch reinterprets them; built from GrainerConfig.
    ee::dsp::GrainSyncMap sizeMap;
    ee::dsp::GrainSyncMap densityMap;
    ee::dsp::GrainSyncMap windowMap;
    ee::dsp::GrainSyncMap delayMap;

    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* sizeSyncParam = nullptr;
    std::atomic<float>* densitySyncParam = nullptr;
    std::atomic<float>* windowParam = nullptr;
    std::atomic<float>* windowSyncParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* stretchParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* scatterParam = nullptr;
    std::atomic<float>* reverseParam = nullptr;
    std::atomic<float>* stereoParam = nullptr;
    std::atomic<float>* modParam = nullptr;
    std::atomic<float>* bitParam = nullptr;
    std::atomic<float>* detuneParam = nullptr;
    std::atomic<float>* pitchLowParam = nullptr;
    std::atomic<float>* pitchUnisonParam = nullptr;
    std::atomic<float>* pitchHighParam = nullptr;
    std::atomic<float>* leftTimeParam = nullptr;
    std::atomic<float>* rightTimeParam = nullptr;
    std::atomic<float>* delayLinkParam = nullptr;
    std::atomic<float>* delaySyncParam = nullptr; // dtsync: ms vs tempo-division display, both time knobs
    std::atomic<float>* delayTypeParam = nullptr;
    std::atomic<float>* delayFeedbackParam = nullptr;
    std::atomic<float>* delayMixParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* reverbLoCutParam = nullptr;
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
    std::atomic<float>* levelParam = nullptr;

    juce::SmoothedValue<float> outputGain;

    // Remembered knob positions for the mode each Sync switch is not currently
    // in, so a round trip through the switch lands back where it started.
    // Persisted as state-tree properties (see get/setStateInformation). Only
    // Size/Density/Window have this now - the delay's own version went with
    // the single dtime knob it belonged to.
    std::atomic<float> sizeFree01 { ee::dsp::config::kDefaultSize01 };
    std::atomic<float> sizeSync01 { ee::dsp::config::kDefaultSize01 };
    std::atomic<float> densityFree01 { ee::dsp::config::kDefaultDensity01 };
    std::atomic<float> densitySync01 { ee::dsp::config::kDefaultDensity01 };
    std::atomic<float> windowFree01 { ee::dsp::config::kDefaultWindow01 };
    std::atomic<float> windowSync01 { ee::dsp::config::kDefaultWindow01 };

    /** Stops ltime/rtime echoing each other forever while dlink is on. */
    std::atomic<bool> mirroring { false };

    /** Held while installState is putting a whole tree in place, so the L/R
        mirror stands down for the length of it. See installState. */
    std::atomic<bool> installingState { false };

    /** Written on the audio thread, read from the editor - see hostBpm(). */
    std::atomic<double> lastKnownBpm { 120.0 };

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
