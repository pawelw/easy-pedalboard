#include "PluginProcessor.h"

#include "ChainOrder.h"
#include "Params.h"
#include "PeakAlpineWebEditor.h"

#include "ee/dsp/AutoWahConfig.h"
#include "ee/dsp/BitCrusherConfig.h"
#include "ee/dsp/ChorusConfig.h"
#include "ee/dsp/PhaserConfig.h"
#include "ee/dsp/RateMap.h"
#include "ee/dsp/RingModulatorConfig.h"
#include "ee/dsp/RustConfig.h"
#include "ee/dsp/SpringConfig.h"
#include "ee/dsp/TapeMachineConfig.h"
#include "ee/dsp/Tremolo.h"
#include "ee/dsp/TremoloConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"

#include "ee/fx/DelayTimeMap.h"

#include "TapeAssets.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iterator>

namespace
{
using namespace ee::alpine;
using ee::plugin::kRampSeconds;
using ee::plugin::percentToText;

// The three modules restate this rather than including ee/plugin - see
// ee/fx/DelayModuleConfig.h. This is the one place all of them are visible.
static_assert (ee::fx::delaymodule::kGainRampSeconds == kRampSeconds,
               "ee::fx::delaymodule::kGainRampSeconds must track ee::plugin::kRampSeconds");
static_assert (ee::fx::MultiEngineModule::kGainRampSeconds == kRampSeconds,
               "MultiEngineModule::kGainRampSeconds must track ee::plugin::kRampSeconds");

/** The Rate knob's map, shared with Peak Trem & Pan through the tremolo's own
    voicing header so the same knob position means the same rate on both. */
constexpr ee::dsp::RateMap kTremRateMap { ee::dsp::tremolo::kRateMinPeriodMs, ee::dsp::tremolo::kRateMaxPeriodMs,
                                          ee::dsp::tremolo::kRateSkewCentreMs };

// ------------------------------------------------------------------- artifact
// The Artifact module's Filter Time knob: one LFO cycle from 30 ms (knob down)
// to 3 s (knob up) - a filter wobble, the same travel Peak Wah's Time knob has.
// This is the inverted-knob wrapper Peak Artifact's own RateMap.h is, its
// {30, 3000, 450} kept in step by hand (the same way Peak Wah keeps its copy).
// Knob down is the shortest period, so every entry flips the position before
// handing it to the shared map.
constexpr ee::dsp::RateMap kArtFilterRateMap { 30.0f, 3000.0f, 450.0f };

float artInvert (float rate01) noexcept
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, rate01);
}

float artRateToPeriodSeconds (float rate01, bool synced, double bpm) noexcept
{
    return kArtFilterRateMap.rateToPeriodSeconds (artInvert (rate01), synced, bpm);
}

float artSyncedDivisionBeats (float rate01) noexcept
{
    return ee::dsp::RateMap::syncedDivisionBeats (artInvert (rate01));
}

juce::String artRateToText (float rate01, bool synced)
{
    return kArtFilterRateMap.rateToText (artInvert (rate01), synced);
}

/** The Artifact Filter's Freq knob -> Hz, log spaced with the low end given
    more travel (kFreqKnobSkew < 1). Peak Artifact's freqHzFor, verbatim. */
float artFreqHzFor (float pct) noexcept
{
    const float t = std::pow (juce::jlimit (0.0f, 1.0f, pct * 0.01f), ee::dsp::autowah::kFreqKnobSkew);
    return ee::dsp::autowah::kFreqMinHz * std::pow (ee::dsp::autowah::kFreqMaxHz / ee::dsp::autowah::kFreqMinHz, t);
}

/** The Artifact Filter's Freq readout: a bare rounded "440 Hz", no kHz. Kept
    separate from this file's `hertzToText` on purpose - that one folds to kHz
    above 1000, and Peak Artifact's own readout does not (see CLAUDE.md on
    formatters that share a name but not a behaviour). */
juce::String artHzToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " Hz";
}

/** The <> wave picker's three positions as ee::dsp::lfoValue shape morphs -
    triangle mid-morph, ramp a quarter down, hard square at the top. In step
    with @synthpeak/artifact-face's WAVES table and Peak Artifact's
    kWaveShape01. */
