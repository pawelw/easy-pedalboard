#include "PluginProcessor.h"

#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#include "BinaryData.h"

#if EE_SHIMMER_TUNER
#include "ShimmerTunerPanel.h"
#endif

namespace
{
using ee::plugin::percentToText;

constexpr const char* kDecayID = "decay";
constexpr const char* kMixID = "mix";
constexpr const char* kLowCutID = "locut";
constexpr const char* kResonanceID = "res";
constexpr const char* kShimmerID = "shimmer";
constexpr const char* kOnID = "on";

// The network is normalised to ~0.42 RMS gain. Trimmed against a reference
// plate so that a 50 % mix lands at the same wet level it does there.
constexpr float kWetTrim = 1.1f;

// Mix knob shaping. Above 1.0 the wet comes in more gradually, so the
// useful part of the range is not squeezed into the first third of travel.
constexpr float kMixCurve = 1.3f;

constexpr float kGainRampSeconds = 0.02f;

juce::String secondsToText (float value, int)
{
    return juce::String (value, value < 1.0f ? 2 : 1) + " s";
}

juce::String hertzToText (float value, int)
{
    if (value <= ee::dsp::FdnReverb::kMinLowCutHz + 0.5f)
        return "off";
    return juce::String (juce::roundToInt (value)) + " Hz";
}

float shapedMix (float percent) noexcept
{
    return std::pow (juce::jlimit (0.0f, 1.0f, percent * 0.01f), kMixCurve);
}
} // namespace

PeakReverbProcessor::PeakReverbProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    decayParam = apvts.getRawParameterValue (kDecayID);
    mixParam = apvts.getRawParameterValue (kMixID);
    lowCutParam = apvts.getRawParameterValue (kLowCutID);
    resonanceParam = apvts.getRawParameterValue (kResonanceID);
    shimmerParam = apvts.getRawParameterValue (kShimmerID);
    onParam = apvts.getRawParameterValue (kOnID);
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakReverbProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto decayRange = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay);
    decayRange.setSkewForCentre (2.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDecayID, 1 }, "Decay Time", decayRange, 3.2f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (secondsToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kMixID, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    auto lowCutRange =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinLowCutHz, ee::dsp::FdnReverb::kMaxLowCutHz);
    lowCutRange.setSkewForCentre (180.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kLowCutID, 1 }, "Low Cut", lowCutRange, ee::dsp::FdnReverb::kMinLowCutHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hertzToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kResonanceID, 1 }, "Resonance", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kShimmerID, 1 }, "Shimmer", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakReverbProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    reverb.prepare (sampleRate);
    reverb.reset();

    monoBuffer.setSize (1, maxBlock, false, true, true);
    wetBuffer.setSize (2, maxBlock, false, true, true);

    dryGain.reset (sampleRate, kGainRampSeconds);
    wetGain.reset (sampleRate, kGainRampSeconds);
    inputGain.reset (sampleRate, kGainRampSeconds);

    const float mix = shapedMix (mixParam->load());
    const bool engaged = onParam->load() > 0.5f;

    reverb.setLowCut (lowCutParam->load());
    reverb.setResonance (resonanceParam->load() * 0.01f);
    reverb.setShimmer (shimmerParam->load() * 0.01f);

    dryGain.setCurrentAndTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setCurrentAndTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi) * kWetTrim);
    inputGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void PeakReverbProcessor::releaseResources()
{
    reverb.reset();
}

double PeakReverbProcessor::getTailLengthSeconds() const
{
    return static_cast<double> (reverb.getTailSeconds());
}

bool PeakReverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void PeakReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numOut == 0 || numSamples == 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const float mix = shapedMix (mixParam->load());
    const bool engaged = onParam->load() > 0.5f;

    reverb.setDecayTime (decayParam->load());
    reverb.setLowCut (lowCutParam->load());
    reverb.setResonance (resonanceParam->load() * 0.01f);
    reverb.setShimmer (shimmerParam->load() * 0.01f);

    // Trails: bypassing stops feeding the network but leaves the wet path open,
    // so the existing tail rings out instead of being cut off.
    dryGain.setTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi) * kWetTrim);
    inputGain.setTargetValue (engaged ? 1.0f : 0.0f);

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int chunk = juce::jmin (maxBlock, numSamples - offset);

        float* mono = monoBuffer.getWritePointer (0);
        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : inL;

        for (int i = 0; i < chunk; ++i)
            mono[i] = 0.5f * (inL[i] + inR[i]) * inputGain.getNextValue();

        float* wetL = wetBuffer.getWritePointer (0);
        float* wetR = wetBuffer.getWritePointer (1);
        reverb.process (mono, wetL, wetR, chunk);

        float* outL = buffer.getWritePointer (0, offset);
        float* outR = numOut > 1 ? buffer.getWritePointer (1, offset) : nullptr;

        for (int i = 0; i < chunk; ++i)
        {
            const float dg = dryGain.getNextValue();
            const float wg = wetGain.getNextValue();

            const float dryL = outL[i];

            if (outR != nullptr)
            {
                const float dryR = outR[i];
                outL[i] = dryL * dg + wetL[i] * wg;
                outR[i] = dryR * dg + wetR[i] * wg;
            }
            else
            {
                outL[i] = dryL * dg + 0.5f * (wetL[i] + wetR[i]) * wg;
            }
        }
    }
}

juce::AudioProcessorEditor* PeakReverbProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Reverb";
    spec.tagline = "Decay drives room size and predelay";
    spec.version = "v" JucePlugin_VersionString;
    spec.knobs = { { kDecayID, "Decay" }, { kMixID, "Mix" }, { kShimmerID, "Shimmer" }, { kLowCutID, "Low Cut" } };

    // Resonance moves to a small cap in the middle of the four - value on the
    // face would only crowd it, so the readout is just the "RESO" label.
    spec.centreKnob =
        ee::ui::KnobSpec { .parameterID = kResonanceID, .caption = "reso", .compact = true, .compactCaption = true };

    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);

    // The blue palette, but the four large caps are fixed silver discs in a
    // brushed-silver bezel rather than the plain photographic cap, a sky fills
    // the face behind the frame, and the lettering is black to read on it.
    auto theme = ee::ui::PedalTheme::blue();
    theme.controlStyle = ee::ui::ControlStyle::analogSilver;
    theme.backgroundImage = juce::ImageCache::getFromMemory (BinaryData::reverbbg_jpeg, BinaryData::reverbbg_jpegSize);
    theme.textPrimary = juce::Colours::black;
    theme.textSecondary = juce::Colour (0xff3a3a3a);
    theme.title = juce::Colours::black;
    theme.logoTint = juce::Colours::black;

    // Swap the value arc and its background track: the line takes the pale
    // colour, the track takes the blue.
    const auto arcLine = theme.knobTrack;
    theme.knobTrack = theme.accent;
    theme.accent = arcLine;

    auto* editor = new ee::ui::PedalEditor (*this, apvts, spec, theme);

#if EE_SHIMMER_TUNER
    // Flip to true to bring the panel back without reconfiguring CMake.
    constexpr bool showTuner = false;

    if (showTuner)
        editor->setSidePanel (std::make_unique<ShimmerTunerPanel> (reverb.getShimmerTuning(),
                                                                   [this] (const ee::dsp::ShimmerTuning& t)
                                                                   { reverb.setShimmerTuning (t); }),
                              ShimmerTunerPanel::preferredWidth);
#endif

    return editor;
}

void PeakReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakReverbProcessor();
}
