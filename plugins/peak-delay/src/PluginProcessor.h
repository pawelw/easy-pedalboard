#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/dsp/ModDelayLine.h"
#include "ee/dsp/Phaser.h"
#include "ee/dsp/TapeCharacter.h"
#include "ee/dsp/TapeDelay.h"
#include "ee/dsp/TapeTransport.h"

#include <array>

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
    std::atomic<float> inputLevelUi { 0.0f };
    std::atomic<int> strikeCountUi { 0 };

    /** The tape machine's current/default voicing, for the EE_TAPE_TUNER dev
        panel - same reason as above, the web editor needs a way to reach
        `tape` without being a member of this class. */
    const ee::dsp::TapeTuning& tapeTuning() const noexcept { return tape.getTuning(); }
    void setTapeTuning (const ee::dsp::TapeTuning& t) noexcept { tape.setTuning (t); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Where the Tape section sits relative to the delay line. Read once per
        block into tapePost below rather than per pass: a router flipped between
        the pre pass and the post one would otherwise run the stage twice, or
        not at all, in that block.

        Only Tape has a router. Of the Mod section's two knobs, Drift is inside
        the delay's own feedback loop - it is not before or after the delay, it
        is part of it - so a Pre/Post control there would have governed only
        half its own section. The Phaser is fixed after the delay instead. */
    bool tapeIsPost() const noexcept;

    /** The Tape section, in place: the transport's wobble, then the tape. */
    void runTape (float* left, float* right, int numSamples) noexcept;

    /** Holds the dry path back by exactly what the two tape stages cost, and
        keeps its line fed whether or not it is being applied.

        Post means the tape is on the repeats, so in that mode the dry signal
        does not pass through the stages and would otherwise come out ~6 ms
        ahead of the latency this plugin reports. `apply` is the router: false
        still writes the input into the line, so flipping to Post mid-note
        reads real audio rather than six milliseconds of silence. */
    void alignDry (float* left, float* right, int numSamples, bool apply) noexcept;

    /** Fast-attack/slow-release peak follower on the pedal input, plus the
        onset test that drives strikeCountUi. Per block rather than per sample:
        the face redraws at 45 Hz and a block is a quarter of that. */
    void meterInput (const float* left, const float* right, int numSamples) noexcept;

    /** The Mod section's insert half, in place. ee::dsp::Phaser on Peak Phase's
        own default voicing; its wet/dry is fixed at the setting where the
        notches are deepest (ee::dsp::phaser::kWetMix), so the knob blends the
        whole stage in from out here. At 0 the stage is skipped and the input is
        left bit exact. */
    void runPhaser (float* left, float* right, int numSamples) noexcept;

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void mirrorTime (const juce::String& from, const juce::String& to);

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

    /** The Tape section, in the order a machine has them: the transport's
        wobble (Flutter), then the tape itself (Wear). Both are the stages Peak
        Tape's knobs of the same name drive - shared engines, not second models
        of them, so the two pedals cannot drift apart. */
    ee::dsp::TapeTransport flutterStage;
    ee::dsp::TapeCharacter tape;

    ee::dsp::Phaser phaser;

    ee::dsp::TapeDelay delay;

    std::atomic<float>* leftTimeParam = nullptr;
    std::atomic<float>* rightTimeParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* timeUnitParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* driftParam = nullptr;
    std::atomic<float>* phaserParam = nullptr;
    std::atomic<float>* wearParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* tapePreParam = nullptr;
    std::atomic<float>* inGainParam = nullptr;
    std::atomic<float>* outGainParam = nullptr;
    std::atomic<float>* onParam = nullptr;

    /** This block's stage settings, read off the parameters once at the top of
        processBlock so the per-chunk code does not have to reach for them -
        and, for the placement, so a block cannot see the router in two states. */
    float phaserBlend = 0.0f;
    bool tapePost = false;

    /** Stops the two time parameters echoing each other forever. */
    std::atomic<bool> mirroring { false };

    /** Written on the audio thread, read from the editor - see currentBpm(). */
    std::atomic<double> lastKnownBpm { 120.0 };

    /** The prepared sample rate. meterInput()'s follower is specified in
        seconds, so it needs this to turn a block length into a time. */
    double sr = 44100.0;

    juce::SmoothedValue<float> dryGain;
    juce::SmoothedValue<float> wetGain;

    /** The header's two faders. Input trims what the pedal is fed - dry path
        and delay input alike, so it moves the whole pedal rather than just the
        repeats - and Output rides the finished signal. Both sit inside the
        bypass crossfade, so a bypassed pedal is unity whatever they say. */
    juce::SmoothedValue<float> inGain;
    juce::SmoothedValue<float> outGain;

    /** 1 while the pedal is engaged, 0 when bypassed. Fades the tape off the
        dry path and closes the delay input, leaving the repeats to ring out. */
    juce::SmoothedValue<float> engageGain;

    /** The stage chain's working buffers. `stageBuffer` carries the signal
        through a side's stages so the untouched version survives for the
        bypass crossfade; `mixBuffer` holds dry+wet before the post side runs;
        `modBuffer` is the Phaser's output, blended back by its knob. */
    juce::AudioBuffer<float> stageBuffer;
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> mixBuffer;
    juce::AudioBuffer<float> modBuffer;

    /** The dry path's latency match for the Post router - see alignDry(). */
    std::array<ee::dsp::ModDelayLine, 2> dryAlign;
    float dryAlignSamples = 0.0f;

    /** meterInput()'s audio-thread state: the smoothed peak inputLevelUi is
        mapped from, a slower average an onset has to stand out against, and
        how many samples are left of the refractory period that stops one note
        being counted as several. */
    float peakLevelSmoothed = 0.0f;
    float onsetFloor = 0.0f;
    int onsetHoldSamples = 0;

    /** engageGain sampled once per chunk. The pre and the post crossfade both
        need the same ramp, and a SmoothedValue can only be walked once. */
    std::vector<float> engageRamp;

    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakDelayProcessor)
};
