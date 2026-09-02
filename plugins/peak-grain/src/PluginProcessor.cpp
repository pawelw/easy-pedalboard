#include "PluginProcessor.h"

#include "ee/dsp/GrainerConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#if EE_GRAIN_TUNER
#include "GrainTunerPanel.h"
#endif

namespace
{
using ee::plugin::percentToText;

constexpr const char* kSizeID = "size";
constexpr const char* kDensityID = "density";
constexpr const char* kTimeID = "time";
constexpr const char* kFeedbackID = "feedback";
constexpr const char* kStretchID = "stretch";
constexpr const char* kFreezeID = "freeze";
constexpr const char* kShapeID = "shape";
constexpr const char* kScatterID = "scatter";
constexpr const char* kReverseID = "reverse";
constexpr const char* kStereoID = "stereo";
constexpr const char* kDetuneID = "detune";
constexpr const char* kPitchLowID = "plow";
constexpr const char* kPitchUnisonID = "puni";
constexpr const char* kPitchHighID = "phigh";
constexpr const char* kReverbID = "reverb";
constexpr const char* kMixID = "mix";
constexpr const char* kOnID = "on";

// The reverb network is normalised to ~0.42 RMS gain; this is Peak Reverb's
// trim, kept so a given Decay lands at the same level on both pedals.
constexpr float kWetTrim = 1.1f;

constexpr float kGainRampSeconds = ee::plugin::kRampSeconds;

juce::String millisecondsToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " ms";
}

juce::String centsToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " ct";
}

/** "12 /s" - grains per second. Deliberately not "Hz": at 12 of them a second
    this is a rate you count, not a pitch. */
juce::String grainsPerSecondToText (float value, int)
{
    return juce::String (value, value < 10.0f ? 1 : 0) + " /s";
}

/** Time reads in seconds once it is past one, because past a second it is a
    wash you wait out rather than a delay you feel. */
juce::String timeToText (float value, int)
{
    if (value >= 1000.0f)
        return juce::String (value * 0.001f, 2) + " s";

    return juce::String (juce::roundToInt (value)) + " ms";
}

/** Stretch is bipolar: a signed percent, or "hold" at the centre detent. */
juce::String signedPercentToText (float value, int)
{
    const int pct = juce::roundToInt (value);
    if (pct == 0)
        return "hold";

    return (pct > 0 ? "+" : "") + juce::String (pct) + " %";
}
} // namespace

float PeakGrainProcessor::reverbDecaySeconds (float percent) noexcept
{
    // One knob opens the mix and lengthens the decay together. The decay is
    // taken over the top half of the travel only: the bottom half is a short
    // room getting louder, which is what "a little reverb" should mean, and
    // only past halfway does it start to become a long tail.
    const float t = juce::jlimit (0.0f, 1.0f, (percent * 0.01f - 0.5f) * 2.0f);

    return ee::dsp::FdnReverb::kMinDecay + t * t * (ee::dsp::FdnReverb::kMaxDecay - ee::dsp::FdnReverb::kMinDecay);
}

PeakGrainProcessor::PeakGrainProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    sizeParam = apvts.getRawParameterValue (kSizeID);
    densityParam = apvts.getRawParameterValue (kDensityID);
    timeParam = apvts.getRawParameterValue (kTimeID);
    feedbackParam = apvts.getRawParameterValue (kFeedbackID);
    stretchParam = apvts.getRawParameterValue (kStretchID);
    freezeParam = apvts.getRawParameterValue (kFreezeID);
    shapeParam = apvts.getRawParameterValue (kShapeID);
    scatterParam = apvts.getRawParameterValue (kScatterID);
    reverseParam = apvts.getRawParameterValue (kReverseID);
    stereoParam = apvts.getRawParameterValue (kStereoID);
    detuneParam = apvts.getRawParameterValue (kDetuneID);
    pitchLowParam = apvts.getRawParameterValue (kPitchLowID);
    pitchUnisonParam = apvts.getRawParameterValue (kPitchUnisonID);
    pitchHighParam = apvts.getRawParameterValue (kPitchHighID);
    reverbParam = apvts.getRawParameterValue (kReverbID);
    mixParam = apvts.getRawParameterValue (kMixID);
    onParam = apvts.getRawParameterValue (kOnID);

#if EE_GRAIN_TRACE
    trace = std::make_unique<GrainTrace> (apvts);
