#include "PluginProcessor.h"

#include "Params.h"
#include "BitBitModulationWebEditor.h"

#include "ee/dsp/AutoWahConfig.h"
#include "ee/dsp/ChorusConfig.h"
#include "ee/dsp/PhaserConfig.h"
#include "ee/dsp/TapeMachineConfig.h"
#include "ee/plugin/ParamText.h"

#include "TapeAssets.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace
{
using namespace ee::modulation;
using namespace ee::fx::modulation;
using ee::plugin::percentToText;

/** A bare rounded "440 Hz", no kHz and no decimal - the Filter Freq readout,
    the same text BitBit Alpine prints for the same knob. Not the shared hzToText,
    which keeps a decimal for the Chorus and Phaser rates (see CLAUDE.md on
    formatters that share a name but not a behaviour). */
juce::String filterFreqToText (float pct, int)
{
    return juce::String (juce::roundToInt (filterFreqHzFor (pct))) + " Hz";
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
                 const char* pid,
                 const char* name,
                 float defaultPct)
{
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid, 1 }, name, percent, defaultPct,
                                                             withText (percentToText)));
}
} // namespace

BitBitModulationProcessor::BitBitModulationProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };

    loadTapeNoiseSample();
}

BitBitModulationProcessor::~BitBitModulationProcessor() = default;

/** Decodes the embedded tape floor once, at construction, exactly as
    BitBitTapeProcessor and BitBitAlpineProcessor do. */
void BitBitModulationProcessor::loadTapeNoiseSample()
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

    module.setTapeNoiseSample (tapeNoiseChannels.data(), numChannels, numSamples, tapeNoiseSampleRate);
}

juce::AudioProcessorValueTreeState::ParameterLayout BitBitModulationProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Every id, range and default is BitBit Alpine's Modulation module's, minus
    // the `mod.` - the same module, so the same knob position sounds the same.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::on, 1 }, "On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::engine, 1 }, "Engine",
        juce::StringArray { "Tape", "Tremolo", "Chorus", "Phaser", "Filter" }, 1));
    addPercent (layout, id::mix, "Mix", 40.0f);

    // Tape. Ranges and defaults are BitBit Tape's.
    addPercent (layout, id::tapeSat, "Tape Saturation", ee::dsp::tape::kDefaultSaturationPct);
    addPercent (layout, id::tapeFlutter, "Tape Flutter", ee::dsp::tape::kDefaultFlutterPct);
    addPercent (layout, id::tapeWear, "Tape Wear", ee::dsp::tape::kDefaultWearPct);
    addPercent (layout, id::tapeNoise, "Tape Noise", ee::dsp::tape::kDefaultNoisePct);

    // -100 dark, 0 flat and bypassed, +100 bright, resting in the middle.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::tapeTone, 1 }, "Tape Tone",
                                                             juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
                                                             ee::dsp::tape::kDefaultTonePct, withText (toneToText)));

    // Mono is one transport under both channels; Stereo opens them onto
    // different points of a slow modulation. On by default, as BitBit Tape's is.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::tapeStereo, 1 }, "Tape Stereo",
                                                            ee::dsp::tape::kDefaultStereoOn));

    // Tremolo. Rate is a plain 0..1 knob, like BitBit Trem & Pan's: what a
    // position means depends on the Sync switch. Host text is the synced
    // reading (a host has no pill to say which mode the knob is in) and the
    // editor overrides it live.
    addPercent (layout, id::tremAmount, "Tremolo Amount", 50.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::tremRate, 1 }, "Tremolo Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        withText ([] (float v, int) { return kTremRateMap.rateToText (v, true); })));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::tremSync, 1 }, "Tremolo Sync", false));
    addPercent (layout, id::tremShape, "Tremolo Shape", 50.0f);
    addPercent (layout, id::tremTube, "Tremolo Tube", 0.0f);

    // Chorus and Phaser. BitBit Chorus's and BitBit Phase's ranges and defaults.
    auto chorusRate = juce::NormalisableRange<float> (ee::dsp::config::kRateMinHz, ee::dsp::config::kRateMaxHz);
    chorusRate.setSkewForCentre (1.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::chorusRate, 1 }, "Chorus Rate",
                                                             chorusRate, ee::dsp::config::kDefaultRateHz,
                                                             withText (ee::plugin::hzToText)));
    addPercent (layout, id::chorusDepth, "Chorus Depth", ee::dsp::config::kDefaultDepthPct);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::chorusPhase, 1 }, "Chorus Phase",
        juce::NormalisableRange<float> (0.0f, ee::dsp::config::kMaxPhaseDeg, 1.0f), ee::dsp::config::kDefaultPhaseDeg,
        withText (degreesToText)));

    auto phaserRate = juce::NormalisableRange<float> (ee::dsp::phaser::kRateMinHz, ee::dsp::phaser::kRateMaxHz);
    phaserRate.setSkewForCentre (1.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::phaseRate, 1 }, "Phaser Rate",
                                                             phaserRate, ee::dsp::phaser::kDefaultRateHz,
                                                             withText (ee::plugin::hzToText)));
    addPercent (layout, id::phaseDepth, "Phaser Depth", ee::dsp::phaser::kDefaultDepthPct);

    // Filter. Freq prints a bare rounded "Hz", so it is spelled out rather than
    // going through addPercent.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::filterFreq, 1 }, "Filter Freq",
                                                             percent, ee::dsp::autowah::kDefaultFreqPct,
                                                             withText (filterFreqToText)));
    addPercent (layout, id::filterQ, "Filter Q", ee::dsp::autowah::kDefaultQPct);
    addPercent (layout, id::filterRange, "Filter Range", ee::dsp::autowah::kDefaultRangePct);

    // One normalised knob; the Sync switch decides what it means. Knob down =
    // fastest in both modes (see kFilterRateMap). Host text is the synced
    // reading; the editor overrides it live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::filterTime, 1 }, "Filter Time", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f),
        0.5f, withText ([] (float v, int) { return filterRateToText (v, true); })));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::filterSync, 1 }, "Filter Sync", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::filterWave, 1 }, "Filter Wave",
                                                              juce::StringArray { "Triangle", "Ramp", "Square" }, 0));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::filterStereo, 1 }, "Filter Stereo", false));

    return layout;
}

