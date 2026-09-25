#include "PluginProcessor.h"

#include "ChainOrder.h"
#include "Params.h"
#include "BitBitAlpineWebEditor.h"

#include "ee/dsp/AutoWahConfig.h"
#include "ee/dsp/BitCrusherConfig.h"
#include "ee/dsp/ChorusConfig.h"
#include "ee/dsp/EqualiserConfig.h"
#include "ee/dsp/CompressorConfig.h"
#include "ee/dsp/PhaserConfig.h"
#include "ee/dsp/RateMap.h"
#include "ee/dsp/RingModulatorConfig.h"
#include "ee/dsp/RustConfig.h"
#include "ee/dsp/SimpleReverbConfig.h"
#include "ee/dsp/SpaceReverb.h"
#include "ee/dsp/SpringConfig.h"
#include "ee/dsp/TapeMachineConfig.h"
#include "ee/dsp/Tremolo.h"
#include "ee/dsp/TremoloConfig.h"
#include "ee/dsp/TubeDriveConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamRange.h"
#include "ee/plugin/ParamText.h"
#include "ee/plugin/StateVersion.h"

#include "ee/fx/DelayTimeMap.h"
#include "ee/fx/ModulationControls.h"

#include "TapeAssets.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iterator>

namespace
{
using namespace ee::alpine;
using namespace ee::fx::modulation; // the Mod module's knob maps, shared with BitBit Modulation
using ee::plugin::kRampSeconds;
using ee::plugin::percentToText;

// The modules restate this rather than including ee/plugin - see
// ee/fx/DelayModuleConfig.h. This is the one place all of them are visible.
static_assert (ee::fx::delaymodule::kGainRampSeconds == kRampSeconds,
               "ee::fx::delaymodule::kGainRampSeconds must track ee::plugin::kRampSeconds");
static_assert (ee::fx::MultiEngineModule::kGainRampSeconds == kRampSeconds,
               "MultiEngineModule::kGainRampSeconds must track ee::plugin::kRampSeconds");

// ------------------------------------------------------------------- artifact
/** A bare rounded "440 Hz", no kHz - the Artifact module's and the Filter
    engine's readouts. Kept separate from this file's `hertzToText` on purpose -
    that one folds to kHz above 1000, and these do not (see CLAUDE.md on
    formatters that share a name but not a behaviour). */
juce::String artHzToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " Hz";
}

/** The Artifact Ring Mod's Freq and Filter readouts, off the ee::dsp::ringmod
    maps ee::fx::ArtifactModule reads - bare rounded "Hz", this file's house
    style (see artHzToText), with the low-pass showing "Off" past its bypass
    point. Tweak stays a plain percent - its meaning is set by Mode. */
juce::String artRingFreqToText (float pct, int)
{
    return artHzToText (ee::dsp::ringmod::freqHzFor (pct * 0.01f), 0);
}

juce::String artRingLpToText (float pct, int)
{
    const float hz = ee::dsp::ringmod::lpHzFor (pct * 0.01f);
    if (hz >= ee::dsp::ringmod::kLpBypassHz)
        return "Off";
    return artHzToText (hz, 0);
}

// The Input/Output trims' range. Asymmetric on purpose, exactly as BitBit
// Delay's are: they are a trim and a level, not a gain stage, so there is more
// cut than boost.
constexpr float kMinGainDb = -24.0f;
constexpr float kMaxGainDb = 12.0f;

// A module's own output trim. Symmetric on purpose, unlike the host's In/Out
// pair above: this one rests at unity and the face draws its arc out from the
// middle, so the two halves have to be the same size or "no change" would not
// be at twelve o'clock. It also stops short of silence at the bottom, which
// the old 0..100 % level did not - and since the four modules are in series,
// a Level anyone could wind to zero was a knob that silenced the whole plugin.
// Mix and the module's own power toggle are how you take a module out.
constexpr float kModuleTrimDb = 12.0f;

constexpr int kDefaultDivision = 5; // 1/8
constexpr float kDefaultTime01 = ee::bitbitdelay::time01ForDivision (kDefaultDivision);

juce::String secondsToText (float value, int)
{
    return value < 1.0f ? juce::String (juce::roundToInt (value * 1000.0f)) + " ms" : juce::String (value, 2) + " s";
}

juce::String hertzToText (float value, int)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (value)) + " Hz";
}

/** "13 ms" - Reverb's Pre-delay never reaches a second, so no unit switch is
    needed, unlike secondsToText above. */
juce::String msToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " ms";
}

/** "100 %" at the voicing this engine was fitted to NI Raum at - Studio's Size
    is a scale, not a 0..1 amount, so it gets its own readout rather than
    addPercent's fixed 0..100 range. */
juce::String sizeToText (float value, int)
{
    return juce::String (juce::roundToInt (value * 100.0f)) + " %";
}

juce::String gainDbToText (float value, int)
{
    return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
}

/** The Artifact Comp's Attack - BitBit Artifact's own readout: a decimal
    under 10 ms, where the knob spends most of its travel. */
juce::String compAttackToText (float ms, int)
{
    return juce::String (ms, ms < 10.0f ? 1 : 0) + " ms";
}

juce::String degreesToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
}

/** BitBit Tape's Tone readout: bipolar, rests at 0, the sign carries the
    direction. Kept in step with plugins/bitbit-tape's own toneToText. */
juce::String toneToText (float value, int)
{
    const int rounded = juce::roundToInt (value);

    if (rounded == 0)
        return "0 %";

    return (rounded > 0 ? "+" : "") + juce::String (rounded) + " %";
}

using Attributes = juce::AudioParameterFloatAttributes;

Attributes withText (juce::String (*fn) (float, int))
{
    return Attributes().withStringFromValueFunction (fn);
}

const juce::NormalisableRange<float> percent { 0.0f, 100.0f, 0.1f };

void addPercent (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                 const char* id,
                 const char* name,
                 float defaultPct)
{
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, percent, defaultPct,
                                                             withText (percentToText)));
}

/** A module's Level: +/- kModuleTrimDb around unity, resting at 0 dB. */
void addTrimDb (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id, const char* name)
{
    const auto range = juce::NormalisableRange<float> (-kModuleTrimDb, kModuleTrimDb, 0.1f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, 0.0f,
                                                             withText (gainDbToText)));
}

/** "eq.b3.freq" - one of the pre-EQ's Advanced band parameters, `band` 0-based. */
juce::String eqBandId (int band, const char* leaf)
{
    return "eq.b" + juce::String (band + 1) + "." + leaf;
}

/** The pre-EQ's Freq. Not hertzToText: that keeps one decimal of kHz, which
    reads 1250 Hz as "1.3 kHz" - fine for a cut's corner, too coarse for a bell
    you are placing. */
juce::String eqHzToText (float value, int)
{
    if (value < 1000.0f)
        return juce::String (juce::roundToInt (value)) + " Hz";
    return juce::String (value / 1000.0f, value < 10000.0f ? 2 : 1) + " kHz";
}

/** The pre-EQ's Q: two decimals, no unit - the way every EQ prints it. */
juce::String qToText (float value, int)
{
    return juce::String (value, value < 10.0f ? 2 : 1);
}

/** The 24 chain.order choice labels, one per permutation, built from
    ChainOrder.h rather than typed out by hand so they cannot drift from
    what permutationForIndex actually decodes each index to. */
