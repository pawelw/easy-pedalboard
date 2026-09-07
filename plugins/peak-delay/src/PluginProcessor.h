#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

#include "ee/dsp/Phaser.h"
#include "ee/dsp/TapeCharacter.h"
#include "ee/dsp/TapeDelay.h"
#include "ee/dsp/TapeTransport.h"
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
    std::atomic<float> inputLevelUi { 0.0f };
    std::atomic<int> strikeCountUi { 0 };

    /** The tape machine's current/default voicing, for the EE_TAPE_TUNER dev
        panel - same reason as above, the web editor needs a way to reach the
        tape without being a member of this class. Both placements are the same
        machine and are tuned together; either one can answer for the pair. */
    const ee::dsp::TapeTuning& tapeTuning() const noexcept { return tapeIn.tape.getTuning(); }
    void setTapeTuning (const ee::dsp::TapeTuning& t) noexcept
    {
        tapeIn.tape.setTuning (t);
        tapeOut.tape.setTuning (t);
    }

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

    /** Splits Wear and Flutter between the two tape placements - all to the
        pre section at 0, all to the post one at 1. */
    void updateTapeAmounts (float placement) noexcept;

    /** A delay time with the post section's latency already taken off it, so
        the first repeat lands where the Time knob says. */
    float trimmedDelaySeconds (float seconds) const noexcept;

    /** Fast-attack/slow-release peak follower on the pedal input, plus the
        onset test that drives strikeCountUi. Per block rather than per sample:
        the face redraws at 45 Hz and a block is a quarter of that. */
    void meterInput (const float* left, const float* right, int numSamples) noexcept;

    /** The Filter section, in place on the repeats. Two cuts either end of the
        band, the same pair Peak EQ carries in its top corner and built from the
        same juce::dsp coefficients, so the two pedals cut alike.

        On the delay's output rather than inside its feedback path, which is
        where a tone control on an echo often sits - because the compounding
        version of this knob already exists. Drift is a rolloff inside the loop
        that takes a little more top off every pass; this shapes the repeats
        once, and leaves the dry signal alone. The two do different jobs, and
        putting a second lowpass in the loop would only have blurred the first.

        Both ends bypass exactly at their resting positions, so a Filter section
        nobody has touched is not in the signal path at all. */
    void runFilter (float* left, float* right, int numSamples) noexcept;

    /** Recomputes the cut coefficients when either knob has actually moved.
        `force` rebuilds both, for the first block after prepare. */
    void updateFilters (bool force);

    /** The Mod section's insert half, in place. ee::dsp::Phaser on Peak Phase's
        own default voicing; its wet/dry is fixed at the setting where the
        notches are deepest (ee::dsp::phaser::kWetMix), so the knob blends the
        whole stage in from out here. At 0 the stage is skipped and the input is
        left bit exact.

        Runs on the repeats only, after the delay, its tape placement and the
        Filter section - the last stage of the wet path. It used to sit on the
        finished mix, which put the sweep on the note being played as well as
        on its echoes, and at Mix 0 the pedal was still audibly phasing a signal
        that had never been near the delay. */
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

    /** One tape machine in one place in the chain: the transport's wobble
        (Flutter), then the tape itself (Wear), in the order a machine has them.
        Both are the stages Peak Tape's knobs of the same name drive - shared
        engines, not second models of them, so the two pedals cannot drift
        apart. */
    struct TapeSection
    {
        ee::dsp::TapeTransport transport;
        ee::dsp::TapeCharacter tape;

        void prepare (double sampleRate)
        {
            transport.prepare (sampleRate);
            tape.prepare (sampleRate);
        }

        void reset() noexcept
        {
            transport.reset();
            tape.reset();
        }

        void setAmounts (float wear01, float flutter01) noexcept
        {
            transport.setFlutter01 (flutter01);
            tape.setAmount (wear01);
        }

        void process (float* left, float* right, int numSamples) noexcept
        {
            transport.process (left, right, numSamples);
            tape.process (left, right, numSamples);
        }

        int latencySamples() const noexcept { return transport.getLatencySamples() + tape.getLatencySamples(); }
    };

    /** Both of the Tape section's placements, wired in permanently: one in
        front of the delay, one on its output. Only ever one of them is turned
        up - the router crossfades Wear and Flutter from one to the other - and
        at zero both stages are bit-exact pass-through with a fixed latency, so
        the idle one costs a delay line and colours nothing.

        Two of them rather than one that moves, because a stage that moves has
        to be fed a different signal the instant it moves, and the six
        milliseconds of the *other* signal still inside its delay line come out
        as a splice. That was the click. Nothing here ever changes what it is
        fed, so there is nothing to splice: what the router changes is only how
        far each one is turned up, which is the same thing turning the Wear knob
        does. */
    TapeSection tapeIn;
    TapeSection tapeOut;

    ee::dsp::Phaser phaser;

    static constexpr int kMaxChannels = 2;

    /** The Filter section's two cuts, one pair per channel. `*Active` is the
        bypass: at the resting end of its travel a cut is not run at all, so it
        cannot colour a signal it is not cutting. */
    std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> hiPass;
    std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> loPass;
    float loCutHz = kLoCutMinHz;
    float hiCutHz = kHiCutMaxHz;
    bool hiPassActive = false;
    bool loPassActive = false;

    ee::dsp::TapeDelay delay;

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

    /** Where the Tape section is, as a number rather than a switch: 0 is
        entirely in front of the delay, 1 entirely on its repeats, and the
        router glides between them over kPlacementSeconds. Read once per block -
        the two sections' own parameter smoothing carries it the rest of the
        way, exactly as it does for a hand on the Wear knob. */
    juce::SmoothedValue<float> tapePlacement;

    /** What the post section's latency costs the repeats, taken back off the
        delay's own time so the gap between the dry signal and its first repeat
        is what the Time knob says in either placement. */
    float postLatencySeconds = 0.0f;

    /** This block's stage settings, read off the parameters once at the top of
        processBlock so the per-chunk code does not have to reach for them. */
    float phaserBlend = 0.0f;

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
