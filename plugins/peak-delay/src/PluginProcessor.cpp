#include "PluginProcessor.h"

#include "ee/dsp/TempoDivision.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#if EE_TAPE_TUNER
#include "TapeTunerPanel.h"
#endif

namespace
{
using ee::plugin::percentToText;

constexpr const char* kLeftTimeID = "ltime";
constexpr const char* kRightTimeID = "rtime";
constexpr const char* kSyncID = "sync";
constexpr const char* kFeedbackID = "fb";
constexpr const char* kMixID = "mix";
constexpr const char* kModID = "mod";
constexpr const char* kTapeID = "tape";
constexpr const char* kOnID = "on";

constexpr int kDefaultDivision = 5; // 1/8

constexpr float kGainRampSeconds = 0.02f;

float divisionSeconds (int index, double bpm) noexcept
{
    const int i = juce::jlimit (0, ee::dsp::kNumTempoDivisions - 1, index);
    return ee::dsp::kTempoDivisions[i].beats * static_cast<float> (60.0 / bpm);
}

/** A chain link, for the Sync button: two capsule outlines lying along the same
    diagonal and overlapping in the middle. Sync ties the delay time to the
    host's tempo, which is a link rather than a word - and the button is too
    small to print one legibly. */
void drawLinkIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    if (side <= 0.0f)
        return;

    // All of it in fractions of the box, so the glyph is the same drawing at
    // any size.
    const float linkW = side * 0.44f;  // capsule across
    const float linkH = side * 0.74f;  // ... and along
    const float offset = side * 0.19f; // each capsule off the centre
    const float stroke = juce::jmax (1.2f, side * 0.11f);

    juce::Path capsule;
    capsule.addRoundedRectangle (-linkW * 0.5f, -linkH * 0.5f, linkW, linkH, linkW * 0.5f);

    const auto centre = area.getCentre();
    const float diagonal = offset * juce::MathConstants<float>::sqrt2 * 0.5f;

    g.setColour (colour);

    // Lower-left and upper-right, both turned onto the same 45-degree axis.
    for (const float sign : { -1.0f, 1.0f })
    {
        const auto place = juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f)
                               .translated (centre.x - sign * diagonal, centre.y + sign * diagonal);

        g.strokePath (
            capsule, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), place);
    }
}
} // namespace

PeakDelayProcessor::PeakDelayProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    leftTimeParam = apvts.getRawParameterValue (kLeftTimeID);
    rightTimeParam = apvts.getRawParameterValue (kRightTimeID);
    syncParam = apvts.getRawParameterValue (kSyncID);
    feedbackParam = apvts.getRawParameterValue (kFeedbackID);
    mixParam = apvts.getRawParameterValue (kMixID);
    modParam = apvts.getRawParameterValue (kModID);
    tapeParam = apvts.getRawParameterValue (kTapeID);
    onParam = apvts.getRawParameterValue (kOnID);

    apvts.addParameterListener (kLeftTimeID, this);
    apvts.addParameterListener (kRightTimeID, this);
    apvts.addParameterListener (kSyncID, this);
}

PeakDelayProcessor::~PeakDelayProcessor()
{
    apvts.removeParameterListener (kLeftTimeID, this);
    apvts.removeParameterListener (kRightTimeID, this);
    apvts.removeParameterListener (kSyncID, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakDelayProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto divisions = ee::dsp::tempoDivisionLabels();

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kLeftTimeID, 1 }, "Left Time",
                                                              divisions, kDefaultDivision));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kRightTimeID, 1 }, "Right Time",
                                                              divisions, kDefaultDivision));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSyncID, 1 }, "Sync L/R", true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kFeedbackID, 1 }, "Feedback", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kMixID, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kModID, 1 }, "Mod", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTapeID, 1 }, "Tape", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakDelayProcessor::mirrorDivision (const juce::String& from, const juce::String& to)
{
    auto* source = apvts.getParameter (from);
    auto* destination = apvts.getParameter (to);

    if (source == nullptr || destination == nullptr)
        return;

    const float value = source->getValue();

    if (std::abs (destination->getValue() - value) > 1.0e-6f)
        destination->setValueNotifyingHost (value);
}

void PeakDelayProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Read the button from the callback argument rather than the cached value:
    // the two are not guaranteed to be in step at this point.
    const bool synced = parameterID == kSyncID ? newValue > 0.5f : (syncParam != nullptr && syncParam->load() > 0.5f);

    if (! synced)
        return;

    if (mirroring.exchange (true))
        return;

    // Turning sync on adopts the left value, which is the one the user set last
    // in the common case of reaching for the button after dialling the left knob.
    if (parameterID == kRightTimeID)
        mirrorDivision (kRightTimeID, kLeftTimeID);
    else
        mirrorDivision (kLeftTimeID, kRightTimeID);

    mirroring = false;
}

void PeakDelayProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    tape.prepare (sampleRate);
    delay.prepare (sampleRate);

    setLatencySamples (tape.getLatencySamples());

    tapedBuffer.setSize (2, maxBlock, false, true, true);
    inputBuffer.setSize (2, maxBlock, false, true, true);
    wetBuffer.setSize (2, maxBlock, false, true, true);

    dryGain.reset (sampleRate, kGainRampSeconds);
    wetGain.reset (sampleRate, kGainRampSeconds);
    engageGain.reset (sampleRate, kGainRampSeconds);

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    tape.setAmount (tapeParam->load() * 0.01f);
    delay.setFeedback (feedbackParam->load() * 0.01f);
    delay.setModulation (modParam->load() * 0.01f);
    delay.setDelaySeconds (divisionSeconds (static_cast<int> (leftTimeParam->load()), 120.0),
                           divisionSeconds (static_cast<int> (rightTimeParam->load()), 120.0));
    delay.snapDelays();

    dryGain.setCurrentAndTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setCurrentAndTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi));
    engageGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void PeakDelayProcessor::releaseResources()
{
    tape.reset();
    delay.reset();
}

double PeakDelayProcessor::getTailLengthSeconds() const
{
    return static_cast<double> (delay.getTailSeconds());
}

bool PeakDelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

void PeakDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numOut == 0 || numSamples == 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // The time knobs are note values, so a host that reports no tempo still has
    // to land somewhere musical.
    double bpm = 120.0;

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    bpm = juce::jlimit (20.0, 300.0, bpm);

    delay.setDelaySeconds (divisionSeconds (static_cast<int> (leftTimeParam->load()), bpm),
                           divisionSeconds (static_cast<int> (rightTimeParam->load()), bpm));
    delay.setFeedback (feedbackParam->load() * 0.01f);
    delay.setModulation (modParam->load() * 0.01f);
    tape.setAmount (tapeParam->load() * 0.01f);

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    // Trails: bypassing closes the input but leaves the repeats running out.
    dryGain.setTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi));
    engageGain.setTargetValue (engaged ? 1.0f : 0.0f);

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int chunk = juce::jmin (maxBlock, numSamples - offset);

        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : inL;

        // The tape machine sits in front of everything, so it colours the dry
        // signal as well as what goes on to be repeated.
        float* tapedL = tapedBuffer.getWritePointer (0);
        float* tapedR = tapedBuffer.getWritePointer (1);

        juce::FloatVectorOperations::copy (tapedL, inL, chunk);
        juce::FloatVectorOperations::copy (tapedR, inR, chunk);

        tape.process (tapedL, tapedR, chunk);

        float* feedL = inputBuffer.getWritePointer (0);
        float* feedR = inputBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            const float e = engageGain.getNextValue();

            tapedL[i] = inL[i] + (tapedL[i] - inL[i]) * e;
            tapedR[i] = inR[i] + (tapedR[i] - inR[i]) * e;

            feedL[i] = tapedL[i] * e;
            feedR[i] = tapedR[i] * e;
        }

        float* wetL = wetBuffer.getWritePointer (0);
        float* wetR = wetBuffer.getWritePointer (1);
        delay.process (feedL, feedR, wetL, wetR, chunk);

        float* outL = buffer.getWritePointer (0, offset);
        float* outR = numOut > 1 ? buffer.getWritePointer (1, offset) : nullptr;

        for (int i = 0; i < chunk; ++i)
        {
            const float dg = dryGain.getNextValue();
            const float wg = wetGain.getNextValue();

            if (outR != nullptr)
            {
                outL[i] = tapedL[i] * dg + wetL[i] * wg;
                outR[i] = tapedR[i] * dg + wetR[i] * wg;
            }
            else
            {
                outL[i] = 0.5f * (tapedL[i] + tapedR[i]) * dg + 0.5f * (wetL[i] + wetR[i]) * wg;
            }
        }
    }
}

juce::AudioProcessorEditor* PeakDelayProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Delay";
    spec.tagline = "Tempo-synced stereo delay";
    spec.version = "v" JucePlugin_VersionString;

    // Tape is a machine in front of the delay rather than part of it, so its
    // knob is the odd one out twice over: a deep green cap, and the only
    // photographic one on a face of digital caps.
    const juce::Colour tapeCap { 0xff375916 };
    const juce::Colour tapeBorder { 0xff17280b };

    auto tape = ee::ui::KnobSpec { kTapeID, "Tape", tapeCap, tapeBorder, tapeCap };
    tape.capStyle = ee::ui::ControlStyle::analog;

    spec.knobs = { { kLeftTimeID, "Left Time" },
                   { kRightTimeID, "Right Time" },
                   { kFeedbackID, "Feedback" },
                   { kMixID, "Mix" },
                   { kModID, "Mod" },
                   tape };

    // Sync carries a chain link rather than a word: lit in the face's ink while
    // the delay follows the host, pale grey while it does not.
    spec.toggles = { { .parameterID = kSyncID, .caption = "Sync", .afterKnobIndex = 0, .icon = drawLinkIcon } };

    spec.knobsPerRow = 3;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow); // same column spacing as Peak Reverb

    auto* editor = new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::moss());

#if EE_TAPE_TUNER
    // Flip to true to bring the tuning panel back without reconfiguring CMake.
    constexpr bool showTuner = false;

    if (showTuner)
        editor->setSidePanel (std::make_unique<TapeTunerPanel> (tape.getTuning(), [this] (const ee::dsp::TapeTuning& t)
                                                                { tape.setTuning (t); }),
                              TapeTunerPanel::preferredWidth);
#endif

    return editor;
}
void PeakDelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakDelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakDelayProcessor();
}