juce::StringArray chainOrderLabels()
{
    static constexpr const char* shortName[chainOrder::kNumModules] = { "Art", "Mod", "Dly", "Rev" };

    juce::StringArray labels;
    for (int i = 0; i < chainOrder::kNumPermutations; ++i)
    {
        juce::StringArray parts;
        for (int moduleId : chainOrder::permutationForIndex (i))
            parts.add (shortName[moduleId]);
        labels.add (parts.joinIntoString ("-"));
    }
    return labels;
}
} // namespace

BitBitAlpineProcessor::BitBitAlpineProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Loading a preset is a whole tree arriving at once, which the L/R mirror
    // has to stand down for - see installState.
    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };

    apvts.addParameterListener (id::dlyLeftTime, this);
    apvts.addParameterListener (id::dlyRightTime, this);
    apvts.addParameterListener (id::dlySync, this);

    // The Advanced bands' ids are built, not constants, so their raw values are
    // looked up once here rather than a string being made on the audio thread.
    for (int b = 0; b < ee::dsp::eq::kAdvancedBands; ++b)
    {
        auto& band = eqBandParams[static_cast<size_t> (b)];
        band.on = apvts.getRawParameterValue (eqBandId (b, id::eqBandOn));
        band.type = apvts.getRawParameterValue (eqBandId (b, id::eqBandType));
        band.freq = apvts.getRawParameterValue (eqBandId (b, id::eqBandFreq));
        band.gain = apvts.getRawParameterValue (eqBandId (b, id::eqBandGain));
        band.q = apvts.getRawParameterValue (eqBandId (b, id::eqBandQ));
    }

    loadTapeNoiseSample();

    // The pre-EQ is the user's, not the session's: a real wrapper (AU, VST3,
    // Standalone) opens on the last one set anywhere. An offline tool builds
    // the processor directly - wrapperType_Undefined - and stays hermetic.
    // Only what changes with the window open is remembered - see GlobalEq;
    // auval, for one, sets every parameter and never opens an editor.
    globalEq.shouldRemember = [this] { return getActiveEditor() != nullptr; };
    if (wrapperType != wrapperType_Undefined)
        globalEq.attach (GlobalEq::defaultFile());
}

BitBitAlpineProcessor::~BitBitAlpineProcessor()
{
    apvts.removeParameterListener (id::dlyLeftTime, this);
    apvts.removeParameterListener (id::dlyRightTime, this);
    apvts.removeParameterListener (id::dlySync, this);
}

/** Decodes the embedded tape floor once, at construction, exactly as
    BitBitTapeProcessor does. The Modulation module's tape engine loops it from
    these samples, so the buffer has to outlive every process call - it is a
    member, and the pointers handed over are into it. */
