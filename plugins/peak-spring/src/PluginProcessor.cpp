#include "PluginProcessor.h"

#include "ee/dsp/SpringConfig.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#include <cmath>

namespace
{
using ee::plugin::percentToText;

constexpr const char* kDecayID = "decay";
constexpr const char* kMixID = "mix";
constexpr const char* kStereoID = "stereo";
constexpr const char* kOnID = "on";

// Mix knob shaping, the same curve Peak Reverb uses: above 1.0 the wet comes in
// more gradually, so the useful part of the range is not squeezed into the
// first third of the travel.
constexpr float kMixCurve = 1.3f;

constexpr float kGainRampSeconds = 0.02f;

juce::String secondsToText (float value, int)
{
    // Two decimals all the way up, unlike the plate reverb's readout: this knob
    // is calibrated against a reference tank, and dialling the same 3.58 s it
    // shows is the whole point of being able to read it that finely.
    return juce::String (value, 2) + " s";
}

float shapedMix (float percent) noexcept
{
    return std::pow (juce::jlimit (0.0f, 1.0f, percent * 0.01f), kMixCurve);
}

/** Wet gain for a shaped mix, including the make-up that offsets how narrow a
    spring tank is next to the full-range dry it is replacing. Grows with the
    mix so there is nothing to hear at the dry end and the full amount only at
    the wet one. */
float wetGainFor (float mix) noexcept
{
    const float makeup = juce::jmin (1.0f + (ee::dsp::spring::kMixMakeupAtFullWet - 1.0f) * mix,
                                     ee::dsp::spring::kMixMakeupMax);
    return std::sin (mix * juce::MathConstants<float>::halfPi) * makeup;
}
} // namespace

PeakSpringProcessor::PeakSpringProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    decayParam = apvts.getRawParameterValue (kDecayID);
    mixParam = apvts.getRawParameterValue (kMixID);
    stereoParam = apvts.getRawParameterValue (kStereoID);
    onParam = apvts.getRawParameterValue (kOnID);
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakSpringProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Skewed so the short, springy end of the range gets most of the travel -
    // past a couple of seconds a tank is already in "surf" territory and the
    // remaining knob only stretches it.
    auto decayRange =
        juce::NormalisableRange<float> (ee::dsp::spring::kMinDecaySeconds, ee::dsp::spring::kMaxDecaySeconds);
    decayRange.setSkewForCentre (ee::dsp::spring::kDecaySkewCentre);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDecayID, 1 }, "Decay", decayRange, ee::dsp::spring::kDefaultDecaySeconds,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (secondsToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kMixID, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        ee::dsp::spring::kDefaultMixPercent,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    // A real tank is a mono device. Stereo runs a second tank whose springs are
    // a few per cent different; mono runs one and feeds both outputs from it.
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kStereoID, 1 }, "Stereo", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakSpringProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    spring.prepare (sampleRate);
    spring.setDecayTime (decayParam->load());
    spring.setStereo (stereoParam->load() > 0.5f);
    spring.reset();

    monoBuffer.setSize (1, maxBlock, false, true, true);
    wetBuffer.setSize (2, maxBlock, false, true, true);

    dryGain.reset (sampleRate, kGainRampSeconds);
    wetGain.reset (sampleRate, kGainRampSeconds);
    inputGain.reset (sampleRate, kGainRampSeconds);

    const float mix = shapedMix (mixParam->load());
    const bool engaged = onParam->load() > 0.5f;

    dryGain.setCurrentAndTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setCurrentAndTargetValue (wetGainFor (mix));
    inputGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void PeakSpringProcessor::releaseResources()
{
    spring.reset();
}

double PeakSpringProcessor::getTailLengthSeconds() const
{
    return static_cast<double> (spring.getTailSeconds());
}

bool PeakSpringProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void PeakSpringProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    spring.setDecayTime (decayParam->load());
    spring.setStereo (stereoParam->load() > 0.5f);

    // Trails: bypassing stops driving the tank but leaves the wet path open, so
    // whatever is still ringing rings out instead of being cut off.
    dryGain.setTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setTargetValue (wetGainFor (mix));
    inputGain.setTargetValue (engaged ? 1.0f : 0.0f);

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int chunk = juce::jmin (maxBlock, numSamples - offset);

        float* mono = monoBuffer.getWritePointer (0);
        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : inL;

        // A tank is a mono device: it is driven by one transducer whatever is
        // playing into it.
        for (int i = 0; i < chunk; ++i)
            mono[i] = 0.5f * (inL[i] + inR[i]) * inputGain.getNextValue();

        float* wetL = wetBuffer.getWritePointer (0);
        float* wetR = wetBuffer.getWritePointer (1);
        spring.process (mono, wetL, wetR, chunk);

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

juce::AudioProcessorEditor* PeakSpringProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Spring";
    spec.tagline = "Dispersive spring tank";
    spec.version = "v" JucePlugin_VersionString;

    // One text row per knob: the caption at rest, the reading only while the
    // knob is actually being turned.
    spec.knobs = {
        { .parameterID = kMixID, .caption = "Mix", .captionUntilTouched = true },
        { .parameterID = kDecayID, .caption = "Decay", .captionUntilTouched = true },
    };

    // Mono / Stereo hangs under the Mix knob, between the two caps.
    spec.toggles = { { .parameterID = kStereoID,
                       .caption = "Stereo",
                       .afterKnobIndex = 0,
                       .centeredBelow = true,
                       .belowGap = 10,
                       .asSwitch = ee::ui::SlideToggleSpec { .labelOff = "Mono", .labelOn = "Stereo" } } };

    // One knob per row - the same narrow two-control face as Peak Phase.
    spec.knobsPerRow = 1;

    // Wide enough that the switch sits the same distance off the Decay cap
    // below it as off the Mix cap above. The shared gap would have the switch
    // overlapping the lower cap outright.
    spec.knobRowGap = 64;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);

    return new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::charcoal());
}

void PeakSpringProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakSpringProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakSpringProcessor();
}