constexpr float kArtWaveShape01[] = { 0.50f, 0.25f, 1.00f };

float artWaveShape01 (int waveIndex) noexcept
{
    const int last = static_cast<int> (std::size (kArtWaveShape01)) - 1;
    return kArtWaveShape01[juce::jlimit (0, last, waveIndex)];
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

/** The Artifact Rust engine's Tone readout, off the ee::dsp::rust map
    ee::fx::ArtifactModule reads - a bare rounded "Hz" (this file's house style),
    "Off" past the bypass point. Grind stays a plain percent. This is the Oxide
    reading; Contact scales the knob down before the map, darker than shown. */
juce::String artRustToneToText (float pct, int)
{
    const float hz = ee::dsp::rust::toneHzFor (pct * 0.01f);
    if (hz >= ee::dsp::rust::kToneBypassHz)
        return "Off";
    return artHzToText (hz, 0);
}

// The Input/Output trims' range. Asymmetric on purpose, exactly as Peak
// Delay's are: they are a trim and a level, not a gain stage, so there is more
// cut than boost.
constexpr float kMinGainDb = -24.0f;
constexpr float kMaxGainDb = 12.0f;

// A module's own output trim. Symmetric on purpose, unlike the host's In/Out
// pair above: this one rests at unity and the face draws its arc out from the
// middle, so the two halves have to be the same size or "no change" would not
// be at twelve o'clock. It also stops short of silence at the bottom, which
// the old 0..100 % level did not - and since the three modules are in series,
// a Level anyone could wind to zero was a knob that silenced the whole plugin.
// Mix and the module's own power toggle are how you take a module out.
constexpr float kModuleTrimDb = 12.0f;

constexpr int kDefaultDivision = 5; // 1/8
constexpr float kDefaultTime01 = ee::peakdelay::time01ForDivision (kDefaultDivision);

juce::String secondsToText (float value, int)
{
    return value < 1.0f ? juce::String (juce::roundToInt (value * 1000.0f)) + " ms" : juce::String (value, 2) + " s";
}

juce::String hertzToText (float value, int)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (value)) + " Hz";
}

juce::String gainDbToText (float value, int)
{
    return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
}

juce::String degreesToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
}

/** Peak Tape's Tone readout: bipolar, rests at 0, the sign carries the
    direction. Kept in step with plugins/peak-tape's own toneToText. */
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

PeakAlpineProcessor::PeakAlpineProcessor()
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

    loadTapeNoiseSample();
}

PeakAlpineProcessor::~PeakAlpineProcessor()
{
    apvts.removeParameterListener (id::dlyLeftTime, this);
    apvts.removeParameterListener (id::dlyRightTime, this);
    apvts.removeParameterListener (id::dlySync, this);
}

/** Decodes the embedded tape floor once, at construction, exactly as
    PeakTapeProcessor does. The Modulation module's tape engine loops it from
    these samples, so the buffer has to outlive every process call - it is a
    member, and the pointers handed over are into it. */
void PeakAlpineProcessor::loadTapeNoiseSample()
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
void PeakAlpineProcessor::mirrorTime (const juce::String& from, const juce::String& to)
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
    PeakDelayProcessor::installState. */
void PeakAlpineProcessor::installState (const juce::ValueTree& tree)
{
    installingState = true;
    apvts.replaceState (tree);
    installingState = false;
}

void PeakAlpineProcessor::parameterChanged (const juce::String& parameterID, float newValue)
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