#endif
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakGrainProcessor::createParameterLayout()
{
    namespace cfg = ee::dsp::config;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto msAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (millisecondsToText);

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto percentAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    // Time, Feedback and Stretch drive the delay half of the effect.
    auto timeRange = juce::NormalisableRange<float> (cfg::kMinTimeMs, cfg::kMaxTimeMs);
    timeRange.setSkewForCentre (cfg::kTimeSkewMs);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTimeID, 1 }, "Time", timeRange, cfg::kDefaultTimeMs,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (timeToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kFeedbackID, 1 }, "Feedback", percent,
                                                             cfg::kDefaultFeedbackPct, percentAttributes));

    // Bipolar: minus is a backwards scan of the frozen buffer, plus is forward,
    // zero holds it. Inert while playing live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kStretchID, 1 }, "Stretch", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        cfg::kDefaultStretchPct,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (signedPercentToText)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kFreezeID, 1 }, "Freeze", false));

    // Size, Density and Scatter are all skewed - or, for Scatter, plain percent -
    // so their useful low ends get most of the travel; the top of each range is
    // a special effect, not a setting.
    auto sizeRange = juce::NormalisableRange<float> (cfg::kMinGrainMs, cfg::kMaxGrainMs);
    sizeRange.setSkewForCentre (cfg::kGrainSkewMs);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kSizeID, 1 }, "Size", sizeRange,
                                                             cfg::kDefaultGrainMs, msAttributes));

    auto densityRange = juce::NormalisableRange<float> (cfg::kMinDensityHz, cfg::kMaxDensityHz);
    densityRange.setSkewForCentre (cfg::kDensitySkewHz);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDensityID, 1 }, "Density", densityRange, cfg::kDefaultDensityHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (grainsPerSecondToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kShapeID, 1 }, "Shape", percent,
                                                             cfg::kDefaultShapePct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kScatterID, 1 }, "Scatter", percent,
                                                             cfg::kDefaultScatterPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kReverseID, 1 }, "Reverse", percent,
                                                             cfg::kDefaultReversePct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kStereoID, 1 }, "Stereo", percent,
                                                             cfg::kDefaultStereoPct, percentAttributes));

    auto detuneRange = juce::NormalisableRange<float> (cfg::kMinDetuneCents, cfg::kMaxDetuneCents);
    detuneRange.setSkewForCentre (cfg::kDetuneSkewCents);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDetuneID, 1 }, "Detune", detuneRange, cfg::kDefaultDetuneCents,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (centsToText)));

    // The three pitch groups are weights against each other, not a position on
    // one scale, so each gets its own knob and they are free to overlap.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchLowID, 1 }, "Pitch Low", percent,
                                                             cfg::kDefaultPitchLowPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchUnisonID, 1 }, "Pitch Unison",
                                                             percent, cfg::kDefaultPitchUnisonPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchHighID, 1 }, "Pitch High",
                                                             percent, cfg::kDefaultPitchHighPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kReverbID, 1 }, "Reverb", percent,
                                                             cfg::kDefaultReverbPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMixID, 1 }, "Mix", percent,
                                                             cfg::kDefaultGrainMixPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakGrainProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    grainer.prepare (sampleRate);
    grainer.reset();

    reverb.prepare (sampleRate);
    reverb.reset();

    // Set here as well as per block: a host asks for getTailLengthSeconds()
    // before it ever calls processBlock, and without this it is answered from
    // FdnReverb's own default decay rather than the knob.
    reverb.setDecayTime (reverbDecaySeconds (reverbParam->load()));

    // Fixed for the life of the plugin: Peak Grain runs the network plain.
    // Read back from the engine's own tuning, so a value the dev panel has
    // changed survives the host re-preparing us.
    const auto& tuning = grainer.getTuning();
    reverb.setResonance (tuning.verbResonance);
    reverb.setShimmer (ee::dsp::config::kVerbShimmer);
    reverb.setLowCut (tuning.verbLowCutHz);

    grainBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
    monoBuffer.setSize (1, maxBlock, false, true, true);
    verbBuffer.setSize (kMaxChannels, maxBlock, false, true, true);

    for (auto* g : { &dryGain, &wetGain, &inputGain, &grainGain, &verbGain })
        g->reset (sampleRate, kGainRampSeconds);

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const float verb = juce::jlimit (0.0f, 1.0f, reverbParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    dryGain.setCurrentAndTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setCurrentAndTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi));
    inputGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
    grainGain.setCurrentAndTargetValue (std::cos (verb * juce::MathConstants<float>::halfPi));
    verbGain.setCurrentAndTargetValue (std::sin (verb * juce::MathConstants<float>::halfPi) * kWetTrim);
}

