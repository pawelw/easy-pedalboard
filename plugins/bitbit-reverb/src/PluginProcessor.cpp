#include "PluginProcessor.h"

#include "Params.h"
#include "BitBitReverbWebEditor.h"

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/SpaceReverb.h"
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
                                                              juce::StringArray { "Spring", "Shimmer", "Modern" }, 2));
    addPercent (layout, id::mix, "Mix", 30.0f);

    // Shimmer (the `space.` ids - see Params.h). Decay, its own feedback
    // amount and Reso are fixed inside ee::fx::ReverbModule::setShimmer.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::spaceOctave, 1 }, "Shimmer Octave", juce::StringArray { "-1 Oct", "0", "+1 Oct" }, 2));

    auto spaceLoCut =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinLowCutHz, ee::dsp::FdnReverb::kMaxLowCutHz);
    spaceLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::spaceLoCut, 1 }, "Shimmer Low Cut",
                                                             spaceLoCut, ee::dsp::FdnReverb::kMinLowCutHz,
                                                             withText (hertzToText)));

    auto spaceHiCut =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinHighCutHz, ee::dsp::FdnReverb::kMaxHighCutHz);
    spaceHiCut.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::spaceHiCut, 1 }, "Shimmer Hi Cut",
                                                             spaceHiCut, ee::dsp::FdnReverb::kMaxHighCutHz,
                                                             withText (hertzToText)));
    addPercent (layout, id::spaceDamping, "Shimmer Damping", 50.0f);

    // Spring. BitBit Spring's ranges and defaults.
    auto springDecay =
        juce::NormalisableRange<float> (ee::dsp::spring::kMinDecaySeconds, ee::dsp::spring::kMaxDecaySeconds);
    springDecay.setSkewForCentre (ee::dsp::spring::kDecaySkewCentre);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::springDecay, 1 }, "Spring Decay", springDecay,
        ee::plugin::snapToRange (springDecay, ee::dsp::spring::kDefaultDecaySeconds), withText (secondsToText)));
    addPercent (layout, id::springTension, "Spring Tension", ee::dsp::spring::kDefaultTension01 * 100.0f);

    // The same travel Shimmer's Low Cut has, deliberately - see SpringConfig.h.
    auto springLoCut = juce::NormalisableRange<float> (ee::dsp::spring::kMinLowCutHz, ee::dsp::spring::kMaxLowCutHz);
    springLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::springLoCut, 1 }, "Spring Low Cut",
                                                             springLoCut, ee::dsp::spring::kOutputLowCutHz,
                                                             withText (hertzToText)));

    // Used to be fixed voicing (SpringConfig.h's kOutputHighCutHz); the same
    // travel the other two engines' Hi Cut has, for the same reason Low Cut's
    // does. Defaults to that old fixed value, so an untouched tank is unchanged.
    auto springHiCut = juce::NormalisableRange<float> (ee::dsp::spring::kMinHighCutHz, ee::dsp::spring::kMaxHighCutHz);
    springHiCut.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::springHiCut, 1 }, "Spring Hi Cut",
                                                             springHiCut, ee::dsp::spring::kOutputHighCutHz,
                                                             withText (hertzToText)));

    // Modern, appended last. Decay is the RT60 of the low mids and Damping the
    // reference's Damp, one for one - see SpaceConfig.h. Pre-delay adds to the
    // engine's own ~18 ms before the first echo; 0 is the reference's setting.
    using Modern = ee::dsp::SpaceReverb;
    auto modernDecay = juce::NormalisableRange<float> (Modern::kMinDecay, Modern::kMaxDecay);
    modernDecay.setSkewForCentre (2.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modernDecay, 1 }, "Modern Decay",
                                                             modernDecay, 2.0f, withText (secondsToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::modernPredelay, 1 }, "Modern Pre-delay",
        juce::NormalisableRange<float> (Modern::kMinPredelayMs, Modern::kMaxPredelayMs), 0.0f, withText (msToText)));
    addPercent (layout, id::modernDamping, "Modern Damping", 25.0f);

    auto modernLoCut = juce::NormalisableRange<float> (Modern::kMinLowCutHz, Modern::kMaxLowCutHz);
    modernLoCut.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modernLoCut, 1 }, "Modern Low Cut",
                                                             modernLoCut, Modern::kMinLowCutHz,
                                                             withText (hertzToText)));

    auto modernHiCut = juce::NormalisableRange<float> (Modern::kMinHighCutHz, Modern::kMaxHighCutHz);
    modernHiCut.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::modernHiCut, 1 }, "Modern Hi Cut",
                                                             modernHiCut, Modern::kMaxHighCutHz,
                                                             withText (hertzToText)));

    return layout;
}

void BitBitReverbProcessor::pushSettings() noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };

    module.setEngine (static_cast<int> (raw (id::engine)));
    module.setEngaged (raw (id::on) > 0.5f);
    module.setMix01 (pct (id::mix));

    // The octave choice's raw value is its index (0/1/2), not the -1/0/+1 the
    // engine wants.
    module.setShimmer (static_cast<int> (raw (id::spaceOctave)) - 1, raw (id::spaceLoCut), raw (id::spaceHiCut),
                       pct (id::spaceDamping));
    module.setModern (raw (id::modernDecay), raw (id::modernPredelay), pct (id::modernDamping), raw (id::modernLoCut),
                      raw (id::modernHiCut));
    module.setSpring (raw (id::springDecay), pct (id::springTension), raw (id::springLoCut), raw (id::springHiCut));
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