juce::AudioProcessorValueTreeState::ParameterLayout PeakAlpineProcessor::createParameterLayout()
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
    // Every id, range and default is Peak Artifact's, because the module is
    // Peak Artifact's. All four engines are voiced (see ee::fx::ArtifactModule).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::artOn, 1 }, "Artifact On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::artEngine, 1 }, "Artifact Engine",
        juce::StringArray { "Ring Mod", "Bit Crush", "Filter", "Rust" }, 2));
    addTrimDb (layout, id::artLevel, "Artifact Level");
    addPercent (layout, id::artMix, "Artifact Mix", 50.0f);

    // Freq prints as a bare rounded "Hz" - Peak Artifact's own readout, not
    // this file's kHz-folding hertzToText - so it is spelled out rather than
    // going through addPercent.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artFltFreq, 1 }, "Artifact Freq", percent, ee::dsp::autowah::kDefaultFreqPct,
        withText ([] (float v, int) { return artHzToText (artFreqHzFor (v), 0); })));

    addPercent (layout, id::artFltQ, "Artifact Q", ee::dsp::autowah::kDefaultQPct);
    addPercent (layout, id::artFltRange, "Artifact Range", ee::dsp::autowah::kDefaultRangePct);

    // One normalised knob; the Sync switch decides what it means. Knob down =
    // fastest in both modes (see kArtFilterRateMap). Host text is the synced
    // reading; the editor overrides it live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artFltTime, 1 }, "Artifact Time", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f),
        0.5f, withText ([] (float v, int) { return artRateToText (v, true); })));

    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::artFltSync, 1 }, "Artifact Sync", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::artFltWave, 1 }, "Artifact Wave",
                                                              juce::StringArray { "Triangle", "Ramp", "Square" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::artFltStereo, 1 },
                                                            "Artifact Stereo", false));

    // Bit Crush knobs, 0..100 like the Filter's - percent host text, resolved to
    // real units by ee::fx::ArtifactModule through the ee::dsp::bitcrush maps.
    addPercent (layout, id::artCrushBits, "Artifact Bits", ee::dsp::bitcrush::kDefaultBitsPct);
    addPercent (layout, id::artCrushRate, "Artifact Rate", ee::dsp::bitcrush::kDefaultRatePct);
    addPercent (layout, id::artCrushLp, "Artifact Crush Filter", ee::dsp::bitcrush::kDefaultLpPct);
    addPercent (layout, id::artCrushJitter, "Artifact Jitter", ee::dsp::bitcrush::kDefaultJitterPct);

    // Ring Mod. Freq and Filter spell out real Hz off the ee::dsp::ringmod maps
    // (like the Filter's Freq); Tweak is a plain percent, its meaning set by
    // Mode. Blend is Artifact Mix, not a knob of its own.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artRingFreq, 1 }, "Artifact Ring Freq", percent, ee::dsp::ringmod::kDefaultFreqPct,
        withText (artRingFreqToText)));
    addPercent (layout, id::artRingTweak, "Artifact Tweak", ee::dsp::ringmod::kDefaultTweakPct);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artRingLp, 1 }, "Artifact Ring Filter", percent, ee::dsp::ringmod::kDefaultLpPct,
        withText (artRingLpToText)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::artRingMode, 1 }, "Artifact Mode",
                                                              juce::StringArray { "Wobble", "Octave" }, 0));

    // Rust. Two knobs - Grind (plain percent) and Tone (real units off the
    // ee::dsp::rust map). Wear and its recovery are fixed inside the engine.
    // Blend is Artifact Mix, not a knob of its own.
    addPercent (layout, id::artRustGrind, "Artifact Grind", ee::dsp::rust::kDefaultGrindPct);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::artRustTone, 1 }, "Artifact Rust Tone", percent, ee::dsp::rust::kDefaultTonePct,
        withText (artRustToneToText)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::artRustMode, 1 }, "Artifact Rust Mode", juce::StringArray { "Oxide", "Contact" }, 0));

    // ------------------------------------------------------------- modulation
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modOn, 1 }, "Modulation On", true));
    layout.add (
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::modEngine, 1 }, "Modulation Engine",
                                                      juce::StringArray { "Tape", "Tremolo", "Chorus", "Phaser" }, 1));
    addTrimDb (layout, id::modLevel, "Modulation Level");
    addPercent (layout, id::modMix, "Modulation Mix", 40.0f);

    // Ranges and defaults are each engine's own pedal's, so the same knob
    // position sounds the same in both places.
    addPercent (layout, id::modTapeSat, "Tape Saturation", ee::dsp::tape::kDefaultSaturationPct);
    addPercent (layout, id::modTapeFlutter, "Tape Flutter", ee::dsp::tape::kDefaultFlutterPct);
    addPercent (layout, id::modTapeWear, "Tape Wear", ee::dsp::tape::kDefaultWearPct);
    addPercent (layout, id::modTapeNoise, "Tape Noise", ee::dsp::tape::kDefaultNoisePct);

    // Tone and Stereo, the two Tape controls the first cut of the face left at
    // their defaults - now on it, so this engine is the whole of Peak Tape.
    // Tone is the same bipolar tilt: -100 dark, 0 flat and bypassed, +100
    // bright, resting in the middle.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modTapeTone, 1 }, "Tape Tone",
                                                             juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
                                                             ee::dsp::tape::kDefaultTonePct, withText (toneToText)));

    // Mono is one transport under both channels; Stereo opens them onto
    // different points of a slow modulation. On by default, as Peak Tape's is.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modTapeStereo, 1 }, "Tape Stereo",
                                                            ee::dsp::tape::kDefaultStereoOn));

    addPercent (layout, id::modTremAmount, "Tremolo Amount", 50.0f);
    // A plain 0..1 knob, like Peak Trem & Pan's: what a position means depends
    // on the Tremolo Sync switch - a free LFO period in ms, or a tempo-locked
    // note division. Host text is the synced reading (a host has no pill to say
    // which mode the knob is in) and the editor overrides it live, the same
    // choice the Artifact Time knob and Peak Trem & Pan make.
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

    // ------------------------------------------------------------------ delay
    // Every id, range and default is Peak Delay's, because the module behind
    // them is Peak Delay's chain. The leaf names match that pedal's exactly,
    // which is what lets one DelayFace component drive both faces off a prefix.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlyOn, 1 }, "Delay On", true));

    // The host's own text for a Time knob is the synced reading at the
    // reference tempo - the same choice Peak Delay makes, for the same reason:
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
                [] (float v, int) { return ee::peakdelay::timeMap().toText (v, true, ee::peakdelay::kReferenceBpm); })
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

    // Ranges, skews and defaults are Peak EQ's, down to the skew centres: the
    // same two cuts, so the same knob position should mean the same corner.
    auto loCutRange = juce::NormalisableRange<float> (kLoCutMinHz, kLoCutMaxHz);
    loCutRange.setSkewForCentre (120.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyLoCut, 1 }, "Delay Low Cut",
                                                             loCutRange, kLoCutMinHz, withText (hertzToText)));

    auto hiCutRange = juce::NormalisableRange<float> (kHiCutMinHz, kHiCutMaxHz);
    hiCutRange.setSkewForCentre (4000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyHiCut, 1 }, "Delay High Cut",
                                                             hiCutRange, kHiCutMaxHz, withText (hertzToText)));

    // false = Post. No control on the face reaches this (see DelayFace's
    // tapeRouter) - Peak Alpine's Mod module is where a tape machine is chosen.
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlyTapePre, 1 }, "Tape Placement", false));

    // ----------------------------------------------------------------- reverb
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::revOn, 1 }, "Reverb On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::revEngine, 1 }, "Reverb Engine",
                                                              juce::StringArray { "Space", "Spring" }, 0));
    addTrimDb (layout, id::revLevel, "Reverb Level");
    addPercent (layout, id::revMix, "Reverb Mix", 30.0f);

    auto spaceDecay = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay);
    spaceDecay.setSkewForCentre (2.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpaceDecay, 1 }, "Space Decay",
                                                             spaceDecay, 2.0f, withText (secondsToText)));
    addPercent (layout, id::revSpaceShimmer, "Space Shimmer", 0.0f);

    auto spaceLoCut =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinLowCutHz, ee::dsp::FdnReverb::kMaxLowCutHz);
    spaceLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpaceLoCut, 1 },
                                                             "Space Low Cut", spaceLoCut,
                                                             ee::dsp::FdnReverb::kMinLowCutHz, withText (hertzToText)));
    addPercent (layout, id::revSpaceReso, "Space Reso", 50.0f);

    auto springDecay =
        juce::NormalisableRange<float> (ee::dsp::spring::kMinDecaySeconds, ee::dsp::spring::kMaxDecaySeconds);
    springDecay.setSkewForCentre (ee::dsp::spring::kDecaySkewCentre);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::revSpringDecay, 1 }, "Spring Decay", springDecay, ee::dsp::spring::kDefaultDecaySeconds,
        withText (secondsToText)));
    addPercent (layout, id::revSpringTension, "Spring Tension", ee::dsp::spring::kDefaultTension01 * 100.0f);

    // The same travel Space's Low Cut has, deliberately - see SpringConfig.h.
    auto springLoCut = juce::NormalisableRange<float> (ee::dsp::spring::kMinLowCutHz, ee::dsp::spring::kMaxLowCutHz);
    springLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::revSpringLoCut, 1 },
                                                             "Spring Low Cut", springLoCut,
                                                             ee::dsp::spring::kOutputLowCutHz, withText (hertzToText)));

    // The signal-chain order, appended rather than slotted into its own
    // section: AU addresses parameters by index, not name, so inserting this
    // one earlier would shift the index of every parameter after it. Index 0 - its
    // default - is Artifact, Modulation, Delay, Reverb: today's fixed order,
    // so a session or preset that predates this parameter is unaffected.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::chainOrder, 1 }, "Chain Order",
                                                              chainOrderLabels(), 0));

    return layout;
}

