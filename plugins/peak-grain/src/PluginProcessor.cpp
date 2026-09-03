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
constexpr const char* kSizeSyncID = "ssync";
constexpr const char* kDensitySyncID = "dsync";
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
constexpr const char* kDelayTimeID = "dtime";
constexpr const char* kDelaySyncID = "dtsync";
constexpr const char* kDelayFeedbackID = "dfb";
constexpr const char* kDelayMixID = "dmix";
constexpr const char* kDecayID = "decay";
constexpr const char* kReverbMixID = "rmix";
constexpr const char* kMixID = "mix";
constexpr const char* kOnID = "on";

// State-tree properties: the knob position each Sync switch is not currently
// showing, so flipping the switch and flipping it back lands where it started.
constexpr const char* kSizeFreeProp = "sizeFree01";
constexpr const char* kSizeSyncProp = "sizeSync01";
constexpr const char* kDensityFreeProp = "densityFree01";
constexpr const char* kDensitySyncProp = "densitySync01";
constexpr const char* kDelayFreeProp = "delayFree01";
constexpr const char* kDelaySyncProp = "delaySync01";

// The reverb network is normalised to ~0.42 RMS gain; this is Peak Reverb's
// trim, kept so a given Decay lands at the same level on both pedals.
constexpr float kWetTrim = 1.1f;

constexpr float kGainRampSeconds = ee::plugin::kRampSeconds;

juce::String centsToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " ct";
}

/** Time reads in seconds once it is past one, because past a second it is a
    wash you wait out rather than a delay you feel. Kept for the now-hidden
    granular Time parameter's host-facing text. */
juce::String timeToText (float value, int)
{
    if (value >= 1000.0f)
        return juce::String (value * 0.001f, 2) + " s";

    return juce::String (juce::roundToInt (value)) + " ms";
}

/** Stretch is bipolar: a signed percent, or "hold" at the centre detent. Kept
    for the now-hidden Stretch parameter's host-facing text. */
juce::String signedPercentToText (float value, int)
{
    const int pct = juce::roundToInt (value);
    if (pct == 0)
        return "hold";

    return (pct > 0 ? "+" : "") + juce::String (pct) + " %";
}

juce::String decaySecondsToText (float value, int)
{
    return juce::String (value, 2) + " s";
}

/** The three tempo-sync maps, built once from GrainerConfig ranges. Size and
    the delay Time are durations (knob up = longer, free unit ms); Density is a
    rate (knob up = faster, free unit grains/second). */
ee::dsp::GrainSyncMap makeSizeMap()
{
    namespace cfg = ee::dsp::config;
    juce::NormalisableRange<float> r (cfg::kMinGrainMs, cfg::kMaxGrainMs);
    r.setSkewForCentre (cfg::kGrainSkewMs);
    return { r, true };
}

ee::dsp::GrainSyncMap makeDensityMap()
{
    namespace cfg = ee::dsp::config;
    juce::NormalisableRange<float> r (cfg::kMinDensityHz, cfg::kMaxDensityHz);
    r.setSkewForCentre (cfg::kDensitySkewHz);
    return { r, false };
}

ee::dsp::GrainSyncMap makeDelayMap()
{
    namespace cfg = ee::dsp::config;
    juce::NormalisableRange<float> r (cfg::kMinTimeMs, cfg::kMaxTimeMs);
    r.setSkewForCentre (cfg::kTimeSkewMs);
    return { r, true };
}

/** A plain "ms" wordmark, borrowed from Peak Delay: the button that swaps a
    knob's reading from a note division to that division's length in
    milliseconds. Same glyph the other tempo-sync pedals carry. */
void drawMsIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto box = area.withSizeKeepingCentre (area.getWidth() * 1.6f, area.getHeight());
    g.setColour (colour);
    g.setFont (juce::Font (juce::FontOptions (area.getHeight() * 0.95f)).boldened());
    g.drawText ("ms", box, juce::Justification::centred, false);
}
} // namespace