void BitBitAlpineProcessor::loadTapeNoiseSample()
{
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::MemoryInputStream> (
        TapeAssets::tapenoise_wav, static_cast<size_t> (TapeAssets::tapenoise_wavSize), false);

    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return; // no recording: the engine falls back to synthesised hiss

    const int numSamples = static_cast<int> (juce::jmin (reader->lengthInSamples, juce::int64 (10 * 60 * 44100)));
    const int numChannels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    tapeNoiseSample.setSize (numChannels, numSamples);
    reader->read (&tapeNoiseSample, 0, numSamples, 0, true, numChannels > 1);
    tapeNoiseSampleRate = reader->sampleRate;

    tapeNoiseChannels.resize (static_cast<size_t> (numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        tapeNoiseChannels[static_cast<size_t> (ch)] = tapeNoiseSample.getReadPointer (ch);

    modulation.setTapeNoiseSample (tapeNoiseChannels.data(), numChannels, numSamples, tapeNoiseSampleRate);
}

/** Mirrors one Delay-module Time parameter onto the other, guarded by the
    caller against re-entry. Kept in normalised units and only written when it
    actually differs, so the host sees one automation move, not a jitter. */
void BitBitAlpineProcessor::mirrorTime (const juce::String& from, const juce::String& to)
{
    auto* source = apvts.getParameter (from);
    auto* destination = apvts.getParameter (to);

    if (source == nullptr || destination == nullptr)
        return;

    const float value = source->getValue();

    if (std::abs (destination->getValue() - value) > 1.0e-6f)
        destination->setValueNotifyingHost (value);
}

/** Installs a whole APVTS tree - a host restoring a session, a preset load -
    with the Sync L/R mirror held off for the length of it. A tree arriving in
    one piece is self-consistent already, and its parameters land one at a time
    in the file's own order, so without this bracket the mirror fires for the
    first Time written while the button still holds the previous state and
    collapses a preset whose two sides are deliberately apart. This mirrors
    BitBitDelayProcessor::installState. */
void BitBitAlpineProcessor::installState (const juce::ValueTree& tree)
{
    installingState = true;
    // Whatever the tree says about the pre-EQ, the one in force stays - see
    // GlobalEq.
    apvts.replaceState (globalEq.keepCurrent (tree));
    installingState = false;
}

void BitBitAlpineProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (installingState.load())
        return;

    // Read the button from the callback argument rather than a cached value:
    // the two are not guaranteed to be in step at this point.
    const bool synced =
        parameterID == id::dlySync ? newValue > 0.5f : apvts.getRawParameterValue (id::dlySync)->load() > 0.5f;

    if (! synced)
        return;

    if (mirroring.exchange (true))
        return;

    // Turning sync on adopts the left value, which is the one the user set last
    // in the common case of reaching for the button after dialling the left knob.
    if (parameterID == id::dlyRightTime)
        mirrorTime (id::dlyRightTime, id::dlyLeftTime);
    else
        mirrorTime (id::dlyLeftTime, id::dlyRightTime);

    mirroring = false;
}

juce::AudioProcessorValueTreeState::ParameterLayout BitBitAlpineProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ----------------------------------------------------------------- global
    const auto gainRange = juce::NormalisableRange<float> (kMinGainDb, kMaxGainDb, 0.1f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::inGain, 1 }, "Input", gainRange,
                                                             0.0f, withText (gainDbToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::outGain, 1 }, "Output", gainRange,
                                                             0.0f, withText (gainDbToText)));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::on, 1 }, "On", true));

    // --------------------------------------------------------------- artifact
    // Every id, range and default is BitBit Artifact's, because the module is
    // BitBit Artifact's. All four engines are voiced (see ee::fx::ArtifactModule).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::artOn, 1 }, "Artifact On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::artEngine, 1 }, "Artifact Engine",
        juce::StringArray { "Ring Mod", "Bit Crush", "Rust", "Drive", "Comp" }, 0));
    addTrimDb (layout, id::artLevel, "Artifact Level");
    addPercent (layout, id::artMix, "Artifact Mix", 50.0f);

    // Bit Crush knobs, 0..100 - percent host text, resolved to
    // real units by ee::fx::ArtifactModule through the ee::dsp::bitcrush maps.
    addPercent (layout, id::artCrushBits, "Artifact Bits", ee::dsp::bitcrush::kDefaultBitsPct);
    addPercent (layout, id::artCrushRate, "Artifact Rate", ee::dsp::bitcrush::kDefaultRatePct);
    addPercent (layout, id::artCrushLp, "Artifact Crush Filter", ee::dsp::bitcrush::kDefaultLpPct);
    addPercent (layout, id::artCrushJitter, "Artifact Jitter", ee::dsp::bitcrush::kDefaultJitterPct);

    // Ring Mod. Freq and Filter spell out real Hz off the ee::dsp::ringmod maps;
    // Tweak is a plain percent, its meaning set by
    // Mode; Rectify is bipolar and rests dead centre, doing nothing there.
    // Blend is Artifact Mix, not a knob of its own.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artRingFreq, 1 }, "Artifact Ring Freq", percent, ee::dsp::ringmod::kDefaultFreqPct,
        withText (artRingFreqToText)));
    addPercent (layout, id::artRingTweak, "Artifact Tweak", ee::dsp::ringmod::kDefaultTweakPct);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artRingLp, 1 }, "Artifact Ring Filter", percent, ee::dsp::ringmod::kDefaultLpPct,
        withText (artRingLpToText)));
    layout.add (
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::artRingRect, 1 }, "Artifact Rectify",
                                                     juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
                                                     ee::dsp::ringmod::kDefaultRectifyPct, withText (toneToText)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::artRingMode, 1 }, "Artifact Mode",
                                                              juce::StringArray { "Wobble", "Octave" }, 0));

    // Rust. One knob - Grind, a plain percent. Wear, its recovery and the post
    // low-pass are fixed inside the engine. Blend is Artifact Mix and tone is
    // Artifact Tone, not knobs of its own.
    addPercent (layout, id::artRustGrind, "Artifact Grind", ee::dsp::rust::kDefaultGrindPct);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::artRustMode, 1 }, "Artifact Rust Mode", juce::StringArray { "Oxide", "Contact" }, 0));

    // Amp. Drive is matched to a real reference unit at 100 %, plain percent
    // (see ee/dsp/TubeDriveConfig.h). Mids and Bit are also plain percent host text
    // (like Bit Crush's own knobs above) - the real unit (dB, the hold rate)
    // is BitBit Artifact's own readout, not this file's. Tone is the same
    // bipolar tilt BitBit Tape's Tone is: -100 dark, 0 flat and bypassed, +100
    // bright, resting in the middle.
    addPercent (layout, id::artAmpDrive, "Artifact Amp Drive", ee::dsp::tubedrive::kDefaultDrivePct);
    addPercent (layout, id::artAmpMids, "Artifact Amp Mids", 0.0f);
    addPercent (layout, id::artAmpBit, "Artifact Amp Bit", ee::fx::ArtifactModule::kAmpDefaultBitPct);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artAmpTone, 1 }, "Artifact Amp Tone",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, withText (toneToText)));

    // ------------------------------------------------------------- modulation
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modOn, 1 }, "Modulation On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::modEngine, 1 }, "Modulation Engine",
        juce::StringArray { "Tape", "Tremolo", "Chorus", "Phaser", "Filter" }, 1));
    addTrimDb (layout, id::modLevel, "Modulation Level");
    addPercent (layout, id::modMix, "Modulation Mix", 40.0f);

    // Ranges and defaults are each engine's own pedal's, so the same knob
    // position sounds the same in both places.
    addPercent (layout, id::modTapeSat, "Tape Saturation", ee::dsp::tape::kDefaultSaturationPct);
    addPercent (layout, id::modTapeFlutter, "Tape Flutter", ee::dsp::tape::kDefaultFlutterPct);
    addPercent (layout, id::modTapeWear, "Tape Wear", ee::dsp::tape::kDefaultWearPct);
    addPercent (layout, id::modTapeNoise, "Tape Noise", ee::dsp::tape::kDefaultNoisePct);

    // Tape's own Tone is gone: the module's footer Tone (below, appended) does
    // it for every engine.
    //
    // Mono is one transport under both channels; Stereo opens them onto
    // different points of a slow modulation. On by default, as BitBit Tape's is.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modTapeStereo, 1 }, "Tape Stereo",
                                                            ee::dsp::tape::kDefaultStereoOn));

    addPercent (layout, id::modTremAmount, "Tremolo Amount", 50.0f);
    // A plain 0..1 knob, like BitBit Trem & Pan's: what a position means depends
    // on the Tremolo Sync switch - a free LFO period in ms, or a tempo-locked
    // note division. Host text is the synced reading (a host has no pill to say
    // which mode the knob is in) and the editor overrides it live, the same
    // choice the Filter Time knob and BitBit Trem & Pan make.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::modTremRate, 1 }, "Tremolo Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        withText ([] (float v, int) { return kTremRateMap.rateToText (v, true); })));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modTremSync, 1 }, "Tremolo Sync", false));
    addPercent (layout, id::modTremShape, "Tremolo Shape", 50.0f);
    addPercent (layout, id::modTremTube, "Tremolo Tube", 0.0f);

    auto chorusRate = juce::NormalisableRange<float> (ee::dsp::config::kRateMinHz, ee::dsp::config::kRateMaxHz);
    chorusRate.setSkewForCentre (1.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modChorusRate, 1 }, "Chorus Rate",
                                                             chorusRate, ee::dsp::config::kDefaultRateHz,
                                                             withText (ee::plugin::hzToText)));
    addPercent (layout, id::modChorusDepth, "Chorus Depth", ee::dsp::config::kDefaultDepthPct);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::modChorusPhase, 1 }, "Chorus Phase",
        juce::NormalisableRange<float> (0.0f, ee::dsp::config::kMaxPhaseDeg, 1.0f), ee::dsp::config::kDefaultPhaseDeg,
        withText (degreesToText)));

    auto phaserRate = juce::NormalisableRange<float> (ee::dsp::phaser::kRateMinHz, ee::dsp::phaser::kRateMaxHz);
    phaserRate.setSkewForCentre (1.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modPhaseRate, 1 }, "Phaser Rate",
                                                             phaserRate, ee::dsp::phaser::kDefaultRateHz,
                                                             withText (ee::plugin::hzToText)));
    addPercent (layout, id::modPhaseDepth, "Phaser Depth", ee::dsp::phaser::kDefaultDepthPct);

    // Filter. Freq prints as a bare rounded "Hz" (artHzToText, not this file's
    // kHz-folding hertzToText), so it is spelled out rather than going through
    // addPercent.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::modFilterFreq, 1 }, "Filter Freq", percent, ee::dsp::autowah::kDefaultFreqPct,
        withText ([] (float v, int) { return artHzToText (filterFreqHzFor (v), 0); })));
    addPercent (layout, id::modFilterQ, "Filter Q", ee::dsp::autowah::kDefaultQPct);
    addPercent (layout, id::modFilterRange, "Filter Range", ee::dsp::autowah::kDefaultRangePct);

    // One normalised knob; the Sync switch decides what it means. Knob down =
    // fastest in both modes (see kFilterRateMap). Host text is the synced
    // reading; the editor overrides it live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::modFilterTime, 1 }, "Filter Time", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f),
        0.5f, withText ([] (float v, int) { return filterRateToText (v, true); })));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modFilterSync, 1 }, "Filter Sync", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::modFilterWave, 1 }, "Filter Wave",
                                                              juce::StringArray { "Triangle", "Ramp", "Square" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modFilterStereo, 1 },
                                                            "Filter Stereo", false));

    // ------------------------------------------------------------------ delay
    // Every id, range and default is BitBit Delay's, because the module behind
    // them is BitBit Delay's chain. The leaf names match that pedal's exactly,
    // which is what lets one DelayFace component drive both faces off a prefix.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlyOn, 1 }, "Delay On", true));

    // The host's own text for a Time knob is the synced reading at the
    // reference tempo - the same choice BitBit Delay makes, for the same reason:
    // a host has no pill to tell it which mode the knob is in.
    //
    // Meta, all three: with Sync L/R on, turning one Time knob moves the other
    // (parameterChanged -> mirrorTime), and turning Sync on adopts the left
    // value into the right. auval's "parameter did not stay set" check fails on
    // exactly that unless the parameters doing it are flagged - and a host that
    // caches parameter values needs the flag to know to re-read.
    const auto timeAttributes =
        Attributes()
            .withStringFromValueFunction (
                [] (float v, int)
                { return ee::bitbitdelay::timeMap().toText (v, true, ee::bitbitdelay::kReferenceBpm); })
            .withMeta (true);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyLeftTime, 1 }, "Left Time",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             kDefaultTime01, timeAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyRightTime, 1 }, "Right Time",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             kDefaultTime01, timeAttributes));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlySync, 1 }, "Sync L/R", true,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlyTimeUnit, 1 }, "Time Unit", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::dlyType, 1 }, "Delay Type",
                                                              juce::StringArray { "Normal", "Wide", "Ping Pong" }, 0));
    addPercent (layout, id::dlyFeedback, "Feedback", 35.0f);
    addTrimDb (layout, id::dlyLevel, "Delay Level");
    addPercent (layout, id::dlyMix, "Delay Mix", 35.0f);
    addPercent (layout, id::dlyWear, "Delay Wear", 0.0f);
    addPercent (layout, id::dlyFlutter, "Delay Flutter", 0.0f);
    addPercent (layout, id::dlyDrift, "Drift", 0.0f);
    addPercent (layout, id::dlyPhaser, "Delay Phaser", 0.0f);

    // Ranges, skews and defaults are BitBit EQ's, down to the skew centres: the
    // same two cuts, so the same knob position should mean the same corner.
    auto loCutRange = juce::NormalisableRange<float> (kLoCutMinHz, kLoCutMaxHz);
    loCutRange.setSkewForCentre (120.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyLoCut, 1 }, "Delay Low Cut",
                                                             loCutRange, kLoCutMinHz, withText (hertzToText)));

    auto hiCutRange = juce::NormalisableRange<float> (kHiCutMinHz, kHiCutMaxHz);
    hiCutRange.setSkewForCentre (4000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyHiCut, 1 }, "Delay High Cut",
                                                             hiCutRange, kHiCutMaxHz, withText (hertzToText)));

    // ----------------------------------------------------------------- reverb
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::revOn, 1 }, "Reverb On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::revEngine, 1 }, "Reverb Engine",
        juce::StringArray { "Spring", "Shimmer", "Studio", "Simple" }, 2));
    addTrimDb (layout, id::revLevel, "Reverb Level");
    addPercent (layout, id::revMix, "Reverb Mix", 30.0f);

    // Shimmer (the `space.` ids - see Params.h). Its own feedback amount and
    // Reso stay fixed inside ee::fx::ReverbModule::setShimmer; Decay is a
    // knob again, clamped to ReverbModule::kMinShimmerDecay..kMaxShimmerDecay
    // rather than FdnReverb's own wider range.
    auto revSpaceDecay = juce::NormalisableRange<float> (ee::fx::ReverbModule::kMinShimmerDecay,
                                                         ee::fx::ReverbModule::kMaxShimmerDecay);
    // 8 s - what this was pinned to before Decay came back as a knob (the old
    // FdnReverb::kMaxDecay, back when that was 8 rather than 10), so an
    // untouched Shimmer sounds exactly as it always has.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpaceDecay, 1 },
                                                             "Shimmer Decay", revSpaceDecay, 8.0f,
                                                             withText (secondsToText)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::revSpaceOctave, 1 },
                                                              "Shimmer Octave",
                                                              juce::StringArray { "-1 Oct", "0", "+1 Oct" }, 2));

    auto spaceLoCut =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinLowCutHz, ee::dsp::FdnReverb::kMaxLowCutHz);
    spaceLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpaceLoCut, 1 },
                                                             "Shimmer Low Cut", spaceLoCut,
                                                             ee::dsp::FdnReverb::kMinLowCutHz, withText (hertzToText)));

    auto spaceHiCut =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinHighCutHz, ee::dsp::FdnReverb::kMaxHighCutHz);
    spaceHiCut.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpaceHiCut, 1 },
                                                             "Shimmer Hi Cut", spaceHiCut,
                                                             ee::dsp::FdnReverb::kMaxHighCutHz, withText (hertzToText)));
    addPercent (layout, id::revSpaceDamping, "Shimmer Damping", 50.0f);

    auto springDecay =
        juce::NormalisableRange<float> (ee::dsp::spring::kMinDecaySeconds, ee::dsp::spring::kMaxDecaySeconds);
    springDecay.setSkewForCentre (ee::dsp::spring::kDecaySkewCentre);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::revSpringDecay, 1 }, "Spring Decay", springDecay,
        ee::plugin::snapToRange (springDecay, ee::dsp::spring::kDefaultDecaySeconds), withText (secondsToText)));
    addPercent (layout, id::revSpringTension, "Spring Tension", ee::dsp::spring::kDefaultTension01 * 100.0f);

    // The same travel Shimmer's Low Cut has, deliberately - see SpringConfig.h.
    auto springLoCut = juce::NormalisableRange<float> (ee::dsp::spring::kMinLowCutHz, ee::dsp::spring::kMaxLowCutHz);
    springLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpringLoCut, 1 },
                                                             "Spring Low Cut", springLoCut,
                                                             ee::dsp::spring::kOutputLowCutHz, withText (hertzToText)));

    // Used to be fixed voicing (SpringConfig.h's kOutputHighCutHz); the same
    // travel the other two engines' Hi Cut has, for the same reason Low Cut's
    // does. Defaults to that old fixed value, so an untouched tank is unchanged.
    auto springHiCut = juce::NormalisableRange<float> (ee::dsp::spring::kMinHighCutHz, ee::dsp::spring::kMaxHighCutHz);
    springHiCut.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpringHiCut, 1 },
                                                             "Spring Hi Cut", springHiCut,
                                                             ee::dsp::spring::kOutputHighCutHz, withText (hertzToText)));

    // The signal-chain order, appended rather than slotted into its own
    // section: AU addresses parameters by index, not name, so inserting this
    // one earlier would shift the index of every parameter after it. Index 0 - its
    // default - is Artifact, Modulation, Delay, Reverb: today's fixed order,
    // so a session or preset that predates this parameter is unaffected.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::chainOrder, 1 }, "Chain Order",
                                                              chainOrderLabels(), 0));

    // Studio, appended last - BitBit Reverb's, id for id; see its processor
    // for what the knobs mean.
    using Studio = ee::dsp::SpaceReverb;
    auto studioDecay = juce::NormalisableRange<float> (Studio::kMinDecay, Studio::kMaxDecay);
    studioDecay.setSkewForCentre (2.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::revStudioDecay, 1 }, "Studio Decay", studioDecay, 3.0f, withText (secondsToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::revStudioSize, 1 }, "Studio Size",
        juce::NormalisableRange<float> (Studio::kMinSize, Studio::kMaxSize), Studio::kDefaultSize,
        withText (sizeToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::revStudioPredelay, 1 }, "Studio Pre-delay",
        juce::NormalisableRange<float> (Studio::kMinPredelayMs, Studio::kMaxPredelayMs), 0.0f, withText (msToText)));
    addPercent (layout, id::revStudioDamping, "Studio Damping", 25.0f);

    auto studioLoCut = juce::NormalisableRange<float> (Studio::kMinLowCutHz, Studio::kMaxLowCutHz);
    studioLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revStudioLoCut, 1 },
                                                             "Studio Low Cut", studioLoCut, Studio::kMinLowCutHz,
                                                             withText (hertzToText)));

    auto studioHiCut = juce::NormalisableRange<float> (Studio::kMinHighCutHz, Studio::kMaxHighCutHz);
    studioHiCut.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revStudioHiCut, 1 },
                                                             "Studio Hi Cut", studioHiCut, Studio::kMaxHighCutHz,
                                                             withText (hertzToText)));

    // The three switchable modules' footer Tone, beside each one's Mix and like
    // it the module's rather than an engine's: -100 dark, 0 flat and bypassed,
    // +100 bright. BitBit Artifact's, Modulation's and Reverb's `tone`, id for
    // id. Appended, for the same AU-index reason Chain Order is.
    for (const auto& [pid, name] : { std::pair { id::artTone, "Artifact Tone" },
                                     std::pair { id::modTone, "Modulation Tone" },
                                     std::pair { id::revTone, "Reverb Tone" } })
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid, 1 }, name,
                                                                 juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
                                                                 0.0f, withText (toneToText)));

    // The Reverb module's Simple engine, appended last - BitBit Reverb's
    // `simple.amount`, id for id.
    addPercent (layout, id::revSimpleAmount, "Reverb Simple Amount", ee::dsp::simple::kDefaultAmountPct);

    // Tremolo Attack: the per-note swell (ee::dsp::Tremolo, TremoloConfig.h's
    // ATTACK). 0 is off - the tremolo as it always was - up to 4 s, with the
    // middle of the knob at 0.5 s. Appended so every other parameter keeps its
    // index.
    auto tremAttack = juce::NormalisableRange<float> (0.0f, ee::dsp::tremolo::kAttackMaxSeconds);
    tremAttack.setSkewForCentre (ee::dsp::tremolo::kAttackSkewCentreSeconds);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modTremAttack, 1 },
                                                             "Tremolo Attack", tremAttack, 0.0f,
                                                             withText (secondsToText)));

    // The Artifact module's Comp engine - BitBit Artifact's `comp.*`, id for
    // id, range for range (no Level - it matches the input's level itself).
    // Appended so every other parameter keeps its index.
    addPercent (layout, id::artCompSensitivity, "Artifact Comp Sensitivity", ee::dsp::comp::kDefaultSensitivityPct);

    auto compAttack = juce::NormalisableRange<float> (ee::dsp::comp::kAttackMinMs, ee::dsp::comp::kAttackMaxMs, 0.01f);
    compAttack.setSkewForCentre (ee::dsp::comp::kAttackCentreMs);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::artCompAttack, 1 },
                                                             "Artifact Comp Attack", compAttack,
                                                             ee::dsp::comp::kDefaultAttackMs,
                                                             withText (compAttackToText)));

    // The header's pre-EQ - see Params.h. Appended so every other parameter
    // keeps its index. Everything defaults flat, and a flat band is not in the
    // signal at all (ee::dsp::Equaliser), so a session from before it renders
    // sample for sample as it did.
    {
        namespace eq = ee::dsp::eq;

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::eqOn, 1 }, "EQ On", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::eqMode, 1 }, "EQ Mode",
                                                                  juce::StringArray { "Simple", "Advanced" }, 0));

        const auto gainRange = juce::NormalisableRange<float> (-eq::kMaxGainDb, eq::kMaxGainDb, 0.1f);
        for (const auto& [pid, name] : { std::pair { id::eqLow, "EQ Low" }, std::pair { id::eqMid, "EQ Mid" },
                                         std::pair { id::eqHigh, "EQ High" } })
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid, 1 }, name, gainRange,
                                                                     0.0f, withText (gainDbToText)));

        juce::StringArray typeNames { "Low Cut 48", "Low Cut 12", "Low Shelf",   "Bell",
                                      "Notch",      "High Shelf", "High Cut 12", "High Cut 48" };
        jassert (typeNames.size() == eq::kNumTypes);

        auto freqRange = juce::NormalisableRange<float> (eq::kMinHz, eq::kMaxHz);
        freqRange.setSkewForCentre (eq::kHzSkewCentre);
        auto qRange = juce::NormalisableRange<float> (eq::kMinQ, eq::kMaxQ, 0.01f);
        qRange.setSkewForCentre (eq::kQSkewCentre);

        for (int b = 0; b < eq::kAdvancedBands; ++b)
        {
            const auto& d = eq::kAdvancedDefaults[static_cast<size_t> (b)];
            const auto name = "EQ " + juce::String (b + 1) + " ";

            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { eqBandId (b, id::eqBandOn), 1 },
                                                                    name + "On", d.on));
            layout.add (
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { eqBandId (b, id::eqBandType), 1 },
                                                              name + "Type", typeNames, static_cast<int> (d.type)));
            layout.add (
                std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { eqBandId (b, id::eqBandFreq), 1 },
                                                             name + "Freq", freqRange, d.hz, withText (eqHzToText)));
            layout.add (
                std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { eqBandId (b, id::eqBandGain), 1 },
                                                             name + "Gain", gainRange, 0.0f, withText (gainDbToText)));
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { eqBandId (b, id::eqBandQ), 1 }, name + "Q", qRange,
                ee::plugin::snapToRange (qRange, eq::kDefaultQ), withText (qToText)));
        }
    }

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::artCompScFilter, 1 },
                                                            "Artifact Comp SC Filter", true));

    return layout;
}