// ---------------------------------------------------------------- the readouts

double PeakAlpineProcessor::readPlayHeadBpm()
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

double PeakAlpineProcessor::currentBpm() const
{
    const double bpm = lastKnownBpm.load (std::memory_order_relaxed);
    return std::isfinite (bpm) ? juce::jlimit (20.0, 300.0, bpm) : 120.0;
}

bool PeakAlpineProcessor::delayIsSynced() const
{
    return apvts.getRawParameterValue (id::dlyTimeUnit)->load() <= 0.5f;
}

float PeakAlpineProcessor::timeMs (const char* parameterId) const
{
    const float time01 = apvts.getRawParameterValue (parameterId)->load();
    return ee::peakdelay::timeMap().value (time01, delayIsSynced(), currentBpm());
}

juce::String PeakAlpineProcessor::timeReadout (const char* parameterId) const
{
    const float time01 = apvts.getRawParameterValue (parameterId)->load();
    return ee::peakdelay::timeMap().toText (time01, delayIsSynced(), currentBpm());
}

/** The millisecond reading regardless of the unit toggle - the face's Readout
    shows this as a small constant figure beside the toggle-aware one. */
juce::String PeakAlpineProcessor::timeMsReadout (const char* parameterId) const
{
    const float time01 = apvts.getRawParameterValue (parameterId)->load();
    return ee::peakdelay::timeMap().toText (time01, false, currentBpm());
}

