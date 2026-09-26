#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

// The output-safety watchdog is a dev-only flight recorder (see
// plugins/bitbit-alpine/CMakeLists.txt). The safety net it sits behind -
// sanitizeOutput - ships in every build; only the recorder and its panel are
// gated on this.
#ifndef EE_ALPINE_WATCHDOG
#define EE_ALPINE_WATCHDOG 0
#endif

#if EE_ALPINE_WATCHDOG
#include "AlpineWatchdog.h"
#endif

#include "ee/fx/ArtifactModule.h"
#include "ee/fx/DelayModule.h"
#include "ee/fx/ModulationControls.h"
#include "ee/fx/ModulationModule.h"
#include "ee/dsp/Equaliser.h"
#include "ee/dsp/SpectrumAnalyser.h"
#include "ee/dsp/Tuner.h"
#include "ee/fx/ReverbModule.h"
#include "ee/plugin/InputMeter.h"
#include "ee/plugin/PresetStore.h"

#include "GlobalEq.h"

#if EE_HAS_FACTORY_PRESETS
#include EE_FACTORY_PRESETS_HEADER
#endif

/**
 * BitBit Alpine: four effect modules under one chrome.
 *
 * Artifact (Ring Mod / Bit Crush / Rust / Drive / Comp - `ee::fx::ArtifactModule`,
 * which is what BitBit Artifact runs), then Modulation (Tape / Tremolo / Chorus /
 * Phaser / Filter), then Delay - which is BitBit Delay's entire chain, the same
 * `ee::fx::DelayModule` that pedal runs - then Reverb (Spring / Shimmer / Studio /
 * Simple). Every engine in here is the engine its own pedal uses, so nothing can
 * drift from the pedal it came from and a fix lands in both.
 *
 * Which runs first, second, third, fourth is `chain.order`, a Lehmer-coded
 * index into the 24 permutations of the four (see ChainOrder.h). Index 0 - its
 * default - is this order, Artifact then Modulation then Delay then Reverb,
 * so a session or preset that predates this parameter plays back unchanged.
 *
 * This class is parameters and plumbing. It owns no DSP of its own: what it
 * does is read the knobs, turn them into the real units the four modules take,
 * and wrap the lot in a global bypass.
 */
class BitBitAlpineProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    BitBitAlpineProcessor();
    ~BitBitAlpineProcessor() override;

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
    ee::plugin::PresetStore presets { apvts, "BitBit Alpine", EE_FACTORY_PRESETS };

    /** The pre-EQ, kept as one machine-wide setting rather than per session or
        preset - see GlobalEq. Attached in the constructor for a real plugin
        wrapper only; public so a test can attach it to a file of its own. */
    GlobalEq globalEq { apvts };

    /** The Delay module's TapScope feed - the same two numbers BitBit Delay's
        face reads, from a second instance of the same meter. */
    ee::plugin::InputMeter inputMeter;

    /** The Filter section's travel, shared with the readouts and with BitBit EQ,
        whose Low Cut / High Cut these are the same two filters as. */
    static constexpr float kLoCutMinHz = 20.0f;
    static constexpr float kLoCutMaxHz = 1200.0f;
    static constexpr float kHiCutMinHz = 1200.0f;
    static constexpr float kHiCutMaxHz = 20000.0f;

    /** The Delay module's readouts, for the face. Same three the BitBit Delay
        processor answers, and for the same reason: what a Time knob position
        means depends on the Sync pill and, when synced, on the host tempo,
        none of which the web view knows. */
    juce::String timeReadout (const char* parameterId) const;
    juce::String timeMsReadout (const char* parameterId) const;
    float timeMs (const char* parameterId) const;

    /** The Modulation module's Filter Time knob: the LFO period in ms when
        free, the note value when synced. The web view has no Sync pill or host
        tempo, so the processor answers this. */
    juce::String filterTimeReadout() const;

    /** The Modulation module's Tremolo Rate knob: the LFO period in ms when
        free, the note value when synced. The processor answers it for the same
        reason it answers the Filter one - no Sync switch or host tempo in the
        web view. */
    juce::String tremoloRateReadout() const;

    /** The Modulation module's Filter engine live cutoff-sweep exponent per
        channel (Range * gate * lfo), for the face's response scope. Written
        from the audio thread, read by the editor's Timer as one "filterMod"
        event - the same feed BitBit Wah pushes. */
    std::atomic<float> filterModL { 0.0f };
    std::atomic<float> filterModR { 0.0f };

    /** The header's tuner. Captures the untouched input - before the Input
        trim, like inputMeter - but only while the editor has the tuner open;
        closed, it does nothing, so the audio is exactly what it was without it.
        The editor owns the analysis half (ee::dsp::TunerAnalyser). */
    ee::dsp::TunerCapture tuner;

    /** The pre-EQ dialog's spectrum: what the chain is fed, after the EQ.
        Captures only while the editor has the dialog open - see
        ee::dsp::SpectrumCapture. */
    ee::dsp::SpectrumCapture eqSpectrum;

    /** The Artifact module, read-only, for the editor's Comp meter feed. */
    const ee::fx::ArtifactModule& artifactModule() const noexcept { return artifact; }

    /** The tuner's mute button. Silences the output, ramped, only while the
        tuner is open - so closing the tuner, or the editor with it, can never
        leave the plugin silent. Not a parameter: nothing a session or preset
        should remember. */
    std::atomic<bool> tunerMute { false };

    double hostBpm() const { return currentBpm(); }

    /** What sanitizeOutput did to one block, handed back to the caller (and,
        in a watchdog build, on to the flight recorder). */
    struct SafetyVerdict
    {
        bool nonFinite = false;
        bool clamped = false;
        bool didReset = false;
        bool firstBadBlock = false;
        float peak = 0.0f;
    };

    /** The output-safety net's running tally. Public so the editor can show it
        and a future test can assert on it: how many blocks it has had to
        sanitise, and how many times a sustained run of them forced a reset of
        the four modules. */
    std::atomic<int> safetyTrips { 0 };
    std::atomic<int> safetyResets { 0 };
    std::atomic<float> lastOutputPeak { 0.0f };