PeakGrainProcessor::PeakGrainProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    sizeMap = makeSizeMap();
    densityMap = makeDensityMap();
    delayMap = makeDelayMap();

    sizeParam = apvts.getRawParameterValue (kSizeID);
    densityParam = apvts.getRawParameterValue (kDensityID);
    sizeSyncParam = apvts.getRawParameterValue (kSizeSyncID);
    densitySyncParam = apvts.getRawParameterValue (kDensitySyncID);
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
    delayTimeParam = apvts.getRawParameterValue (kDelayTimeID);
    delaySyncParam = apvts.getRawParameterValue (kDelaySyncID);
    delayFeedbackParam = apvts.getRawParameterValue (kDelayFeedbackID);
    delayMixParam = apvts.getRawParameterValue (kDelayMixID);
    decayParam = apvts.getRawParameterValue (kDecayID);
    reverbMixParam = apvts.getRawParameterValue (kReverbMixID);
    mixParam = apvts.getRawParameterValue (kMixID);
    onParam = apvts.getRawParameterValue (kOnID);

    // Seed both mode slots from the parameters' defaults, so the first flip of a
    // Sync switch has somewhere sensible to land before the user has set it.
    sizeFree01 = sizeParam->load();
    sizeSync01 = sizeParam->load();
    densityFree01 = densityParam->load();
    densitySync01 = densityParam->load();
    delayFree01 = delayTimeParam->load();
    delaySync01 = delayTimeParam->load();

#if EE_GRAIN_TRACE
    trace = std::make_unique<GrainTrace> (apvts);
#endif
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakGrainProcessor::createParameterLayout()
{
    namespace cfg = ee::dsp::config;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto percentAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    const auto unit = juce::NormalisableRange<float> (0.0f, 1.0f);

    // Size and Density are one normalised knob each; the Sync switch beside them
    // decides whether that maps to a free unit or a note division. The
    // host-facing text assumes the free reading - the editor overrides it with
    // one that follows the switch and the host tempo.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kSizeID, 1 }, "Size", unit, cfg::kDefaultSize01,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return makeSizeMap().toText (v, false, 120.0); })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDensityID, 1 }, "Density", unit, cfg::kDefaultDensity01,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return makeDensityMap().toText (v, false, 120.0); })));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSizeSyncID, 1 }, "Size Sync",
                                                            cfg::kDefaultSizeSync));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDensitySyncID, 1 }, "Density Sync",
                                                            cfg::kDefaultDensitySync));

    // The granular delay half - Time, Feedback, Stretch - is no longer on the
    // face, but the parameters and the engine wiring stay: the cloud is still a
    // granular delay, it just runs at these fixed defaults now.
    auto timeRange = juce::NormalisableRange<float> (cfg::kMinTimeMs, cfg::kMaxTimeMs);
    timeRange.setSkewForCentre (cfg::kTimeSkewMs);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTimeID, 1 }, "Time", timeRange, cfg::kDefaultTimeMs,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (timeToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kFeedbackID, 1 }, "Feedback", percent,
                                                             cfg::kDefaultFeedbackPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kStretchID, 1 }, "Stretch", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        cfg::kDefaultStretchPct,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (signedPercentToText)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kFreezeID, 1 }, "Freeze", false));

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

    // The post delay: a clean digital delay after the grain stage, before the
    // reverb. One normalised Time knob with its own Sync switch, plus Feedback
    // and Mix.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDelayTimeID, 1 }, "Delay Time", unit, cfg::kDefaultDelayTime01,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return makeDelayMap().toText (v, false, 120.0); })));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDelaySyncID, 1 }, "Delay Sync",
                                                            cfg::kDefaultDelaySync));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDelayFeedbackID, 1 },
                                                             "Delay Feedback", percent, cfg::kDefaultDelayFeedbackPct,
                                                             percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDelayMixID, 1 }, "Delay Mix", percent,
                                                             cfg::kDefaultDelayMixPct, percentAttributes));

    // The reverb now hears the whole post-delay blend. Decay is straight
    // seconds onto the network; Mix is its own dry/wet.
    auto decayRange = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDecayID, 1 }, "Decay", decayRange, cfg::kDefaultReverbDecaySeconds,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (decaySecondsToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kReverbMixID, 1 }, "Reverb Mix",
                                                             percent, cfg::kDefaultReverbMixPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMixID, 1 }, "Mix", percent,
                                                             cfg::kDefaultGrainMixPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