void PeakGrainProcessor::releaseResources()
{
    grainer.reset();
    reverb.reset();
}

double PeakGrainProcessor::getTailLengthSeconds() const
{
    return static_cast<double> (grainer.getTailSeconds() + reverb.getTailSeconds());
}

bool PeakGrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // The whole point of the effect is where the grains land across the image,
    // so a mono output from a stereo input would throw away the best of it.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

juce::String PeakGrainProcessor::densityReadout() const
{
    return grainsPerSecondToText (densityParam->load(), 0);
}

juce::String PeakGrainProcessor::stretchReadout() const
{
    if (freezeParam->load() <= 0.5f)
        return juce::String::fromUTF8 ("\xe2\x80\x94"); // em dash: Stretch does nothing while live

    return signedPercentToText (stretchParam->load(), 0);
}

void PeakGrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numOut == 0 || numSamples == 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

#if EE_GRAIN_TRACE
    float inputPeak = 0.0f;
    for (int ch = 0; ch < numIn; ++ch)
        inputPeak = juce::jmax (inputPeak, buffer.getMagnitude (ch, 0, numSamples));
#endif

    grainer.setSizeMs (sizeParam->load());
    grainer.setDensityHz (densityParam->load());
    grainer.setTimeMs (timeParam->load());
    grainer.setFeedback (feedbackParam->load() * 0.01f);
    grainer.setStretch (stretchParam->load() * 0.01f);
    grainer.setFreeze (freezeParam->load() > 0.5f);
    grainer.setShape (shapeParam->load() * 0.01f);
    grainer.setScatter (scatterParam->load() * 0.01f);
    grainer.setReverse (reverseParam->load() * 0.01f);
    grainer.setStereo (stereoParam->load() * 0.01f);
    grainer.setDetuneCents (detuneParam->load());
    grainer.setPitchMix (pitchLowParam->load(), pitchUnisonParam->load(), pitchHighParam->load());

    reverb.setDecayTime (reverbDecaySeconds (reverbParam->load()));

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const float verb = juce::jlimit (0.0f, 1.0f, reverbParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    // Trails, the same deal as Peak Reverb: bypassing stops feeding the grain
    // buffer and the reverb, but leaves the wet path open so the cloud and its
    // tail ring out instead of being chopped off mid-decay.
    dryGain.setTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi));
    inputGain.setTargetValue (engaged ? 1.0f : 0.0f);
    grainGain.setTargetValue (std::cos (verb * juce::MathConstants<float>::halfPi));
    verbGain.setTargetValue (std::sin (verb * juce::MathConstants<float>::halfPi) * kWetTrim);

    // Everything below writes into scratch buffers that prepareToPlay sizes. If
    // it has not run - or ran for a smaller block than the host is now handing
    // us - the honest thing is to pass the audio through untouched rather than
    // to write past the end of them.
    const int scratch =
        juce::jmin (grainBuffer.getNumSamples(), juce::jmin (monoBuffer.getNumSamples(), verbBuffer.getNumSamples()));

    if (scratch <= 0 || grainBuffer.getNumChannels() < kMaxChannels || verbBuffer.getNumChannels() < kMaxChannels)
        return;

    const int step = juce::jmin (maxBlock, scratch);

    for (int offset = 0; offset < numSamples; offset += step)
    {
        const int chunk = juce::jmin (step, numSamples - offset);

        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : nullptr;

        float* grainL = grainBuffer.getWritePointer (0);
        float* grainR = grainBuffer.getWritePointer (1);

        // The engine's own input gate, so bypass stops recording rather than
        // muting - grains already in flight still have their source.
        for (int i = 0; i < chunk; ++i)
        {
            const float g = inputGain.getNextValue();
            grainL[i] = inL[i] * g;
            grainR[i] = (inR != nullptr ? inR[i] : inL[i]) * g;
        }

        grainer.process (grainL, grainR, grainL, grainR, chunk);

        // The reverb hears the grains and nothing else - the dry signal stays
        // out of it, which is what keeps the effect from washing out the note
        // that triggered it.
        float* mono = monoBuffer.getWritePointer (0);
        for (int i = 0; i < chunk; ++i)
            mono[i] = 0.5f * (grainL[i] + grainR[i]);

        float* wetL = verbBuffer.getWritePointer (0);
        float* wetR = verbBuffer.getWritePointer (1);
        reverb.process (mono, wetL, wetR, chunk);

        float* outL = buffer.getWritePointer (0, offset);
        float* outR = numOut > 1 ? buffer.getWritePointer (1, offset) : nullptr;

        for (int i = 0; i < chunk; ++i)
        {
            const float dg = dryGain.getNextValue();
            const float wg = wetGain.getNextValue();
            const float gg = grainGain.getNextValue();
            const float vg = verbGain.getNextValue();

            // A mono output bus cannot carry where the grains landed, so it
            // gets the fold-down rather than half the cloud.
            const float cloudL = outR != nullptr ? grainL[i] : 0.5f * (grainL[i] + grainR[i]);
            const float cloudR = outR != nullptr ? grainR[i] : 0.0f;
            const float tailL = outR != nullptr ? wetL[i] : 0.5f * (wetL[i] + wetR[i]);
            const float tailR = outR != nullptr ? wetR[i] : 0.0f;

            float l = outL[i] * dg + (cloudL * gg + tailL * vg) * wg;
            float r = outR != nullptr ? outR[i] * dg + (cloudR * gg + tailR * vg) * wg : 0.0f;

            // Bypass.h makes the point that this guard is not optional even for
            // an engine that cannot produce a NaN itself: a non-finite sample
            // handed on gets latched into the tail of the next feedback effect
            // in the chain and roars.
            if (! std::isfinite (l))
                l = 0.0f;
            if (! std::isfinite (r))
                r = 0.0f;

            outL[i] = l;
            if (outR != nullptr)
                outR[i] = r;
        }
    }