// ---------------------------------------------------------------- the readouts

double BitBitAlpineProcessor::readPlayHeadBpm()
{
    double bpm = currentBpm();

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpmValue = position->getBpm())
                if (std::isfinite (*hostBpmValue))
                    bpm = *hostBpmValue;

    bpm = juce::jlimit (20.0, 300.0, bpm);
    lastKnownBpm.store (bpm, std::memory_order_relaxed);
    return bpm;
}

double BitBitAlpineProcessor::currentBpm() const
{
    const double bpm = lastKnownBpm.load (std::memory_order_relaxed);
    return std::isfinite (bpm) ? juce::jlimit (20.0, 300.0, bpm) : 120.0;
}

bool BitBitAlpineProcessor::delayIsSynced() const
{
    return apvts.getRawParameterValue (id::dlyTimeUnit)->load() <= 0.5f;
}

float BitBitAlpineProcessor::timeMs (const char* parameterId) const
{
    const float time01 = apvts.getRawParameterValue (parameterId)->load();
    return ee::bitbitdelay::timeMap().value (time01, delayIsSynced(), currentBpm());
}

juce::String BitBitAlpineProcessor::timeReadout (const char* parameterId) const
{
    const float time01 = apvts.getRawParameterValue (parameterId)->load();
    return ee::bitbitdelay::timeMap().toText (time01, delayIsSynced(), currentBpm());
}