double PeakGrainProcessor::currentBpm() const
{
    double bpm = 120.0;

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    return juce::jlimit (20.0, 300.0, bpm);
}

juce::String PeakGrainProcessor::sizeReadout() const
{
    return sizeMap.toText (sizeParam->load(), sizeSyncParam->load() > 0.5f, currentBpm());
}

juce::String PeakGrainProcessor::densityReadout() const
{
    return densityMap.toText (densityParam->load(), densitySyncParam->load() > 0.5f, currentBpm());
}

juce::String PeakGrainProcessor::delayTimeReadout() const
{
    return delayMap.toText (delayTimeParam->load(), delaySyncParam->load() > 0.5f, currentBpm());
}

void PeakGrainProcessor::syncToggled (const char* paramID,
                                      std::atomic<float>& freeSlot,
                                      std::atomic<float>& syncSlot,
                                      const std::atomic<float>* syncFlag)
{
    // The bool has already flipped to its new state by the time this click
    // callback runs.
    const bool nowSynced = syncFlag != nullptr && syncFlag->load() > 0.5f;

    auto* parameter = apvts.getParameter (paramID);
    if (parameter == nullptr)
        return;

    const float current = parameter->getValue(); // normalised, 0..1

    // Remember where the mode we are leaving was left; restore the mode we are
    // entering to where it was last.
    if (nowSynced)
        freeSlot.store (current);
    else
        syncSlot.store (current);

    const float target = nowSynced ? syncSlot.load() : freeSlot.load();
    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, target));
}

void PeakGrainProcessor::onSizeSyncToggled()
{
    syncToggled (kSizeID, sizeFree01, sizeSync01, sizeSyncParam);
}

void PeakGrainProcessor::onDensitySyncToggled()
{
    syncToggled (kDensityID, densityFree01, densitySync01, densitySyncParam);
}

void PeakGrainProcessor::onDelaySyncToggled()
{
    syncToggled (kDelayTimeID, delayFree01, delaySync01, delaySyncParam);
}

void PeakGrainProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    grainer.prepare (sampleRate);
    grainer.reset();

    delay.prepare (sampleRate);
    delay.reset();
    delay.setModulation (0.0f);
    snapDelayNextBlock = true;

    reverb.prepare (sampleRate);
    reverb.reset();

    // Set here as well as per block: a host asks for getTailLengthSeconds()
    // before it ever calls processBlock, and without this it is answered from
    // FdnReverb's own default decay rather than the knob.
    reverb.setDecayTime (decayParam->load());

    // Fixed for the life of the plugin: Peak Grain runs the network plain. Read
    // back from the engine's own tuning, so a value the dev panel has changed
    // survives the host re-preparing us.
    const auto& tuning = grainer.getTuning();
    reverb.setResonance (tuning.verbResonance);
    reverb.setShimmer (ee::dsp::config::kVerbShimmer);
    reverb.setLowCut (tuning.verbLowCutHz);

    grainBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
    stageBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
    delayInBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
    delayWetBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
    monoBuffer.setSize (1, maxBlock, false, true, true);
    verbBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
    engageBuffer.setSize (1, maxBlock, false, true, true);

    for (auto* g : { &grainDry, &grainWet, &delayDry, &delayWet, &reverbDry, &reverbWet, &engageGain })
        g->reset (sampleRate, kGainRampSeconds);

    const float hp = juce::MathConstants<float>::halfPi;
    const float gMix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const float dMix = juce::jlimit (0.0f, 1.0f, delayMixParam->load() * 0.01f);
    const float rMix = juce::jlimit (0.0f, 1.0f, reverbMixParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    grainDry.setCurrentAndTargetValue (engaged ? std::cos (gMix * hp) : 1.0f);
    grainWet.setCurrentAndTargetValue (std::sin (gMix * hp));
    delayDry.setCurrentAndTargetValue (engaged ? std::cos (dMix * hp) : 1.0f);
    delayWet.setCurrentAndTargetValue (std::sin (dMix * hp));
    reverbDry.setCurrentAndTargetValue (engaged ? std::cos (rMix * hp) : 1.0f);
    reverbWet.setCurrentAndTargetValue (std::sin (rMix * hp) * kWetTrim);
    engageGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void PeakGrainProcessor::releaseResources()
{
    grainer.reset();
    delay.reset();
    reverb.reset();
}

double PeakGrainProcessor::getTailLengthSeconds() const
{
    return static_cast<double> (grainer.getTailSeconds() + delay.getTailSeconds() + reverb.getTailSeconds());
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

    const double bpm = currentBpm();
    const bool sizeSynced = sizeSyncParam->load() > 0.5f;
    const bool densitySynced = densitySyncParam->load() > 0.5f;
    const bool delaySynced = delaySyncParam->load() > 0.5f;

    grainer.setSizeMs (sizeMap.value (sizeParam->load(), sizeSynced, bpm));
    grainer.setDensityHz (densityMap.value (densityParam->load(), densitySynced, bpm));
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

    const float delaySecs = delayMap.value (delayTimeParam->load(), delaySynced, bpm) * 0.001f;
    delay.setDelaySeconds (delaySecs, delaySecs);
    delay.setFeedback (delayFeedbackParam->load() * 0.01f);
    delay.setModulation (0.0f);

    if (snapDelayNextBlock)
    {
        delay.snapDelays();
        snapDelayNextBlock = false;
    }

    reverb.setDecayTime (decayParam->load());

    const float hp = juce::MathConstants<float>::halfPi;
    const float gMix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const float dMix = juce::jlimit (0.0f, 1.0f, delayMixParam->load() * 0.01f);
    const float rMix = juce::jlimit (0.0f, 1.0f, reverbMixParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    // Trails: bypassing opens every stage's dry leg to unity and closes its send
    // to zero, so the grain cloud, the delay repeats and the reverb tail all
    // ring out over the untouched input instead of being chopped off.
    grainDry.setTargetValue (engaged ? std::cos (gMix * hp) : 1.0f);
    grainWet.setTargetValue (std::sin (gMix * hp));
    delayDry.setTargetValue (engaged ? std::cos (dMix * hp) : 1.0f);
    delayWet.setTargetValue (std::sin (dMix * hp));
    reverbDry.setTargetValue (engaged ? std::cos (rMix * hp) : 1.0f);
    reverbWet.setTargetValue (std::sin (rMix * hp) * kWetTrim);
    engageGain.setTargetValue (engaged ? 1.0f : 0.0f);

    // Everything below writes into scratch buffers that prepareToPlay sizes. If
    // it has not run - or ran for a smaller block than the host is now handing
    // us - pass the audio through untouched rather than writing past the end.
    const int scratch =
        juce::jmin (juce::jmin (juce::jmin (grainBuffer.getNumSamples(), stageBuffer.getNumSamples()),
                                engageBuffer.getNumSamples()),
                    juce::jmin (juce::jmin (delayInBuffer.getNumSamples(), delayWetBuffer.getNumSamples()),
                                juce::jmin (monoBuffer.getNumSamples(), verbBuffer.getNumSamples())));

    if (scratch <= 0 || grainBuffer.getNumChannels() < kMaxChannels || stageBuffer.getNumChannels() < kMaxChannels ||
        delayInBuffer.getNumChannels() < kMaxChannels || delayWetBuffer.getNumChannels() < kMaxChannels ||
        verbBuffer.getNumChannels() < kMaxChannels)
        return;

    const int step = juce::jmin (maxBlock, scratch);

    for (int offset = 0; offset < numSamples; offset += step)
    {
        const int chunk = juce::jmin (step, numSamples - offset);

        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : nullptr;

        float* grainL = grainBuffer.getWritePointer (0);
        float* grainR = grainBuffer.getWritePointer (1);
        float* egBuf = engageBuffer.getWritePointer (0);

        // The engine's own input gate, so bypass stops recording rather than
        // muting - grains already in flight still have their source. The ramp is
        // sampled once here and reused by the delay and reverb send gates below,
        // so every gate agrees on a given sample.
        for (int i = 0; i < chunk; ++i)
        {
            const float g = engageGain.getNextValue();
            egBuf[i] = g;
            grainL[i] = inL[i] * g;
            grainR[i] = (inR != nullptr ? inR[i] : inL[i]) * g;
        }

        grainer.process (grainL, grainR, grainL, grainR, chunk);

        // Grain stage: the equal-power blend of the dry note and the cloud - the
        // signal an outboard delay would see at the grain pedal's output. The
        // finite guard here keeps a poisoned grain from latching into the delay
        // or reverb feedback further down.
        float* stageL = stageBuffer.getWritePointer (0);
        float* stageR = stageBuffer.getWritePointer (1);
        float* sendL = delayInBuffer.getWritePointer (0);
        float* sendR = delayInBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            const float gd = grainDry.getNextValue();
            const float gw = grainWet.getNextValue();
            const float eg = egBuf[i];
            const float dryR = inR != nullptr ? inR[i] : inL[i];

            float sL = inL[i] * gd + grainL[i] * gw;
            float sR = dryR * gd + grainR[i] * gw;

            if (! std::isfinite (sL))
                sL = 0.0f;
            if (! std::isfinite (sR))
                sR = 0.0f;

            stageL[i] = sL;
            stageR[i] = sR;
            sendL[i] = sL * eg; // gated copy: TapeDelay reads before it writes
            sendR[i] = sR * eg;
        }

        // Delay: the gated grain stage into the delay line, blended equal-power
        // back against the ungated stage.
        float* delL = delayWetBuffer.getWritePointer (0);
        float* delR = delayWetBuffer.getWritePointer (1);
        delay.process (sendL, sendR, delL, delR, chunk);

        float* mono = monoBuffer.getWritePointer (0);
        float* postL = stageBuffer.getWritePointer (0); // reuse: post-delay blend
        float* postR = stageBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            const float dd = delayDry.getNextValue();
            const float dw = delayWet.getNextValue();
            const float eg = egBuf[i];

            const float pL = stageL[i] * dd + delL[i] * dw;
            const float pR = stageR[i] * dd + delR[i] * dw;

            postL[i] = pL;
            postR[i] = pR;
            mono[i] = 0.5f * (pL + pR) * eg; // gated reverb send
        }

        float* verbL = verbBuffer.getWritePointer (0);
        float* verbR = verbBuffer.getWritePointer (1);
        reverb.process (mono, verbL, verbR, chunk);

        float* outL = buffer.getWritePointer (0, offset);
        float* outR = numOut > 1 ? buffer.getWritePointer (1, offset) : nullptr;

        for (int i = 0; i < chunk; ++i)
        {
            const float rd = reverbDry.getNextValue();
            const float rw = reverbWet.getNextValue();

            float l = postL[i] * rd + verbL[i] * rw;
            float r = postR[i] * rd + verbR[i] * rw;

            // Bypass.h makes the point that this guard is not optional even for
            // an engine that cannot produce a NaN itself: a non-finite sample
            // handed on gets latched into the tail of the next feedback effect
            // in the chain and roars.
            if (! std::isfinite (l))
                l = 0.0f;
            if (! std::isfinite (r))
                r = 0.0f;

            if (outR != nullptr)
            {
                outL[i] = l;
                outR[i] = r;
            }
            else
            {
                // A mono output bus cannot carry where the grains landed, so it
                // gets the fold-down rather than half the field.
                outL[i] = 0.5f * (l + r);
            }
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
    spec.tagline = "Granular delay into delay into plate";
    spec.version = "v" JucePlugin_VersionString;

    // Five captioned boxes, one row each. The knobs are consumed in order by
    // `knobGroups`.
    spec.knobs = {
        { .parameterID = kSizeID, .caption = "Size", .liveValueText = [this] { return sizeReadout(); } },
        { .parameterID = kDensityID, .caption = "Destiny", .liveValueText = [this] { return densityReadout(); } },
        { kShapeID, "Shape" },
        { kMixID, "Mix" },

        { .parameterID = kPitchLowID, .caption = "Low" },
        { .parameterID = kPitchUnisonID, .caption = "Unison" },
        { .parameterID = kPitchHighID, .caption = "High" },
        { kDetuneID, "Detune" },

        { kReverseID, "Reverse" },
        { kScatterID, "Scatter" },
        { kStereoID, "Stereo" },

        { .parameterID = kDelayTimeID, .caption = "Time", .liveValueText = [this] { return delayTimeReadout(); } },
        { kDelayFeedbackID, "Feedback" },
        { kDelayMixID, "Mix" },

        { kDecayID, "Decay" },
        { kReverbMixID, "Mix" },
    };

    spec.knobGroups = {
        { "Grain", 4 }, { "Pitch", 4 }, { "Random", 3 }, { "Delay", 3 }, { "Reverb", 2 },
    };

    // A Sync / ms button under Size, under Destiny, and under the delay Time
    // knob: pressed, the knob picks a note division and the reading is the
    // division label; released, it is the free unit at the host tempo. The
    // toggle is silent - `onClick` only nudges the knob to its remembered
    // position for the mode being entered.
    spec.toggles = {
        { .parameterID = kSizeSyncID,
          .caption = "Sync",
          .afterKnobIndex = 0,
          .centeredBelow = true,
          .belowGap = 10,
          .onClick = [this] { onSizeSyncToggled(); },
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kDensitySyncID,
          .caption = "Sync",
          .afterKnobIndex = 1,
          .centeredBelow = true,
          .belowGap = 10,
          .onClick = [this] { onDensitySyncToggled(); },
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kDelaySyncID,
          .caption = "Sync",
          .afterKnobIndex = 11,
          .centeredBelow = true,
          .belowGap = 10,
          .onClick = [this] { onDelaySyncToggled(); },
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
    };

    // Live / Freeze rides in the strip across the top.
    spec.slideToggle = ee::ui::SlideToggleSpec { .parameterID = kFreezeID, .labelOff = "Live", .labelOn = "Freeze" };

    // Logo and name share the bottom row, which buys back the title row for the
    // fifth rank of knobs.
    spec.titleBesideLogo = true;

    spec.knobsPerRow = 4; // width only; `knobGroups` drives the row layout
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);

    // Five ranks of knobs plus the switch strip, and now a Sync button hanging
    // under three of them: smaller caps, a wide row gap so those buttons clear
    // the captioned box below them (the same reason Peak Spring widens its gap),
    // and a tall face.
    spec.knobDiameter = 82;
    spec.knobRowGap = 64;
    spec.height = 1060;

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
    auto state = apvts.copyState();

    state.setProperty (kSizeFreeProp, sizeFree01.load(), nullptr);
    state.setProperty (kSizeSyncProp, sizeSync01.load(), nullptr);
    state.setProperty (kDensityFreeProp, densityFree01.load(), nullptr);
    state.setProperty (kDensitySyncProp, densitySync01.load(), nullptr);
    state.setProperty (kDelayFreeProp, delayFree01.load(), nullptr);
    state.setProperty (kDelaySyncProp, delaySync01.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakGrainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto restore = [this] (const char* prop, std::atomic<float>& slot, const std::atomic<float>* fallback)
            { slot.store (static_cast<float> (apvts.state.getProperty (prop, fallback->load()))); };

            restore (kSizeFreeProp, sizeFree01, sizeParam);
            restore (kSizeSyncProp, sizeSync01, sizeParam);
            restore (kDensityFreeProp, densityFree01, densityParam);
            restore (kDensitySyncProp, densitySync01, densityParam);
            restore (kDelayFreeProp, delayFree01, delayTimeParam);
            restore (kDelaySyncProp, delaySync01, delayTimeParam);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakGrainProcessor();
}
