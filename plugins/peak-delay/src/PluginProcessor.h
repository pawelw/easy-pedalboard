#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

#include "ee/dsp/TapeDelay.h"
#include "ee/fx/DelayModule.h"
#include "ee/plugin/InputMeter.h"
#include "ee/plugin/PresetStore.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

class PeakDelayProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    PeakDelayProcessor();
    ~PeakDelayProcessor() override;

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

    /** The pedal's preset banks: the one compiled in from plugins/peak-delay/
        presets/, and the user's own on disk. Public because the editor's
        bridge takes a reference to it - see ee/plugin/PresetBridge.h, which is
        the whole of what a face needs to browse and save. Declared after apvts
        so the tree it reads and writes is already built. */
    ee::plugin::PresetStore presets { apvts, "Peak Delay", EE_FACTORY_PRESETS };

    /** The Filter section's travel, shared with the readouts and with Peak EQ,
        whose Low Cut / High Cut these are the same two filters as. Public
        because the parameter text functions live outside the class. */
    static constexpr float kLoCutMinHz = 20.0f;
    static constexpr float kLoCutMaxHz = 1200.0f;
    static constexpr float kHiCutMinHz = 1200.0f;
    static constexpr float kHiCutMaxHz = 20000.0f;

    /** Text under a Time knob: the division label ("1/8") when synced, or the
        free-running time ("333 ms", "1.50 s") when the ms button is on.
        Re-derives the text from the knob position rather than reading it off
        the parameter, so it is correct however this is called. */
    juce::String timeReadout (const std::atomic<float>* timeParam) const;

    /** The millisecond reading regardless of the ms toggle - the onyx face's
        Readout shows this as a small, constant figure alongside timeReadout's
        toggle-aware one (which swaps to the very same reading when ms is on),
        rather than only being able to see the ms conversion by turning ms on
        and losing the division label. */
    juce::String timeMsReadout (const std::atomic<float>* timeParam) const;

    /** Milliseconds for one Time knob, in whichever mode the pill is in -
        what both the ms readout and the scope are derived from. */
    float timeMs (const std::atomic<float>* timeParam) const;

    /** Thin wrappers over timeReadout()/timeMsReadout() for the web editor,
        which - unlike the old ee::ui editor - isn't a member of this class
        and so can't reach leftTimeParam/rightTimeParam directly. */
    juce::String leftTimeReadout() const { return timeReadout (leftTimeParam); }
    juce::String rightTimeReadout() const { return timeReadout (rightTimeParam); }
    juce::String leftTimeMsReadout() const { return timeMsReadout (leftTimeParam); }
    juce::String rightTimeMsReadout() const { return timeMsReadout (rightTimeParam); }

    /** The host tempo the readouts above were worked out at, for the web
        editor's timer feed. A synced Time knob is a note division, so the
        millisecond figure beside it moves whenever the host's tempo does -
        with nothing on the face changing and no parameter to listen to. The
        page watches this number instead and re-reads the readouts when it
        moves. Same wrapper reason as the four above: currentBpm() is private
        and the editor is not a member. */
    double hostBpm() const { return currentBpm(); }

    /** Both Time knobs as plain numbers of milliseconds, for the TapScope.
        The scope places its taps on a real time axis, which it cannot get
        from the normalised knob value alone: what a position means depends
        on the Sync pill and, when synced, on the host tempo. The readouts
        above carry the same figure, but as display text ("1/8", "1.50 s") -
        parsing that back into a number would be a formatter dependency in
        the wrong direction. */
    float leftTimeMs() const { return timeMs (leftTimeParam); }
    float rightTimeMs() const { return timeMs (rightTimeParam); }

    /** Live input level (0 = silent, 1 = 0 dBFS, mapped from -40..0 dB) and a
        monotonic count of note onsets, both for the face's TapScope. The scope
        animates nothing at all until something is played, and each strike
        starts one playhead sweeping its time axis - so it needs to know when a
        note started, not only how loud the input is. Detected here rather than
        in the web view because the level feed the editor pushes is a 45 Hz
        sample of something that happens in milliseconds. Written from the
        audio thread. */
    ee::plugin::InputMeter inputMeter;

    /** The tape machine's current/default voicing, for the EE_TAPE_TUNER dev
        panel - same reason as above, the web editor needs a way to reach the
        tape without being a member of this class. Both placements are the same
        machine and are tuned together; either one can answer for the pair. */
    const ee::dsp::TapeTuning& tapeTuning() const noexcept { return chain.tapeTuning(); }
    void setTapeTuning (const ee::dsp::TapeTuning& t) noexcept { chain.setTapeTuning (t); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** The Delay Type button's three positions, read off kTypeID's choice
        index. Out of range means the state came from somewhere odd, so it
        falls back to the mode the pedal has always had. */
    ee::dsp::TapeDelay::Routing routing() const noexcept;

    /** Where the Tape section sits relative to the delay line - the target
        tapePlacement glides towards, not a decision the audio path branches on.

        Only Tape has a router. Of the Mod section's two knobs, Drift is inside
        the delay's own feedback loop - it is not before or after the delay, it
        is part of it - so a Pre/Post control there would have governed only
        half its own section. The Phaser is fixed on the repeats instead. */
    bool tapeIsPost() const noexcept;

    /** Every setting the chain takes, read off the parameters in real units.
        Shared by prepareToPlay and processBlock, which both need the whole set. */
    void pushSettings (double bpm, bool synced) noexcept;

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void mirrorTime (const juce::String& from, const juce::String& to);

    /** Every route a whole APVTS tree can arrive by goes through this rather
        than calling apvts.replaceState directly - a host restoring a session,
        and the preset store, which is handed this as its install hook. */
    void installState (const juce::ValueTree& tree);

    /** Reads the host's tempo off the playhead and caches it. ONLY safe from
        processBlock/prepareToPlay: JUCE documents getPlayHead() as callable
        only from the audio callback, and Ableton's playhead really is invalid
        outside it - reading it from the message thread segfaulted Live inside
        the editor's formatKnobValue handler. */
    double readPlayHeadBpm();

    /** The last tempo readPlayHeadBpm() saw, clamped to 20..300 and defaulting
        to 120 before the first block. Safe from any thread - this is what the
        readouts and the scope's time axis use. */
    double currentBpm() const;

    /** Whether the Time knobs read as note divisions. The "ms" parameter is
        the pill's own sense - true means free-running - so this is its
        inverse, named for what the knobs are actually doing. */
    bool isSynced() const;

    /** The pedal's whole chain: the delay line, a tape machine either side of
        it, the filter pair and the phaser on the repeats, the dry/wet law and
        the trims. Peak Alpine's Delay module is a second instance of this, which
        is why none of it is written out here any more - a fix lands in both. */
    ee::fx::DelayModule chain;

    std::atomic<float>* leftTimeParam = nullptr;
    std::atomic<float>* rightTimeParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* typeParam = nullptr;
    std::atomic<float>* timeUnitParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* driftParam = nullptr;
    std::atomic<float>* phaserParam = nullptr;
    std::atomic<float>* wearParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* loCutParam = nullptr;
    std::atomic<float>* hiCutParam = nullptr;
    std::atomic<float>* tapePreParam = nullptr;
    std::atomic<float>* inGainParam = nullptr;
    std::atomic<float>* outGainParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    /** Stops the two time parameters echoing each other forever. */
    std::atomic<bool> mirroring { false };

    /** Held while installState is putting a whole tree in place, so the L/R
        mirror stands down for the length of it. See installState. */
    std::atomic<bool> installingState { false };

    /** Written on the audio thread, read from the editor - see currentBpm(). */
    std::atomic<double> lastKnownBpm { 120.0 };

    /** The prepared sample rate, for the readouts' own use. */
    double sr = 44100.0;

    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakDelayProcessor)
};