juce::String PeakAlpineProcessor::artifactTimeReadout() const
{
    const float time01 = apvts.getRawParameterValue (id::artFltTime)->load();
    const bool synced = apvts.getRawParameterValue (id::artFltSync)->load() > 0.5f;
    return artRateToText (time01, synced);
}

juce::String PeakAlpineProcessor::tremoloRateReadout() const
{
    const float rate01 = apvts.getRawParameterValue (id::modTremRate)->load();
    const bool synced = apvts.getRawParameterValue (id::modTremSync)->load() > 0.5f;
    return kTremRateMap.rateToText (rate01, synced);
}

// ------------------------------------------------------------------- settings

void PeakAlpineProcessor::pushSettings (double bpm) noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };
    const auto flag = [&raw] (const char* pid) { return raw (pid) > 0.5f; };

    // ----------------------------------------------------------------- artifact
    artifact.setEngine (static_cast<int> (raw (id::artEngine)));
    artifact.setEngaged (flag (id::artOn));
    artifact.setLevel (juce::Decibels::decibelsToGain (raw (id::artLevel)));
    artifact.setMix01 (pct (id::artMix));

    {
        const bool artSynced = flag (id::artFltSync);
        const float artPeriod = juce::jmax (1.0e-4f, artRateToPeriodSeconds (raw (id::artFltTime), artSynced, bpm));

        artifact.setFilter (pct (id::artFltFreq), pct (id::artFltQ), pct (id::artFltRange),
                            artWaveShape01 (static_cast<int> (raw (id::artFltWave))), artPeriod,
                            flag (id::artFltStereo));
    }

    artifact.setCrush (pct (id::artCrushBits), pct (id::artCrushRate), pct (id::artCrushLp), pct (id::artCrushJitter));

    artifact.setRing (pct (id::artRingFreq), pct (id::artRingTweak), pct (id::artRingLp),
                      static_cast<int> (raw (id::artRingMode)));

    artifact.setRust (pct (id::artRustGrind), pct (id::artRustTone), static_cast<int> (raw (id::artRustMode)));

    // --------------------------------------------------------------- modulation
    modulation.setEngine (static_cast<int> (raw (id::modEngine)));
    modulation.setEngaged (flag (id::modOn));
    modulation.setLevel (juce::Decibels::decibelsToGain (raw (id::modLevel)));
    modulation.setMix01 (pct (id::modMix));

    // Tone is a bipolar -100..100 knob and the engine takes -1..1; Stereo is
    // the machine's mono/stereo switch.
    modulation.setTape (pct (id::modTapeSat), pct (id::modTapeFlutter), pct (id::modTapeWear), pct (id::modTapeNoise),
                        raw (id::modTapeTone) * 0.01f, flag (id::modTapeStereo) ? 1.0f : 0.0f);

    // The Rate knob is a position, not a rate: what it means is this face's
    // map, set by the Tremolo Sync switch - a free period in ms, or a
    // tempo-locked note division. The transport that aligns a synced LFO's
    // phase to the host grid is handed over separately, from processBlock.
    const bool tremSynced = flag (id::modTremSync);
    const float tremPeriodSeconds = kTremRateMap.rateToPeriodSeconds (raw (id::modTremRate), tremSynced, bpm);
    modulation.setTremolo (pct (id::modTremAmount), tremPeriodSeconds, pct (id::modTremShape), pct (id::modTremTube));

    modulation.setChorus (raw (id::modChorusRate), pct (id::modChorusDepth), raw (id::modChorusPhase));
    modulation.setPhaser (raw (id::modPhaseRate), pct (id::modPhaseDepth));

    // -------------------------------------------------------------------- delay
    const bool synced = delayIsSynced();

    delay.setTimes (ee::peakdelay::timeSeconds (raw (id::dlyLeftTime), synced, bpm),
                    ee::peakdelay::timeSeconds (raw (id::dlyRightTime), synced, bpm));
    delay.setFeedback01 (pct (id::dlyFeedback));

    const int typeIndex = static_cast<int> (raw (id::dlyType));
    delay.setRouting (typeIndex == 1   ? ee::dsp::TapeDelay::Routing::wide
                      : typeIndex == 2 ? ee::dsp::TapeDelay::Routing::pingPong
                                       : ee::dsp::TapeDelay::Routing::normal);

    delay.setTapePost (! flag (id::dlyTapePre));
    delay.setTape (pct (id::dlyWear), pct (id::dlyFlutter));
    delay.setDrift01 (pct (id::dlyDrift));
    delay.setPhaser01 (pct (id::dlyPhaser));
    delay.setFilter (raw (id::dlyLoCut), raw (id::dlyHiCut));
    delay.setMix01 (pct (id::dlyMix));
    delay.setEngaged (flag (id::dlyOn));

    // The host owns the input trim, so the module's is left at unity. The
    // module's output trim is the Delay Level knob in the header - the same
    // +/- kModuleTrimDb trim mod.level and rev.level are, resting at 0 dB where
    // decibelsToGain is exactly 1.0f and the module is bit-identical bypassed.
    delay.setTrims (1.0f, juce::Decibels::decibelsToGain (raw (id::dlyLevel)));

    // ------------------------------------------------------------------- reverb
    reverb.setEngine (static_cast<int> (raw (id::revEngine)));
    reverb.setEngaged (flag (id::revOn));
    reverb.setLevel (juce::Decibels::decibelsToGain (raw (id::revLevel)));
    reverb.setMix01 (pct (id::revMix));

    reverb.setSpace (raw (id::revSpaceDecay), pct (id::revSpaceShimmer), raw (id::revSpaceLoCut),
                     pct (id::revSpaceReso));
    reverb.setSpring (raw (id::revSpringDecay), pct (id::revSpringTension), raw (id::revSpringLoCut));
}