#if EE_ALPINE_WATCHDOG
    /** Dev-only: the per-block black box the watchdog panel reads. Public
        because the editor polls it - see BitBitAlpineWebEditor. */
    AlpineWatchdog watchdog;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Every setting the four modules take, in real units, read off the
        parameters. One place rather than two: prepareToPlay and processBlock
        both need the whole set, and a control pushed from only one of them
        would be wrong until the next block. */
    void pushSettings (double bpm) noexcept;

    /** The last line of defence, run on the finished output every block in
        every build. Replaces any non-finite sample with silence and brickwalls
        anything past +/-kSafetyCeiling; if the output stays bad for
        safetyResetHoldBlocks in a row - which a NaN latched in a feedback line
        does forever - it resets the four modules and clears the block. It
        cannot catch the first bad block before it is heard, but it stops the
        sustained roar that follows. */
    SafetyVerdict sanitizeOutput (juce::AudioBuffer<float>& buffer, int numCh, int numSamples) noexcept;

    /** The four modules, in place, in activeChainOrder. `dip`, when there is
        one, is a per-sample gain put on every module's input and on the chain's
        output - see runChainWithReorderFade. */
    void runChain (juce::AudioBuffer<float>& block, int numCh, int numSamples, const float* dip) noexcept;

    /** runChain, with a change of chain.order faded rather than stepped. The
        modules carry one set of state each, so two orders cannot run side by
        side and be crossfaded; instead the chain dips to silence over
        kChainFadeSeconds, the order swaps at the bottom, and it comes back up.
        Swapping on the spot put a step in the output as big as the difference
        between the two orders - a click on every drag of a module, and on every
        change of an automated chain.order. The global bypass's dry path is not
        in the dip.

        The dip is on every module's input, not only the output: a module's
        input changes source at the swap too, and a delay line records that
        step and plays it back later - the chorus ~10 ms on, after an
        output-only dip had already come back up. */
    void runChainWithReorderFade (juce::AudioBuffer<float>& buffer, int numCh, int numSamples) noexcept;

    bool tunerMuted() const noexcept { return tuner.isActive() && tunerMute.load (std::memory_order_relaxed); }

    /** Follows a person turning one of the Delay module's two Time knobs onto
        the other while its "Sync L/R" button is on - the same mirror BitBit Delay
        runs, ported here because the Delay module is only DSP and carries none
        of this. Registered as an APVTS listener for the two times and the
        button. */
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void mirrorTime (const juce::String& from, const juce::String& to);

    /** Every route a whole APVTS tree can arrive by goes through this rather
        than calling apvts.replaceState directly - a host restoring a session,
        and the preset store, which is handed this as its install hook - so the
        L/R mirror can stand down for the length of the install. */
    void installState (const juce::ValueTree& tree);

    /** Reads the host's tempo off the playhead and caches it. ONLY safe from
        processBlock/prepareToPlay: JUCE documents getPlayHead() as callable only
        from the audio callback, and Ableton's playhead really is invalid outside
        it - reading it from the message thread segfaulted Live inside BitBit
        Delay's formatKnobValue handler. */
    double readPlayHeadBpm();

    /** The last tempo readPlayHeadBpm() saw, clamped and defaulting to 120
        before the first block. Safe from any thread. */
    double currentBpm() const;

    /** Whether the Delay module's Time knobs read as note divisions. The
        "timeunit" parameter is the pill's own sense - true means free-running -
        so this is its inverse, named for what the knobs are doing. */
    bool delayIsSynced() const;

    /** Decodes the embedded tape floor once and hands it to the Modulation
        module's tape engine, so its Noise knob plays the recording BitBit Tape
        does rather than the synthesised hiss fallback. The buffer is a member:
        the engine loops from these samples and the pointers must outlive it. */
    void loadTapeNoiseSample();

    juce::AudioBuffer<float> tapeNoiseSample;
    std::vector<const float*> tapeNoiseChannels;
    double tapeNoiseSampleRate = 44100.0;

    /** The header's pre-EQ - see Params.h. Bands 0..kEqSimpleBands-1 are the
        Simple face's, the rest the Advanced face's eight. */
    static constexpr int kEqSimpleBands = 3;
    ee::dsp::Equaliser equaliser;
    static_assert (kEqSimpleBands + ee::dsp::eq::kAdvancedBands <= ee::dsp::Equaliser::kMaxBands);

    struct EqBandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
    };
    std::array<EqBandParams, ee::dsp::eq::kAdvancedBands> eqBandParams {};

    ee::fx::ArtifactModule artifact;
    ee::fx::ModulationModule modulation;
    ee::fx::DelayModule delay;
    ee::fx::ReverbModule reverb;

    /** Keeps the Modulation module's Filter and Tremolo LFOs on the host grid
        while they are synced - the same object BitBit Modulation runs. */
    ee::fx::modulation::HostSync modSync;

    /** Stops the two Delay-module time parameters echoing each other forever. */
    std::atomic<bool> mirroring { false };

    /** Held while installState is putting a whole tree in place, so the L/R
        mirror stands down for the length of it. See installState. */
    std::atomic<bool> installingState { false };

    static constexpr int kMaxChannels = 2;

    /** The host trims, and the global bypass. All three sit outside the four
        modules and inside nothing, so a bypassed plugin is unity whatever the
        trims say - see processBlock. */
    juce::SmoothedValue<float> inGain;
    juce::SmoothedValue<float> outGain;
    juce::SmoothedValue<float> engageGain;
    juce::SmoothedValue<float> tunerMuteGain; // 1 = sounding; see tunerMute

    /** The untouched input, kept for the global bypass crossfade. */
    juce::AudioBuffer<float> dryBuffer;
    ee::fx::AlignDelay bypassAlign; // dryBuffer, held back by the reported latency

    /** True only for the duration of processBlockBypassed - see there. */
    bool hostBypassed = false;

    /** Written on the audio thread, read from the editor - see currentBpm(). */
    std::atomic<double> lastKnownBpm { 120.0 };

    double sr = 44100.0;
    int maxBlock = 512;

    /** +12 dBFS. Not a limiter - a wall the output should never reach unless
        something upstream has broken. Legit resonance and feedback peaks stay
        well under it. */
    static constexpr float kSafetyCeiling = 4.0f;

    /** Each half of a reorder's dip - see runChainWithReorderFade. Long enough
        not to click, short enough that the gap reads as the chain changing
        rather than as a dropout. */
    static constexpr double kChainFadeSeconds = 0.008;

    int activeChainOrder = 0;  // the order the audio is actually running
    int chainFadeLength = 384; // kChainFadeSeconds in samples, set in prepareToPlay
    int chainFadeLeft = 0;     // samples left in the current half; 0 = at rest
    bool chainFadingIn = false;
    bool chainOrderPrimed = false; // false until the first block after prepare
    std::vector<float> chainDip;   // one half's gain curve, chainFadeLength long

    /** Consecutive sanitised blocks before sanitizeOutput resets the modules.
        Set from the sample rate and block size in prepareToPlay to roughly
        25 ms - long enough that one glitchy block that clears on its own does
        not trip a reset, short enough that a latched blow-up dies fast. */
    int safetyResetHoldBlocks = 16;
    int badBlockRun = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBitAlpineProcessor)
};