/** The millisecond reading regardless of the unit toggle - the face's Readout
    shows this as a small constant figure beside the toggle-aware one. */
juce::String BitBitAlpineProcessor::timeMsReadout (const char* parameterId) const
{
    const float time01 = apvts.getRawParameterValue (parameterId)->load();
    return ee::bitbitdelay::timeMap().toText (time01, false, currentBpm());
}

juce::String BitBitAlpineProcessor::filterTimeReadout() const
{
    const float time01 = apvts.getRawParameterValue (id::modFilterTime)->load();
    const bool synced = apvts.getRawParameterValue (id::modFilterSync)->load() > 0.5f;
    return filterRateToText (time01, synced);
}

juce::String BitBitAlpineProcessor::tremoloRateReadout() const
{
    const float rate01 = apvts.getRawParameterValue (id::modTremRate)->load();
    const bool synced = apvts.getRawParameterValue (id::modTremSync)->load() > 0.5f;
    return kTremRateMap.rateToText (rate01, synced);
}

// ------------------------------------------------------------------- settings

void BitBitAlpineProcessor::pushSettings (double bpm) noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };
    const auto flag = [&raw] (const char* pid) { return raw (pid) > 0.5f; };

    // Bypassed, the output is the global crossfade's dry path whatever the
    // modules make, so each is told it is off too - and, once its own ramp
    // and (for the Delay) its trails are done, stops running. A bypassed Alpine
    // costs its dry path, not four modules nobody hears. Coming back, the
    // global ramp opens onto the modules' own dry paths and each module's wet
    // follows as it wakes.
    const bool pluginOn = flag (id::on) && ! hostBypassed;
    const auto moduleOn = [&flag, pluginOn] (const char* pid) { return pluginOn && flag (pid); };

    // ----------------------------------------------------------------- artifact
    artifact.setEngine (static_cast<int> (raw (id::artEngine)));
    artifact.setEngaged (moduleOn (id::artOn));
    artifact.setLevel (juce::Decibels::decibelsToGain (raw (id::artLevel)));
    artifact.setMix01 (pct (id::artMix));
    artifact.setTone (raw (id::artTone) * 0.01f);

    artifact.setCrush (pct (id::artCrushBits), pct (id::artCrushRate), pct (id::artCrushLp), pct (id::artCrushJitter));

    artifact.setRing (pct (id::artRingFreq), pct (id::artRingTweak), pct (id::artRingLp), raw (id::artRingRect) * 0.01f,
                      static_cast<int> (raw (id::artRingMode)));

    artifact.setRust (pct (id::artRustGrind), static_cast<int> (raw (id::artRustMode)));

    artifact.setAmp (pct (id::artAmpDrive), pct (id::artAmpMids), pct (id::artAmpBit), raw (id::artAmpTone) * 0.01f);
    artifact.setComp (pct (id::artCompSensitivity), raw (id::artCompAttack), flag (id::artCompScFilter));

    // --------------------------------------------------------------- modulation
    modulation.setEngine (static_cast<int> (raw (id::modEngine)));
    modulation.setEngaged (moduleOn (id::modOn));
    modulation.setLevel (juce::Decibels::decibelsToGain (raw (id::modLevel)));
    modulation.setMix01 (pct (id::modMix));
    modulation.setTone (raw (id::modTone) * 0.01f);

    // Stereo is the machine's mono/stereo switch.
    modulation.setTape (pct (id::modTapeSat), pct (id::modTapeFlutter), pct (id::modTapeWear), pct (id::modTapeNoise),
                        flag (id::modTapeStereo) ? 1.0f : 0.0f);

    // The Rate knob is a position, not a rate: what it means is this face's
    // map, set by the Tremolo Sync switch - a free period in ms, or a
    // tempo-locked note division. The transport that aligns a synced LFO's
    // phase to the host grid is handed over separately, from processBlock.
    const bool tremSynced = flag (id::modTremSync);
    const float tremPeriodSeconds = kTremRateMap.rateToPeriodSeconds (raw (id::modTremRate), tremSynced, bpm);
    modulation.setTremolo (pct (id::modTremAmount), tremPeriodSeconds, pct (id::modTremShape), pct (id::modTremTube));
    modulation.setTremoloAttack (raw (id::modTremAttack));

    modulation.setChorus (raw (id::modChorusRate), pct (id::modChorusDepth), raw (id::modChorusPhase));
    modulation.setPhaser (raw (id::modPhaseRate), pct (id::modPhaseDepth));

    {
        const bool filterSynced = flag (id::modFilterSync);
        const float filterPeriod =
            juce::jmax (1.0e-4f, filterRateToPeriodSeconds (raw (id::modFilterTime), filterSynced, bpm));

        modulation.setFilter (pct (id::modFilterFreq), pct (id::modFilterQ), pct (id::modFilterRange),
                              filterWaveShape01 (static_cast<int> (raw (id::modFilterWave))), filterPeriod,
                              flag (id::modFilterStereo));
    }

    // -------------------------------------------------------------------- delay
    const bool synced = delayIsSynced();

    delay.setTimes (ee::bitbitdelay::timeSeconds (raw (id::dlyLeftTime), synced, bpm),
                    ee::bitbitdelay::timeSeconds (raw (id::dlyRightTime), synced, bpm));
    delay.setFeedback01 (pct (id::dlyFeedback));

    const int typeIndex = static_cast<int> (raw (id::dlyType));
    delay.setRouting (typeIndex == 1   ? ee::dsp::TapeDelay::Routing::wide
                      : typeIndex == 2 ? ee::dsp::TapeDelay::Routing::pingPong
                                       : ee::dsp::TapeDelay::Routing::normal);

    delay.setTape (pct (id::dlyWear), pct (id::dlyFlutter));
    delay.setDrift01 (pct (id::dlyDrift));
    delay.setPhaser01 (pct (id::dlyPhaser));
    delay.setFilter (raw (id::dlyLoCut), raw (id::dlyHiCut));
    delay.setMix01 (pct (id::dlyMix));
    delay.setEngaged (moduleOn (id::dlyOn));

    // The host owns the input trim, so the module's is left at unity. The
    // module's output trim is the Delay Level knob in the header - the same
    // +/- kModuleTrimDb trim mod.level and rev.level are, resting at 0 dB where
    // decibelsToGain is exactly 1.0f and the module is bit-identical bypassed.
    delay.setTrims (1.0f, juce::Decibels::decibelsToGain (raw (id::dlyLevel)));

    // ------------------------------------------------------------------- reverb
    reverb.setEngine (static_cast<int> (raw (id::revEngine)));
    reverb.setEngaged (moduleOn (id::revOn));
    reverb.setLevel (juce::Decibels::decibelsToGain (raw (id::revLevel)));
    reverb.setMix01 (pct (id::revMix));
    reverb.setTone (raw (id::revTone) * 0.01f);

    // The octave choice's raw value is its index (0/1/2), not the -1/0/+1 the
    // engine wants.
    reverb.setShimmer (raw (id::revSpaceDecay), static_cast<int> (raw (id::revSpaceOctave)) - 1, raw (id::revSpaceLoCut),
                       raw (id::revSpaceHiCut), pct (id::revSpaceDamping));
    reverb.setStudio (raw (id::revStudioDecay), raw (id::revStudioPredelay), pct (id::revStudioDamping),
                      raw (id::revStudioLoCut), raw (id::revStudioHiCut), raw (id::revStudioSize));

    // --------------------------------------------------------------------- eq
    // One engine, both faces: bands 0..2 are Simple's, the rest Advanced's, and
    // whichever face is not selected has every band off - which fades it out
    // rather than cutting it (see ee::dsp::Equaliser).
    {
        namespace eq = ee::dsp::eq;
        using Band = ee::dsp::Equaliser::BandSettings;

        const bool eqOn = flag (id::eqOn);
        const bool advanced = static_cast<int> (raw (id::eqMode)) == 1;
        const bool simpleOn = eqOn && ! advanced;

        equaliser.setBand (
            0, Band { eq::FilterType::lowShelf, eq::kSimpleLowHz, raw (id::eqLow), eq::kSimpleShelfQ, simpleOn });
        equaliser.setBand (1,
                           Band { eq::FilterType::bell, eq::kSimpleMidHz, raw (id::eqMid), eq::kSimpleMidQ, simpleOn });
        equaliser.setBand (
            2, Band { eq::FilterType::highShelf, eq::kSimpleHighHz, raw (id::eqHigh), eq::kSimpleShelfQ, simpleOn });

        for (int b = 0; b < eq::kAdvancedBands; ++b)
        {
            const auto& p = eqBandParams[static_cast<size_t> (b)];
            const int type = juce::jlimit (0, eq::kNumTypes - 1, static_cast<int> (p.type->load()));

            equaliser.setBand (kEqSimpleBands + b,
                               Band { static_cast<eq::FilterType> (type), p.freq->load(), p.gain->load(), p.q->load(),
                                      eqOn && advanced && p.on->load() > 0.5f });
        }
    }
    reverb.setSimple (pct (id::revSimpleAmount));
    reverb.setSpring (raw (id::revSpringDecay), pct (id::revSpringTension), raw (id::revSpringLoCut),
                      raw (id::revSpringHiCut));
}