// ---------------------------------------------------------------- the readouts

juce::String BitBitModulationProcessor::filterTimeReadout() const
{
    const float time01 = apvts.getRawParameterValue (id::filterTime)->load();
    const bool synced = apvts.getRawParameterValue (id::filterSync)->load() > 0.5f;
    return filterRateToText (time01, synced);
}

juce::String BitBitModulationProcessor::tremoloRateReadout() const
{
    const float rate01 = apvts.getRawParameterValue (id::tremRate)->load();
    const bool synced = apvts.getRawParameterValue (id::tremSync)->load() > 0.5f;
    return kTremRateMap.rateToText (rate01, synced);
}

// ------------------------------------------------------------------- settings

void BitBitModulationProcessor::pushSettings (double blockBpm) noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };
    const auto flag = [&raw] (const char* pid) { return raw (pid) > 0.5f; };

    module.setEngine (static_cast<int> (raw (id::engine)));
    module.setEngaged (flag (id::on));
    module.setMix01 (pct (id::mix));

    // Tone is a bipolar -100..100 knob and the engine takes -1..1; Stereo is
    // the machine's mono/stereo switch.
    module.setTape (pct (id::tapeSat), pct (id::tapeFlutter), pct (id::tapeWear), pct (id::tapeNoise),
                    raw (id::tapeTone) * 0.01f, flag (id::tapeStereo) ? 1.0f : 0.0f);

    // The Rate knob is a position, not a rate - see kTremRateMap. The transport
    // that aligns a synced LFO's phase to the host grid is handed over
    // separately, from processBlock.
    const float tremPeriodSeconds =
        kTremRateMap.rateToPeriodSeconds (raw (id::tremRate), flag (id::tremSync), blockBpm);
    module.setTremolo (pct (id::tremAmount), tremPeriodSeconds, pct (id::tremShape), pct (id::tremTube));

    module.setChorus (raw (id::chorusRate), pct (id::chorusDepth), raw (id::chorusPhase));
    module.setPhaser (raw (id::phaseRate), pct (id::phaseDepth));

    const float filterPeriod =
        juce::jmax (1.0e-4f, filterRateToPeriodSeconds (raw (id::filterTime), flag (id::filterSync), blockBpm));
    module.setFilter (pct (id::filterFreq), pct (id::filterQ), pct (id::filterRange),
                      filterWaveShape01 (static_cast<int> (raw (id::filterWave))), filterPeriod,
                      flag (id::filterStereo));
}

void BitBitModulationProcessor::installState (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
}

double BitBitModulationProcessor::readPlayHeadBpm()
{
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpm = position->getBpm())
                if (std::isfinite (*hostBpm))
                    bpm = *hostBpm;

    bpm = juce::jlimit (20.0, 300.0, bpm);
    return bpm;
}

// ---------------------------------------------------------------------- audio

void BitBitModulationProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    pushSettings (readPlayHeadBpm());
    module.prepare (sampleRate, maxBlock);
    hostSync.reset();

    // Re-hand the tape floor after prepare, the same belt-and-braces
    // BitBitTapeProcessor uses - the read rate is worked out from both the sample
    // and the session, and prepare has just changed the session's.
    if (! tapeNoiseChannels.empty())
        module.setTapeNoiseSample (tapeNoiseChannels.data(), static_cast<int> (tapeNoiseChannels.size()),
                                   tapeNoiseSample.getNumSamples(), tapeNoiseSampleRate);

    // The Tape engine reads off a transport line, and the module pads every
    // other engine and its dry path out to match - one figure, whichever engine
    // is selected.
    setLatencySamples (module.latencySamples());
}

void BitBitModulationProcessor::releaseResources()
{
    module.reset();
}

bool BitBitModulationProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // Stereo in / mono out would fold the stereo engines back together.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

void BitBitModulationProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numOut == 0 || numSamples == 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numIn == 1)
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    const int numCh = juce::jmin (numOut, int { kMaxChannels });
    if (numCh == 0)
        return;

    const double blockBpm = readPlayHeadBpm();
    pushSettings (blockBpm);

    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    hostSync.process (module, getPlayHead(), raw (id::filterTime), raw (id::filterSync) > 0.5f, raw (id::tremRate),
                      raw (id::tremSync) > 0.5f, blockBpm, sampleRate, numSamples);

    module.process (buffer, numCh, numSamples);

    filterModL.store (module.filterModL(), std::memory_order_relaxed);
    filterModR.store (module.filterModR(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* BitBitModulationProcessor::createEditor()
{
    return new BitBitModulationWebEditor (*this);
}

void BitBitModulationProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BitBitModulationProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            installState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitBitModulationProcessor();
}