#if EE_GRAIN_TRACE
    float outputPeak = 0.0f;
    for (int ch = 0; ch < numOut; ++ch)
        outputPeak = juce::jmax (outputPeak, buffer.getMagnitude (ch, 0, numSamples));

    trace->observe (inputPeak, outputPeak);
#endif
}

juce::AudioProcessorEditor* PeakGrainProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Grain";
    spec.tagline = "Granular delay into a plate";
    spec.version = "v" JucePlugin_VersionString;

    // Four captioned boxes, one per row, then Reverb and Mix bare underneath.
    // The knobs are consumed in order by `knobGroups`; the two left over form
    // the trailing row.
    spec.knobs = {
        { kTimeID, "Time" },
        { kFeedbackID, "Feedback" },
        { .parameterID = kStretchID,
          .caption = "Stretch",
          .bipolarArc = true,
          .centreDetent = true,
          .liveValueText = [this] { return stretchReadout(); } },

        { kSizeID, "Size" },
        { .parameterID = kDensityID, .caption = "Density", .liveValueText = [this] { return densityReadout(); } },
        { kShapeID, "Shape" },

        { .parameterID = kPitchLowID, .caption = "Low" },
        { .parameterID = kPitchUnisonID, .caption = "Unison" },
        { .parameterID = kPitchHighID, .caption = "High" },
        { kDetuneID, "Detune" },

        { kReverseID, "Reverse" },
        { kScatterID, "Scatter" },
        { kStereoID, "Stereo" },

        { kReverbID, "Reverb" },
        { kMixID, "Mix" },
    };

    spec.knobGroups = {
        { "Delay", 3 },
        { "Grain", 3 },
        { "Pitch", 4 },
        { "Random", 3 },
    };

    // Live / Freeze rides in the strip across the top; the Stretch readout
    // follows it (a dash while live, a signed percent once frozen).
    spec.slideToggle = ee::ui::SlideToggleSpec { .parameterID = kFreezeID, .labelOff = "Live", .labelOn = "Freeze" };

    // Logo and name share the bottom row, which buys back the title row for the
    // fifth rank of knobs.
    spec.titleBesideLogo = true;

    spec.knobsPerRow = 4; // width only; `knobGroups` drives the row layout
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);

    // Five ranks of knobs plus the switch strip: smaller caps, a wide row gap so
    // the captioned boxes clear each other, and a much taller face.
    spec.knobDiameter = 82;
    spec.knobRowGap = 38;
    spec.height = 960;

    auto* editor = new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::onyx());

#if EE_GRAIN_TUNER
    // The panel owns the voicing while it is open: the grain half goes to the
    // engine, the two reverb fields straight to the network.
    editor->setSidePanel (std::make_unique<GrainTunerPanel> (grainer.getTuning(),
                                                             [this] (const ee::dsp::GrainerTuning& t)
                                                             {
                                                                 grainer.setTuning (t);
                                                                 reverb.setResonance (t.verbResonance);
                                                                 reverb.setLowCut (t.verbLowCutHz);
                                                             }),
                          GrainTunerPanel::preferredWidth);
#endif

    return editor;
}

void PeakGrainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakGrainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakGrainProcessor();
}