// ---------------------------------------------------------------------- audio

void BitBitAlpineProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    badBlockRun = 0;
    chainFadeLength = juce::jmax (1, static_cast<int> (std::lround (kChainFadeSeconds * sampleRate)));
    chainFadeLeft = 0;
    chainFadingIn = false;
    chainOrderPrimed = false;
    chainDip.assign (static_cast<size_t> (chainFadeLength), 0.0f);
    safetyResetHoldBlocks =
        juce::jlimit (4, 64, static_cast<int> (std::lround (0.025 * sampleRate / juce::jmax (1, maxBlock))));

#if EE_ALPINE_WATCHDOG
    watchdog.prepare (*this, sampleRate, maxBlock);
#endif

    // prepareToPlay is one of the callbacks where the playhead is valid, so the
    // cache starts out holding the host's real tempo rather than 120.
    pushSettings (readPlayHeadBpm());

    // After pushSettings: prepare lands every band on its target outright.
    equaliser.prepare (sampleRate);

    delay.setFilterRestingPoints (kLoCutMinHz, kHiCutMaxHz);

    artifact.prepare (sampleRate, maxBlock);
    modulation.prepare (sampleRate, maxBlock);
    delay.prepare (sampleRate, maxBlock);
    reverb.prepare (sampleRate, maxBlock);

    modSync.reset();

    // Re-hand the tape floor after prepare, the same belt-and-braces
    // BitBitTapeProcessor uses - the read rate is worked out from both the sample
    // and the session, and prepare has just changed the session's.
    if (! tapeNoiseChannels.empty())
        modulation.setTapeNoiseSample (tapeNoiseChannels.data(), static_cast<int> (tapeNoiseChannels.size()),
                                       tapeNoiseSample.getNumSamples(), tapeNoiseSampleRate);

    inputMeter.prepare (sampleRate);
    tuner.prepare (sampleRate);
    eqSpectrum.prepare (sampleRate);

    dryBuffer.setSize (kMaxChannels, maxBlock, false, true, true);

    inGain.reset (sampleRate, kRampSeconds);
    outGain.reset (sampleRate, kRampSeconds);
    engageGain.reset (sampleRate, kRampSeconds);
    tunerMuteGain.reset (sampleRate, kRampSeconds);

    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::inGain)->load()));
    outGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::outGain)->load()));
    engageGain.setCurrentAndTargetValue (apvts.getRawParameterValue (id::on)->load() > 0.5f ? 1.0f : 0.0f);
    tunerMuteGain.setCurrentAndTargetValue (tunerMuted() ? 0.0f : 1.0f);

    // Only the Modulation module's alignment delays the signal without that
    // being the effect: its Tape engine's transport, which every other engine
    // is padded out to. The Delay module's tape is on its repeats, so its dry
    // path has none, and the Artifact and Reverb modules' engines are all
    // latency-free. They are summed anyway, in series, for the day one is not.
    const int latency = artifact.latencySamples() + modulation.latencySamples() + delay.latencySamples();
    setLatencySamples (latency);

    // The global bypass's reference is held back by the same figure, so a
    // bypassed plugin is the input *as the host is compensating for it* and
    // not the input ahead of where the rest of the session is.
    bypassAlign.prepare (kMaxChannels, latency);
}

