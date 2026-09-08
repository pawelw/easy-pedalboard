#include "PluginProcessor.h"

#include "Params.h"
#include "PeakAlpineWebEditor.h"

#include "ee/dsp/ChorusConfig.h"
#include "ee/dsp/PhaserConfig.h"
#include "ee/dsp/RateMap.h"
#include "ee/dsp/SpringConfig.h"
#include "ee/dsp/TapeMachineConfig.h"
#include "ee/dsp/TremoloConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"

#include "ee/fx/DelayTimeMap.h"

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

// The Input/Output trims' range. Asymmetric on purpose, exactly as Peak
// Delay's are: they are a trim and a level, not a gain stage, so there is more
// cut than boost.
constexpr float kMinGainDb = -24.0f;
constexpr float kMaxGainDb = 12.0f;

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
} // namespace

PeakAlpineProcessor::PeakAlpineProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
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

    // ------------------------------------------------------------- modulation
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::modOn, 1 }, "Modulation On", true));
    layout.add (
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::modEngine, 1 }, "Modulation Engine",
                                                      juce::StringArray { "Tape", "Tremolo", "Chorus", "Phaser" }, 1));
    addPercent (layout, id::modLevel, "Modulation Level", 100.0f);
    addPercent (layout, id::modMix, "Modulation Mix", 40.0f);

    // Ranges and defaults are each engine's own pedal's, so the same knob
    // position sounds the same in both places.
    addPercent (layout, id::modTapeSat, "Tape Saturation", ee::dsp::tape::kDefaultSaturationPct);
    addPercent (layout, id::modTapeFlutter, "Tape Flutter", ee::dsp::tape::kDefaultFlutterPct);
    addPercent (layout, id::modTapeWear, "Tape Wear", ee::dsp::tape::kDefaultWearPct);
    addPercent (layout, id::modTapeNoise, "Tape Noise", ee::dsp::tape::kDefaultNoisePct);

    addPercent (layout, id::modTremAmount, "Tremolo Amount", 50.0f);
    // A plain 0..1 knob, like Peak Trem & Pan's: what a position means depends
    // on the Sync switch, so the mapping is the map's rather than the range's.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modTremRate, 1 }, "Tremolo Rate",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
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
    const auto timeAttributes = Attributes().withStringFromValueFunction (
        [] (float v, int) { return ee::peakdelay::timeMap().toText (v, true, ee::peakdelay::kReferenceBpm); });
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyLeftTime, 1 }, "Left Time",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             kDefaultTime01, timeAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::dlyRightTime, 1 }, "Right Time",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             kDefaultTime01, timeAttributes));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlySync, 1 }, "Sync L/R", true));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlyTimeUnit, 1 }, "Time Unit", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::dlyType, 1 }, "Delay Type",
                                                              juce::StringArray { "Normal", "Wide", "Ping Pong" }, 0));
    addPercent (layout, id::dlyFeedback, "Feedback", 35.0f);
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

    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::dlyTapePre, 1 }, "Tape Placement", true));

    // ----------------------------------------------------------------- reverb
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::revOn, 1 }, "Reverb On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::revEngine, 1 }, "Reverb Engine",
                                                              juce::StringArray { "Space", "Spring" }, 0));
    addPercent (layout, id::revLevel, "Reverb Level", 100.0f);
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

// ------------------------------------------------------------------- settings

void PeakAlpineProcessor::pushSettings (double bpm) noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };
    const auto flag = [&raw] (const char* pid) { return raw (pid) > 0.5f; };

    // --------------------------------------------------------------- modulation
    modulation.setEngine (static_cast<int> (raw (id::modEngine)));
    modulation.setEngaged (flag (id::modOn));
    modulation.setLevel (pct (id::modLevel));
    modulation.setMix01 (pct (id::modMix));

    modulation.setTape (pct (id::modTapeSat), pct (id::modTapeFlutter), pct (id::modTapeWear), pct (id::modTapeNoise));

    // The Rate knob is a position, not a rate: what it means is this face's
    // map, and free-running here because the module's face has no Sync switch.
    const float tremPeriodSeconds = kTremRateMap.rateToPeriodSeconds (raw (id::modTremRate), false, bpm);
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

    // The host owns the trims; the module's own are left at unity so they are
    // not applied twice.
    delay.setTrims (1.0f, 1.0f);

    // ------------------------------------------------------------------- reverb
    reverb.setEngine (static_cast<int> (raw (id::revEngine)));
    reverb.setEngaged (flag (id::revOn));
    reverb.setLevel (pct (id::revLevel));
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

    // prepareToPlay is one of the callbacks where the playhead is valid, so the
    // cache starts out holding the host's real tempo rather than 120.
    pushSettings (readPlayHeadBpm());

    delay.setFilterRestingPoints (kLoCutMinHz, kHiCutMaxHz);

    modulation.prepare (sampleRate, maxBlock);
    delay.prepare (sampleRate, maxBlock);
    reverb.prepare (sampleRate, maxBlock);

    inputMeter.prepare (sampleRate);

    dryBuffer.setSize (kMaxChannels, maxBlock, false, true, true);

    inGain.reset (sampleRate, kRampSeconds);
    outGain.reset (sampleRate, kRampSeconds);
    engageGain.reset (sampleRate, kRampSeconds);

    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::inGain)->load()));
    outGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id::outGain)->load()));
    engageGain.setCurrentAndTargetValue (apvts.getRawParameterValue (id::on)->load() > 0.5f ? 1.0f : 0.0f);

    // The dry path's latency, which is the Delay module's tape section - the
    // only stage in the chain that delays the signal it is not an effect on.
    setLatencySamples (delay.latencySamples());
}

void PeakAlpineProcessor::releaseResources()
{
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

    pushSettings (readPlayHeadBpm());

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

    // The chain. Fixed order, and each module is responsible for its own
    // dry/wet and its own power toggle - all this does is hand the signal on.
    modulation.process (buffer, numCh, numSamples);
    delay.process (buffer, numCh, numCh, numSamples);
    reverb.process (buffer, numCh, numSamples);

    // The global bypass crossfades back to the input as it was before the
    // Input trim, so a bypassed plugin is unity whatever either trim says.
    ee::plugin::crossfadeToDry (buffer, dryBuffer, engageGain, numCh, numSamples, &outGain);
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
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakAlpineProcessor();
}
