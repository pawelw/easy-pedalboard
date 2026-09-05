#include "PluginProcessor.h"

#include "PeakWahWebEditor.h"
#include "RateMap.h"

#include "ee/dsp/AutoWahConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"

#include <cmath>

namespace
{
using ee::plugin::percentToText;

using ee::plugin::kRampSeconds;

constexpr const char* kRangeID = "range";
constexpr const char* kFreqID = "freq";
constexpr const char* kQID = "q";
constexpr const char* kMixID = "mix";
constexpr const char* kDecayID = "decay";
constexpr const char* kStereoID = "stereo";
constexpr const char* kShapeID = "shape";
constexpr const char* kTimeID = "time";  // meaning set by Sync (free ms / synced note)
constexpr const char* kTypeID = "ftype"; // 0 % = Low, 50 % = Band, 100 % = High
constexpr const char* kSyncID = "sync";
constexpr const char* kOnID = "on";

constexpr float kDefaultFreePeriodMs = 400.0f;

juce::String hzToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " Hz";
}

/** The Type knob's reading: whichever of the three named anchors (Low, Band,
    High) the knob is currently closest to - a compound "Low-Band 42 %"
    reading between them named the blend exactly but ran too long for the
    knob's own display. */
juce::String filterTypeToText (float pct, int)
{
    const float p = juce::jlimit (0.0f, 100.0f, pct);

    if (p < 33.0f)
        return "Low";
    if (p > 67.0f)
        return "High";
    return "Band";
}

float freqHzFor (float pct)
{
    const float t = std::pow (juce::jlimit (0.0f, 1.0f, pct * 0.01f), ee::dsp::autowah::kFreqKnobSkew);
    return ee::dsp::autowah::kFreqMinHz * std::pow (ee::dsp::autowah::kFreqMaxHz / ee::dsp::autowah::kFreqMinHz, t);
}
} // namespace

PeakWahProcessor::PeakWahProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    rangeParam = apvts.getRawParameterValue (kRangeID);
    freqParam = apvts.getRawParameterValue (kFreqID);
    qParam = apvts.getRawParameterValue (kQID);
    mixParam = apvts.getRawParameterValue (kMixID);
    decayParam = apvts.getRawParameterValue (kDecayID);
    stereoParam = apvts.getRawParameterValue (kStereoID);
    shapeParam = apvts.getRawParameterValue (kShapeID);
    timeParam = apvts.getRawParameterValue (kTimeID);
    typeParam = apvts.getRawParameterValue (kTypeID);
    syncParam = apvts.getRawParameterValue (kSyncID);
    onParam = apvts.getRawParameterValue (kOnID);

    storedFreeRate01.store (ee::peakwah::rate01ForFreePeriodMs (kDefaultFreePeriodMs));
    storedSyncRate01.store (timeParam->load());

    apvts.addParameterListener (kSyncID, this);
}

PeakWahProcessor::~PeakWahProcessor()
{
    apvts.removeParameterListener (kSyncID, this);
}

void PeakWahProcessor::parameterChanged (const juce::String& parameterID, float)
{
    // Host automation can deliver this off the message thread; onSyncToggled()
    // calls setValueNotifyingHost(), which isn't safe to do from the audio
    // thread, so always hop before running it.
    if (parameterID == kSyncID)
        juce::MessageManager::callAsync ([this] { onSyncToggled(); });
}

void PeakWahProcessor::onSyncToggled()
{
    // syncParam already carries the new state by the time this listener runs.
    const bool nowSynced = syncParam->load() > 0.5f;
    const float current = timeParam->load();

    if (nowSynced)
        storedFreeRate01.store (current);
    else
        storedSyncRate01.store (current);

    const float target = nowSynced ? storedSyncRate01.load() : storedFreeRate01.load();

    if (auto* time = apvts.getParameter (kTimeID))
        time->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, target));
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakWahProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto pctAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kRangeID, 1 }, "Range", percent,
                                                             ee::dsp::autowah::kDefaultRangePct, pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kFreqID, 1 }, "Freq", percent, ee::dsp::autowah::kDefaultFreqPct,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                                           { return hzToText (freqHzFor (v), 0); })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kQID, 1 }, "Q", percent,
                                                             ee::dsp::autowah::kDefaultQPct, pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMixID, 1 }, "Mix", percent,
                                                             ee::dsp::autowah::kDefaultMixPct, pctAttr));

    // How fast the wobble flattens after you stop playing; fully up latches it on.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDecayID, 1 }, "Decay", percent,
                                                             ee::dsp::autowah::kDefaultDecayPct, pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kShapeID, 1 }, "Shape", percent,
                                                             ee::dsp::autowah::kDefaultShapePct, pctAttr));

    // One normalised knob; the Sync switch decides what it means, and each mode's
    // last position is remembered. Down = fastest, up = slowest in both modes
    // (Peak Wah runs this knob the other way up - see RateMap.h). The host-facing
    // text assumes the synced reading; the editor overrides it live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTimeID, 1 }, "Time", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return ee::peakwah::rateToText (v, true); })));

    // Continuous, not a three-way switch: the tank's low-, band- and high-pass
    // taps crossfade into each other the way Shape morphs the LFO wave.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTypeID, 1 }, "Type", percent, ee::dsp::autowah::kDefaultTypePct,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (filterTypeToText)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kStereoID, 1 }, "Stereo", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSyncID, 1 }, "Sync", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakWahProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate;

    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    wah.prepare (newSampleRate);
    wah.reset();

    const juce::dsp::ProcessSpec spec { newSampleRate, static_cast<juce::uint32> (maxBlock), 1 };
    const auto hiCutCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (newSampleRate, kHiCutHz);
    for (auto& filter : hiCut)
    {
        filter.prepare (spec);
        filter.coefficients = hiCutCoeffs;
        filter.reset();
    }

    dryBuffer.setSize (kMaxChannels, maxBlock, false, false, true);

    const bool engaged = onParam->load() > 0.5f;
    wetMix.reset (newSampleRate, kRampSeconds);
    wetMix.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);

    haveExpectedPpq = false;
    wasPlaying = false;
}