void BitBitAlpineProcessor::releaseResources()
{
    bypassAlign.reset();
    artifact.reset();
    modulation.reset();
    delay.reset();
    reverb.reset();
}

double BitBitAlpineProcessor::getTailLengthSeconds() const
{
    // In series, so they add: the delay's repeats feed the reverb, which then
    // has its own tail to ring out afterwards.
    return delay.tailSeconds() + reverb.tailSeconds();
}

bool BitBitAlpineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // A stereo-in / mono-out would throw half the signal away.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

void BitBitAlpineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numOut == 0 || numSamples == 0)
        return;

    // Clear any output channel the input does not feed, then fan a genuine mono
    // input across them so the stereo stages have something on both sides.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numIn == 1)
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    const int numCh = juce::jmin (numOut, int { kMaxChannels });

    const double bpm = readPlayHeadBpm();
    pushSettings (bpm);

    // What the Delay module's scope animates from, taken off the untouched
    // input before anything below writes over the buffer: whether a note is
    // being played, not what the plugin is doing with it. The Input trim is
    // deliberately not in front of this.
    inputMeter.process (buffer.getReadPointer (0), numIn > 1 ? buffer.getReadPointer (1) : nullptr, numSamples);
    tuner.process (buffer.getReadPointer (0), numIn > 1 ? buffer.getReadPointer (1) : nullptr, numSamples);

    if (numSamples > dryBuffer.getNumSamples())
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    bypassAlign.process (dryBuffer, numCh, numSamples);

    inGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::inGain)->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::outGain)->load()));
    engageGain.setTargetValue (apvts.getRawParameterValue (id::on)->load() > 0.5f && ! hostBypassed ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    // The header's pre-EQ, on what the whole chain is fed. After the tuner and
    // the input meter, which read the instrument as it is played, and not on
    // dryBuffer, so the global bypass is the untouched input still.
    equaliser.process (buffer.getArrayOfWritePointers(), numCh, numSamples);
    eqSpectrum.process (buffer.getReadPointer (0), numCh > 1 ? buffer.getReadPointer (1) : nullptr, numSamples);

    // The Modulation module's two tempo-locked LFOs, kept on the host grid when
    // their Sync switches are on and the transport is rolling - see
    // ee::fx::modulation::HostSync, which BitBit Modulation drives the same way.
    // The other modules carry their own sync internally.
    modSync.process (modulation, getPlayHead(), apvts.getRawParameterValue (id::modFilterTime)->load(),
                     apvts.getRawParameterValue (id::modFilterSync)->load() > 0.5f,
                     apvts.getRawParameterValue (id::modTremRate)->load(),
                     apvts.getRawParameterValue (id::modTremSync)->load() > 0.5f, bpm, sr, numSamples);

    // The chain. chain.order picks who's fed to whom, in the order it names -
    // each module is still responsible for its own dry/wet and its own power
    // toggle, all this does is hand the signal on. See ChainOrder.h. A new
    // order is not switched to on the spot: see runChainWithReorderFade.
#if EE_ALPINE_WATCHDOG
    watchdog.beginBlock();
    watchdog.setStagePeak (AlpineWatchdog::stageInput, buffer, numCh, numSamples);
#endif

    runChainWithReorderFade (buffer, numCh, numSamples);

    // The Modulation Filter engine's live sweep position, for the editor's scope.
    filterModL.store (modulation.filterModL(), std::memory_order_relaxed);
    filterModR.store (modulation.filterModR(), std::memory_order_relaxed);

    // The global bypass crossfades back to the input as it was before the
    // Input trim, so a bypassed plugin is unity whatever either trim says.
    ee::plugin::crossfadeToDry (buffer, dryBuffer, engageGain, numCh, numSamples, &outGain);

    // The tuner's mute, after everything - bypassed or not, a muted tuner is
    // silent. Untouched unless it is muting or ramping, so the ordinary path
    // is not even multiplied by one.
    tunerMuteGain.setTargetValue (tunerMuted() ? 0.0f : 1.0f);
    if (tunerMuteGain.isSmoothing() || tunerMuteGain.getTargetValue() < 1.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = tunerMuteGain.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }

    // The last line of defence - on the finished output, every block, every
    // build. See sanitizeOutput.
    const SafetyVerdict verdict = sanitizeOutput (buffer, numCh, numSamples);

#if EE_ALPINE_WATCHDOG
    watchdog.endBlock (verdict.peak, verdict.nonFinite, verdict.clamped, verdict.didReset, verdict.firstBadBlock,
                       getPlayHead());
#else
    juce::ignoreUnused (verdict);
#endif
}

