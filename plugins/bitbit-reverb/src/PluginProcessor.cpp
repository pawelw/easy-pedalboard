#include "PluginProcessor.h"

#include "Params.h"
#include "BitBitReverbWebEditor.h"

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/SpringConfig.h"
#include "ee/plugin/ParamRange.h"
#include "ee/plugin/ParamText.h"
#include "ee/plugin/StateVersion.h"

namespace
{
using namespace ee::reverb;
using ee::plugin::percentToText;

/** "480 ms" under a second, "2.40 s" above - the Decay readout BitBit Alpine
    prints for the same two knobs. */
juce::String secondsToText (float value, int)
{
    return value < 1.0f ? juce::String (juce::roundToInt (value * 1000.0f)) + " ms" : juce::String (value, 2) + " s";
}

/** "180 Hz" under 1 kHz, "1.2 kHz" above - BitBit Alpine's Low Cut readout. */
juce::String hertzToText (float value, int)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (value)) + " Hz";
}

/** "13 ms" - Pre-delay never reaches a second, so no unit switch is needed. */
juce::String msToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " ms";
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

BitBitReverbProcessor::BitBitReverbProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };
}

BitBitReverbProcessor::~BitBitReverbProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout BitBitReverbProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Every id, range and default is BitBit Alpine's Reverb module's, minus the
    // `rev.` - the same module, so the same knob position sounds the same.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::on, 1 }, "On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::engine, 1 }, "Engine",
                                                              juce::StringArray { "Space", "Spring" }, 0));
    addPercent (layout, id::mix, "Mix", 30.0f);

    // Space.
    auto spaceDecay = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay);
    spaceDecay.setSkewForCentre (2.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::spaceDecay, 1 }, "Space Decay",
                                                             spaceDecay, 2.0f, withText (secondsToText)));
    addPercent (layout, id::spaceShimmer, "Space Shimmer", 0.0f);

    auto spaceLoCut =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinLowCutHz, ee::dsp::FdnReverb::kMaxLowCutHz);
    spaceLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::spaceLoCut, 1 }, "Space Low Cut",
                                                             spaceLoCut, ee::dsp::FdnReverb::kMinLowCutHz,
                                                             withText (hertzToText)));
    addPercent (layout, id::spaceReso, "Space Reso", 50.0f);

    // Spring. BitBit Spring's ranges and defaults.
    auto springDecay =
        juce::NormalisableRange<float> (ee::dsp::spring::kMinDecaySeconds, ee::dsp::spring::kMaxDecaySeconds);
    springDecay.setSkewForCentre (ee::dsp::spring::kDecaySkewCentre);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::springDecay, 1 }, "Spring Decay", springDecay,
        ee::plugin::snapToRange (springDecay, ee::dsp::spring::kDefaultDecaySeconds), withText (secondsToText)));
    addPercent (layout, id::springTension, "Spring Tension", ee::dsp::spring::kDefaultTension01 * 100.0f);

    // The same travel Space's Low Cut has, deliberately - see SpringConfig.h.
    auto springLoCut = juce::NormalisableRange<float> (ee::dsp::spring::kMinLowCutHz, ee::dsp::spring::kMaxLowCutHz);
    springLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::springLoCut, 1 }, "Spring Low Cut",
                                                             springLoCut, ee::dsp::spring::kOutputLowCutHz,
                                                             withText (hertzToText)));

    // Appended after Spring rather than slotted into the Space block above -
    // see the comment on these two ids in Params.h.
    auto spacePredelay =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinPredelayMs, ee::dsp::FdnReverb::kMaxPredelayMs);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::spacePredelay, 1 },
                                                             "Space Pre-delay", spacePredelay, 13.0f,
                                                             withText (msToText)));
    addPercent (layout, id::spaceDamping, "Space Damping", 50.0f);

    return layout;
}

void BitBitReverbProcessor::pushSettings() noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };

    module.setEngine (static_cast<int> (raw (id::engine)));
    module.setEngaged (raw (id::on) > 0.5f);
    module.setMix01 (pct (id::mix));

    module.setSpace (raw (id::spaceDecay), pct (id::spaceShimmer), raw (id::spaceLoCut), pct (id::spaceReso),
                     raw (id::spacePredelay), pct (id::spaceDamping));
    module.setSpring (raw (id::springDecay), pct (id::springTension), raw (id::springLoCut));
}

void BitBitReverbProcessor::installState (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
}

// ---------------------------------------------------------------------- audio

void BitBitReverbProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    pushSettings();
    module.prepare (sampleRate, maxBlock);

    // Both engines are latency-free, so this is zero - reported anyway, for the
    // day one is not.
    setLatencySamples (module.latencySamples());
}

void BitBitReverbProcessor::releaseResources()
{
    module.reset();
}

bool BitBitReverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // Mono out from a stereo in would throw away half the signal for no reason.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

void BitBitReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    pushSettings();

    module.process (buffer, numCh, numSamples);
}

juce::AudioProcessorEditor* BitBitReverbProcessor::createEditor()
{
    return new BitBitReverbWebEditor (*this);
}

void BitBitReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = ee::plugin::copyVersionedState (apvts).createXml())
        copyXmlToBinary (*xml, destData);
}

void BitBitReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = ee::plugin::xmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            installState (ee::plugin::sanitisedState (juce::ValueTree::fromXml (*xml), apvts));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitBitReverbProcessor();
}