// ---------------------------------------------------------------------- audio

void PeakAlpineProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    badBlockRun = 0;
    safetyResetHoldBlocks =
        juce::jlimit (4, 64, static_cast<int> (std::lround (0.025 * sampleRate / juce::jmax (1, maxBlock))));

#if EE_ALPINE_WATCHDOG
    watchdog.prepare (*this, sampleRate, maxBlock);
#endif

    // prepareToPlay is one of the callbacks where the playhead is valid, so the
    // cache starts out holding the host's real tempo rather than 120.
    pushSettings (readPlayHeadBpm());

    delay.setFilterRestingPoints (kLoCutMinHz, kHiCutMaxHz);

    artifact.prepare (sampleRate, maxBlock);
    modulation.prepare (sampleRate, maxBlock);
    delay.prepare (sampleRate, maxBlock);
    reverb.prepare (sampleRate, maxBlock);

    artHaveExpectedPpq = false;
    artWasPlaying = false;

    // Re-hand the tape floor after prepare, the same belt-and-braces
    // PeakTapeProcessor uses - the read rate is worked out from both the sample
    // and the session, and prepare has just changed the session's.
    if (! tapeNoiseChannels.empty())
        modulation.setTapeNoiseSample (tapeNoiseChannels.data(), static_cast<int> (tapeNoiseChannels.size()),
                                       tapeNoiseSample.getNumSamples(), tapeNoiseSampleRate);

    inputMeter.prepare (sampleRate);

    dryBuffer.setSize (kMaxChannels, maxBlock, false, true, true);

    inGain.reset (sampleRate, kRampSeconds);
    outGain.reset (sampleRate, kRampSeconds);
    engageGain.reset (sampleRate, kRampSeconds);

    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::inGain)->load()));
    outGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::outGain)->load()));
    engageGain.setCurrentAndTargetValue (apvts.getRawParameterValue (id::on)->load() > 0.5f ? 1.0f : 0.0f);

    // Two stages in the chain delay the signal without that being the effect:
    // the Modulation module's alignment (its Tape engine's transport, which
    // every other engine is now padded out to) and the Delay module's tape
    // section. They are in series, so they add. The Artifact module's engines
    // are all latency-free, so it contributes zero - added for the day one is
    // not.
    setLatencySamples (artifact.latencySamples() + modulation.latencySamples() + delay.latencySamples());
}