void BitBitAlpineProcessor::runChain (juce::AudioBuffer<float>& block,
                                      int numCh,
                                      int numSamples,
                                      const float* dip) noexcept
{
    const auto applyDip = [&block, numCh, numSamples, dip]
    {
        if (dip != nullptr)
            for (int ch = 0; ch < numCh; ++ch)
                juce::FloatVectorOperations::multiply (block.getWritePointer (ch), dip, numSamples);
    };

    for (const int moduleId : chainOrder::permutationForIndex (activeChainOrder))
    {
        applyDip();

        switch (moduleId)
        {
        case chainOrder::moduleArtifact:
            artifact.process (block, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageArtifact, block, numCh, numSamples);
#endif
            break;

        case chainOrder::moduleModulation:
            modulation.process (block, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageModulation, block, numCh, numSamples);
#endif
            break;

        case chainOrder::moduleDelay:
            delay.process (block, numCh, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageDelay, block, numCh, numSamples);
#endif
            break;

        default: // chainOrder::moduleReverb
            reverb.process (block, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageReverb, block, numCh, numSamples);
#endif
            break;
        }
    }

    applyDip();
}

void BitBitAlpineProcessor::runChainWithReorderFade (juce::AudioBuffer<float>& buffer,
                                                     int numCh,
                                                     int numSamples) noexcept
{
    const auto wantedOrder = [this]
    {
        return juce::jlimit (0, chainOrder::kNumPermutations - 1,
                             static_cast<int> (apvts.getRawParameterValue (id::chainOrder)->load()));
    };

    // Nothing to hide a swap from on the first block after prepare, or while
    // the global bypass has the output entirely on the dry path: take the new
    // order outright. The first is what keeps a session that opens on a
    // reordered chain playing it from sample one.
    const bool unheard = engageGain.getCurrentValue() == 0.0f && ! engageGain.isSmoothing();

    if (! chainOrderPrimed || unheard)
    {
        activeChainOrder = wantedOrder();
        chainFadeLeft = 0;
        chainFadingIn = false;
        chainOrderPrimed = true;
    }
    else if (chainFadeLeft == 0 && wantedOrder() != activeChainOrder)
    {
        chainFadeLeft = chainFadeLength;
        chainFadingIn = false;
    }

    // At rest this is one pass over the whole block - the same calls, with the
    // same arguments, as before the fade existed. Only a block a fade starts,
    // turns or ends in is split, so the swap lands on its exact sample.
    for (int offset = 0; offset < numSamples;)
    {
        const int len = chainFadeLeft > 0 ? juce::jmin (numSamples - offset, chainFadeLeft) : numSamples - offset;

        juce::AudioBuffer<float> segment (buffer.getArrayOfWritePointers(), numCh, offset, len);

        if (chainFadeLeft > 0)
        {
            // Raised cosine, out to silence and back up: the two orders are
            // never heard at once, because the modules carry one set of state
            // and cannot run both.
            const int done = chainFadeLength - chainFadeLeft;
            const float step = juce::MathConstants<float>::pi / static_cast<float> (chainFadeLength);

            for (int i = 0; i < len; ++i)
            {
                const float c = std::cos (static_cast<float> (done + i + 1) * step);
                chainDip[static_cast<size_t> (i)] = chainFadingIn ? 0.5f - 0.5f * c : 0.5f + 0.5f * c;
            }

            runChain (segment, numCh, len, chainDip.data());

            chainFadeLeft -= len;

            if (chainFadeLeft == 0)
            {
                chainFadingIn = ! chainFadingIn;

                // Silent now: swap to whatever the parameter says at this
                // moment - a drag that moved again mid-fade lands on where it
                // ended, not where it started - and come back up.
                if (chainFadingIn)
                {
                    activeChainOrder = wantedOrder();
                    chainFadeLeft = chainFadeLength;
                }
            }
        }
        else
        {
            runChain (segment, numCh, len, nullptr);
        }

        offset += len;
    }
}

BitBitAlpineProcessor::SafetyVerdict
BitBitAlpineProcessor::sanitizeOutput (juce::AudioBuffer<float>& buffer, int numCh, int numSamples) noexcept
{
    bool nonFinite = false;
    bool clamped = false;
    float peak = 0.0f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* d = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = d[i];

            if (! std::isfinite (x))
            {
                x = 0.0f;
                d[i] = 0.0f;
                nonFinite = true;
            }
            else if (x > kSafetyCeiling)
            {
                x = kSafetyCeiling;
                d[i] = kSafetyCeiling;
                clamped = true;
            }
            else if (x < -kSafetyCeiling)
            {
                x = -kSafetyCeiling;
                d[i] = -kSafetyCeiling;
                clamped = true;
            }

            peak = juce::jmax (peak, std::abs (x));
        }
    }

    lastOutputPeak.store (peak, std::memory_order_relaxed);

    SafetyVerdict verdict;
    verdict.nonFinite = nonFinite;
    verdict.clamped = clamped;
    verdict.peak = peak;

    if (nonFinite || clamped)
    {
        verdict.firstBadBlock = badBlockRun == 0;
        ++badBlockRun;
        safetyTrips.fetch_add (1, std::memory_order_relaxed);

        if (badBlockRun >= safetyResetHoldBlocks)
        {
            // A run this long is not a transient - something has latched. Flush
            // every module's state and give the host one clear block rather
            // than a brickwalled roar.
            equaliser.reset();
            artifact.reset();
            modulation.reset();
            delay.reset();
            reverb.reset();
            buffer.clear();

            badBlockRun = 0;
            verdict.didReset = true;
            verdict.peak = 0.0f;
            safetyResets.fetch_add (1, std::memory_order_relaxed);
        }
    }
    else
    {
        badBlockRun = 0;
    }

    return verdict;
}

void BitBitAlpineProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Audio thread only, so no atomic: it is set and cleared around the one
    // call that reads it.
    hostBypassed = true;
    processBlock (buffer, midi);
    hostBypassed = false;
}

juce::AudioProcessorEditor* BitBitAlpineProcessor::createEditor()
{
    return new BitBitAlpineWebEditor (*this);
}

void BitBitAlpineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = ee::plugin::copyVersionedState (apvts).createXml())
        copyXmlToBinary (*xml, destData);
}

void BitBitAlpineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = ee::plugin::xmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            installState (ee::plugin::sanitisedState (juce::ValueTree::fromXml (*xml), apvts));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitBitAlpineProcessor();
}
