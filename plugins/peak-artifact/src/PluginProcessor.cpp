#include "PluginProcessor.h"

#include "Params.h"
#include "PeakArtifactWebEditor.h"
#include "RateMap.h"

#include "ee/dsp/AutoWahConfig.h"
#include "ee/plugin/ParamText.h"

#include <cmath>

namespace
{
using namespace ee::artifact;
using ee::plugin::percentToText;

/** The <> wave picker's three positions, as ee::dsp::lfoValue shape morphs:
    triangle in the middle of the morph, ramp a quarter down, hard square at the
    top. Kept in step with jsui/src/engines.jsx's WAVES table. */
constexpr float kWaveShape01[] = { 0.50f, 0.25f, 1.00f };

juce::String hzToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " Hz";
}

float freqHzFor (float pct)
{
    const float t = std::pow (juce::jlimit (0.0f, 1.0f, pct * 0.01f), ee::dsp::autowah::kFreqKnobSkew);
    return ee::dsp::autowah::kFreqMinHz * std::pow (ee::dsp::autowah::kFreqMaxHz / ee::dsp::autowah::kFreqMinHz, t);
}

using Attributes = juce::AudioParameterFloatAttributes;

Attributes withText (juce::String (*fn) (float, int))
{
    return Attributes().withStringFromValueFunction (fn);
}

float waveShape01 (int waveIndex) noexcept
{
    const int last = static_cast<int> (std::size (kWaveShape01)) - 1;
    return kWaveShape01[juce::jlimit (0, last, waveIndex)];
}
} // namespace

PeakArtifactProcessor::PeakArtifactProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };
}

PeakArtifactProcessor::~PeakArtifactProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout PeakArtifactProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto pctAttr = Attributes().withStringFromValueFunction (percentToText);

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::on, 1 }, "On", true));

    // Filter is the only voiced engine, so the pedal opens on it. Ring Mod and
    // Bit Crush are selectable and pass audio through untouched for now.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::engine, 1 }, "Engine",
        juce::StringArray { "Ring Mod", "Bit Crush", "Filter" }, 2));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::mix, 1 }, "Mix", percent, 50.0f,
                                                             pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::fltFreq, 1 }, "Freq", percent, ee::dsp::autowah::kDefaultFreqPct,
        withText ([] (float v, int) { return hzToText (freqHzFor (v), 0); })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::fltQ, 1 }, "Q", percent,
                                                             ee::dsp::autowah::kDefaultQPct, pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::fltRange, 1 }, "Range", percent,
                                                             ee::dsp::autowah::kDefaultRangePct, pctAttr));

    // One normalised knob; the Sync switch decides what it means. Down = fastest,
    // up = slowest in both modes (see RateMap.h). The host-facing text assumes
    // the synced reading; the editor overrides it live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::fltTime, 1 }, "Time", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f,
        withText ([] (float v, int) { return ee::peakartifact::rateToText (v, true); })));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::fltSync, 1 }, "Sync", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::fltWave, 1 }, "Wave", juce::StringArray { "Triangle", "Ramp", "Square" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::fltStereo, 1 }, "Stereo", false));

    return layout;
}

void PeakArtifactProcessor::pushSettings (double bpm) noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };
    const auto flag = [&raw] (const char* pid) { return raw (pid) > 0.5f; };

    module.setEngine (static_cast<int> (raw (id::engine)));
    module.setEngaged (flag (id::on));
    module.setMix01 (pct (id::mix));

    const bool synced = flag (id::fltSync);
    const float period = juce::jmax (
        1.0e-4f, ee::peakartifact::rateToPeriodSeconds (raw (id::fltTime), synced, bpm));

    module.setFilter (pct (id::fltFreq), pct (id::fltQ), pct (id::fltRange),
                      waveShape01 (static_cast<int> (raw (id::fltWave))), period, flag (id::fltStereo));
}

juce::String PeakArtifactProcessor::timeReadout() const
{
    const float time01 = apvts.getRawParameterValue (id::fltTime)->load();
    const bool synced = apvts.getRawParameterValue (id::fltSync)->load() > 0.5f;
    return ee::peakartifact::rateToText (time01, synced);
}

void PeakArtifactProcessor::installState (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
}

// ---------------------------------------------------------------------- audio

void PeakArtifactProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpm = position->getBpm())
                if (std::isfinite (*hostBpm))
                    bpm = juce::jlimit (20.0, 300.0, *hostBpm);

    pushSettings (bpm);
    module.prepare (sampleRate, maxBlock);

    haveExpectedPpq = false;
    wasPlaying = false;
}

void PeakArtifactProcessor::releaseResources()
{
    module.reset();
}

bool PeakArtifactProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void PeakArtifactProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    pushSettings (bpm);

    const float time01 = apvts.getRawParameterValue (id::fltTime)->load();
    const bool synced = apvts.getRawParameterValue (id::fltSync)->load() > 0.5f;

    // The LFO always free-runs; when synced to a running transport we also align
    // it to the host grid - a hard snap on the first playing block or a jump,
    // otherwise a gentle per-block pull. Same shape as Peak Wah.
    if (synced && havePpq && isPlaying)
    {
        const double cyclesPerQuarter =
            1.0 / juce::jmax (1.0e-4, static_cast<double> (ee::peakartifact::syncedDivisionBeats (time01)));
        const double ppqPerSample = bpm / (60.0 * sampleRate);

        const double target = ppqStart * cyclesPerQuarter;
        const bool jumped = ! wasPlaying || (haveExpectedPpq && std::abs (ppqStart - expectedPpq) > 0.25);

        if (jumped)
            module.snapFilterPhase (target);
        else
            module.nudgeFilterPhase (target);

        expectedPpq = ppqStart + numSamples * ppqPerSample;
        haveExpectedPpq = true;
    }
    else
    {
        haveExpectedPpq = false;
    }
    wasPlaying = isPlaying;

    module.process (buffer, numCh, numSamples);

    // Publish the Filter engine's live sweep position for the editor's scope.
    lfoModLUi.store (module.filterModL(), std::memory_order_relaxed);
    lfoModRUi.store (module.filterModR(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PeakArtifactProcessor::createEditor()
{
    return new PeakArtifactWebEditor (*this);
}

void PeakArtifactProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakArtifactProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            installState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakArtifactProcessor();
}