void PeakAlpineProcessor::releaseResources()
{
    artifact.reset();
    modulation.reset();
    delay.reset();
    reverb.reset();
}

double PeakAlpineProcessor::getTailLengthSeconds() const
{
    // In series, so they add: the delay's repeats feed the reverb, which then
    // has its own tail to ring out afterwards.
    return delay.tailSeconds() + reverb.tailSeconds();
}

bool PeakAlpineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void PeakAlpineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    if (numSamples > dryBuffer.getNumSamples())
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    inGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::inGain)->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::outGain)->load()));
    engageGain.setTargetValue (apvts.getRawParameterValue (id::on)->load() > 0.5f ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    // The Artifact Filter LFO free-runs; when its Sync pill is on and the
    // transport is running it is also aligned to the host grid - a hard snap on
    // the first playing block or a jump, a gentle per-block pull otherwise. The
    // same shape Peak Artifact and Peak Wah use; the other three modules carry
    // their own sync internally.
    {
        const float artTime01 = apvts.getRawParameterValue (id::artFltTime)->load();
        const bool artSynced = apvts.getRawParameterValue (id::artFltSync)->load() > 0.5f;

        double ppqStart = 0.0;
        bool havePpq = false;
        bool isPlaying = false;
        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
            {
                if (const auto ppq = position->getPpqPosition())
                {
                    ppqStart = *ppq;
                    havePpq = std::isfinite (ppqStart);
                }
                isPlaying = position->getIsPlaying();
            }

        if (artSynced && havePpq && isPlaying)
        {
            const double cyclesPerQuarter =
                1.0 / juce::jmax (1.0e-4, static_cast<double> (artSyncedDivisionBeats (artTime01)));
            const double ppqPerSample = bpm / (60.0 * sr);

            const double target = ppqStart * cyclesPerQuarter;
            const bool jumped = ! artWasPlaying || (artHaveExpectedPpq && std::abs (ppqStart - artExpectedPpq) > 0.25);

            if (jumped)
                artifact.snapFilterPhase (target);
            else
                artifact.nudgeFilterPhase (target);

            artExpectedPpq = ppqStart + numSamples * ppqPerSample;
            artHaveExpectedPpq = true;
        }
        else
        {
            artHaveExpectedPpq = false;
        }
        artWasPlaying = isPlaying;
    }

    // The Modulation module's Tremolo LFO: free-running, and when its Sync
    // switch is on and the transport is rolling, also pulled onto the host
    // grid. Peak Trem & Pan drives its engine's Transport exactly this way -
    // `synced` for the engine is the switch AND a finite ppq AND playing.
    {
        const float tremRate01 = apvts.getRawParameterValue (id::modTremRate)->load();
        const bool tremSyncSwitch = apvts.getRawParameterValue (id::modTremSync)->load() > 0.5f;

        double ppqStart = 0.0;
        bool havePpq = false;
        bool isPlaying = false;
        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
            {
                if (const auto ppq = position->getPpqPosition())
                {
                    ppqStart = *ppq;
                    havePpq = std::isfinite (ppqStart);
                }
                isPlaying = position->getIsPlaying();
            }

        ee::dsp::Tremolo::Transport transport;
        transport.synced = tremSyncSwitch && havePpq && isPlaying;
        transport.playing = isPlaying;
        transport.ppqStart = ppqStart;
        transport.cyclesPerQuarter =
            1.0 / juce::jmax (1.0e-4, static_cast<double> (ee::dsp::RateMap::syncedDivisionBeats (tremRate01)));
        transport.ppqPerSample = bpm / (60.0 * sr);
        modulation.setTremoloTransport (transport);
    }

    // The chain. chain.order picks who's fed to whom, in the order it names -
    // each module is still responsible for its own dry/wet and its own power
    // toggle, all this does is hand the signal on. See ChainOrder.h.
    const auto chainOrderNow =
        chainOrder::permutationForIndex (static_cast<int> (apvts.getRawParameterValue (id::chainOrder)->load()));

#if EE_ALPINE_WATCHDOG
    watchdog.beginBlock();
    watchdog.setStagePeak (AlpineWatchdog::stageInput, buffer, numCh, numSamples);
#endif

    for (const int moduleId : chainOrderNow)
    {
        switch (moduleId)
        {
        case chainOrder::moduleArtifact:
            artifact.process (buffer, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageArtifact, buffer, numCh, numSamples);
#endif
            break;

        case chainOrder::moduleModulation:
            modulation.process (buffer, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageModulation, buffer, numCh, numSamples);
#endif
            break;

        case chainOrder::moduleDelay:
            delay.process (buffer, numCh, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageDelay, buffer, numCh, numSamples);
#endif
            break;

        default: // chainOrder::moduleReverb
            reverb.process (buffer, numCh, numSamples);
#if EE_ALPINE_WATCHDOG
            watchdog.setStagePeak (AlpineWatchdog::stageReverb, buffer, numCh, numSamples);
#endif
            break;
        }
    }

    // The Artifact Filter engine's live sweep position, for the editor's scope.
    artifactModL.store (artifact.filterModL(), std::memory_order_relaxed);
    artifactModR.store (artifact.filterModR(), std::memory_order_relaxed);

    // The global bypass crossfades back to the input as it was before the
    // Input trim, so a bypassed plugin is unity whatever either trim says.
    ee::plugin::crossfadeToDry (buffer, dryBuffer, engageGain, numCh, numSamples, &outGain);

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

PeakAlpineProcessor::SafetyVerdict
PeakAlpineProcessor::sanitizeOutput (juce::AudioBuffer<float>& buffer, int numCh, int numSamples) noexcept
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

juce::AudioProcessorEditor* PeakAlpineProcessor::createEditor()
{
    return new PeakAlpineWebEditor (*this);
}

void PeakAlpineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakAlpineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            installState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakAlpineProcessor();
}