void PeakWahProcessor::releaseResources() {}

bool PeakWahProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // Stereo in / mono out would fold the L/R sweep back together.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

juce::String PeakWahProcessor::freqReadout() const
{
    return hzToText (freqHzFor (freqParam->load()), 0);
}

juce::String PeakWahProcessor::timeReadout() const
{
    return ee::peakwah::rateToText (timeParam->load(), syncParam->load() > 0.5f);
}

juce::String PeakWahProcessor::typeReadout() const
{
    return filterTypeToText (typeParam->load(), 0);
}

void PeakWahProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    double bpm = 120.0;
    bool havePpq = false;
    bool isPlaying = false;
    double ppqStart = 0.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;
            if (const auto ppq = position->getPpqPosition())
            {
                ppqStart = *ppq;
                havePpq = std::isfinite (ppqStart);
            }
            isPlaying = position->getIsPlaying();
        }
    if (! std::isfinite (bpm))
        bpm = 120.0;
    bpm = juce::jlimit (20.0, 300.0, bpm);

    const float time01 = timeParam->load();
    const bool synced = syncParam->load() > 0.5f;
    const bool engaged = onParam->load() > 0.5f;

    const float periodSeconds = juce::jmax (1.0e-4f, ee::peakwah::rateToPeriodSeconds (time01, synced, bpm));
    wah.setPeriodSeconds (periodSeconds);

    // The LFO always free-runs; when synced to a running transport we also align
    // it to the host grid - a hard snap on the first playing block or a transport
    // jump, otherwise a gentle per-block pull (see AutoWah::nudgePhase).
    if (synced && havePpq && isPlaying)
    {
        const double cyclesPerQuarter =
            1.0 / juce::jmax (1.0e-4, static_cast<double> (ee::peakwah::syncedDivisionBeats (time01)));
        const double ppqPerSample = bpm / (60.0 * sampleRate);

        const double target = ppqStart * cyclesPerQuarter;
        const bool jumped = ! wasPlaying || (haveExpectedPpq && std::abs (ppqStart - expectedPpq) > 0.25);

        if (jumped)
            wah.snapPhase (target);
        else
            wah.nudgePhase (target);

        expectedPpq = ppqStart + numSamples * ppqPerSample;
        haveExpectedPpq = true;
    }
    else
    {
        haveExpectedPpq = false;
    }
    wasPlaying = isPlaying;

    wah.setRange01 (rangeParam->load() * 0.01f);
    wah.setFreq01 (freqParam->load() * 0.01f);
    wah.setQ01 (qParam->load() * 0.01f);
    wah.setMix01 (mixParam->load() * 0.01f);
    wah.setDecay01 (decayParam->load() * 0.01f);
    wah.setStereo (stereoParam->load() > 0.5f);
    wah.setShape01 (shapeParam->load() * 0.01f);
    wah.setTypeMorph01 (typeParam->load() * 0.01f);

    if (numSamples > dryBuffer.getNumSamples())
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // Signal glow: a fast-attack/slow-release peak follower on the dry input,
    // in seconds rather than a fixed per-block factor so it doesn't chase
    // faster or slower depending on the host's block size.
    {
        float blockPeak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = dryBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                blockPeak = juce::jmax (blockPeak, std::abs (data[i]));
        }

        const double blockSeconds = numSamples / sampleRate;
        const float attackCoeff = static_cast<float> (std::exp (-blockSeconds / 0.005));
        const float releaseCoeff = static_cast<float> (std::exp (-blockSeconds / 0.4));
        const float coeff = blockPeak > peakLevelSmoothed ? attackCoeff : releaseCoeff;
        peakLevelSmoothed = coeff * peakLevelSmoothed + (1.0f - coeff) * blockPeak;

        const float db = juce::Decibels::gainToDecibels (peakLevelSmoothed, -60.0f);
        peakLevelUi.store (juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 40.0f), std::memory_order_relaxed);
    }

    float* left = buffer.getWritePointer (0);
    float* right = numCh >= 2 ? buffer.getWritePointer (1) : nullptr;
    wah.process (left, right, numSamples);

    // Publish the LFO state for the editor's response scope.
    lfoModLUi.store (wah.modL(), std::memory_order_relaxed);
    lfoModRUi.store (wah.modR(), std::memory_order_relaxed);

    // Fixed 2.5 kHz hi-cut on the wet signal, always on and last in the chain.
    // It runs every block, bypassed or not, so re-engaging never starts it cold.
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& filter = hiCut[static_cast<size_t> (ch)];
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = filter.processSample (data[i]);
        filter.snapToZero();
    }

    wetMix.setTargetValue (engaged ? 1.0f : 0.0f);
    ee::plugin::crossfadeToDry (buffer, dryBuffer, wetMix, numCh, numSamples);
}

juce::AudioProcessorEditor* PeakWahProcessor::createEditor()
{
    return new PeakWahWebEditor (*this);
}

void PeakWahProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakWahProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakWahProcessor();
}
