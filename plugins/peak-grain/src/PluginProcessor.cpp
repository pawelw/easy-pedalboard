#include "PluginProcessor.h"

#include "ee/dsp/GrainerConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#include <cmath>

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
constexpr const char* kVolumeID = "volume";

// Per-module enable switches, one per face panel.
constexpr const char* kGrainOnID = "grainon";
constexpr const char* kPitchOnID = "pitchon";
constexpr const char* kRandomOnID = "randon";
constexpr const char* kDelayOnID = "delon";
constexpr const char* kReverbOnID = "revon";

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

/** One grain envelope, for the Shape knob's cap - the same idea as Peak Wah's
    morphing LFO glyph, but the curve that morphs here is the grain window:
    `shape` leans it from soft (a slow rise into a gentle tail) at 0 to plucky
    (an instant attack into a sharp decay) at 1, matching `Grainer::setShape`. */
void drawGrainShapeIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float shape01)
{
    const auto r = area.reduced (area.getWidth() * 0.12f, area.getHeight() * 0.26f);
    const float s = juce::jlimit (0.0f, 1.0f, shape01);
    const float attack = 0.42f - 0.36f * s; // fraction of the width spent rising
    const float decayK = 2.0f + 4.5f * s;   // steepness of the exponential tail

    juce::Path p;
    constexpr int steps = 48;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (steps);
        const float e = t < attack ? (attack > 1.0e-4f ? t / attack : 1.0f)
                                   : std::exp (-decayK * (t - attack) / juce::jmax (1.0e-4f, 1.0f - attack));
        const float x = r.getX() + t * r.getWidth();
        const float y = r.getBottom() - e * r.getHeight();
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** The IEC power glyph - a ring broken at the top with a stem through the gap -
    for each module's enable button. Lit when the section is on, the pale grey
    of an unreached tick when off, the same as the Sync buttons. */
void drawPowerIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto r = area.reduced (area.getWidth() * 0.10f, area.getHeight() * 0.10f);
    const auto c = r.getCentre();
    const float radius = r.getWidth() * 0.45f;
    const float stroke = juce::jmax (1.3f, r.getHeight() * 0.12f);

    juce::Path ring;
    ring.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::degreesToRadians (38.0f), juce::degreesToRadians (322.0f),
                        true);

    g.setColour (colour);
    g.strokePath (ring, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (c.x, r.getY(), c.x, c.y + radius * 0.10f, stroke);
}

/** Placeholder module mark - a little grain spray - pending the real per-module
    icons from the design (grain dots, notes, gears, tape reel, reflection). */
void drawGroupMarkPlaceholder (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    static const float pts[][2] = { { 0.30f, 0.28f }, { 0.66f, 0.22f }, { 0.50f, 0.50f },
                                    { 0.24f, 0.66f }, { 0.72f, 0.62f }, { 0.46f, 0.82f } };
    const float d = area.getWidth() * 0.17f;
    g.setColour (colour.withAlpha (0.6f));
    for (const auto& p : pts)
        g.fillEllipse (area.getX() + p[0] * area.getWidth() - d * 0.5f,
                       area.getY() + p[1] * area.getHeight() - d * 0.5f, d, d);
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
    grainOnParam = apvts.getRawParameterValue (kGrainOnID);
    pitchOnParam = apvts.getRawParameterValue (kPitchOnID);
    randomOnParam = apvts.getRawParameterValue (kRandomOnID);
    delayOnParam = apvts.getRawParameterValue (kDelayOnID);
    reverbOnParam = apvts.getRawParameterValue (kReverbOnID);
    volumeParam = apvts.getRawParameterValue (kVolumeID);

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

    // Per-module enables. Default on, so a fresh instance behaves as before.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGrainOnID, 1 }, "Grain On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kPitchOnID, 1 }, "Pitch On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kRandomOnID, 1 }, "Random On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDelayOnID, 1 }, "Delay On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kReverbOnID, 1 }, "Reverb On", true));

    // Master output level, in dB, applied to the whole wet+dry mix last.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kVolumeID, 1 }, "Level", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                                           { return juce::String (v, 1) + " dB"; })));

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

    outputGain.reset (sampleRate, kGainRampSeconds);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));

    const float hp = juce::MathConstants<float>::halfPi;
    const float gMix = grainOnParam->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f) : 0.0f;
    const float dMix = delayOnParam->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, delayMixParam->load() * 0.01f) : 0.0f;
    const float rMix = reverbOnParam->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, reverbMixParam->load() * 0.01f) : 0.0f;
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

    // Each face module has an enable switch. Off leaves the knobs alone but
    // feeds the engine that section's no-op values: Random flat, Pitch pure
    // unison, and (below) the grain / delay / reverb blends fully dry.
    const bool randomOn = randomOnParam->load() > 0.5f;
    const bool pitchOn = pitchOnParam->load() > 0.5f;

    grainer.setSizeMs (sizeMap.value (sizeParam->load(), sizeSynced, bpm));
    grainer.setDensityHz (densityMap.value (densityParam->load(), densitySynced, bpm));
    grainer.setTimeMs (timeParam->load());
    grainer.setFeedback (feedbackParam->load() * 0.01f);
    grainer.setStretch (stretchParam->load() * 0.01f);
    grainer.setFreeze (freezeParam->load() > 0.5f);
    grainer.setShape (shapeParam->load() * 0.01f);
    grainer.setScatter ((randomOn ? scatterParam->load() : 0.0f) * 0.01f);
    grainer.setReverse ((randomOn ? reverseParam->load() : 0.0f) * 0.01f);
    grainer.setStereo ((randomOn ? stereoParam->load() : 0.0f) * 0.01f);
    grainer.setDetuneCents (pitchOn ? detuneParam->load() : 0.0f);
    if (pitchOn)
        grainer.setPitchMix (pitchLowParam->load(), pitchUnisonParam->load(), pitchHighParam->load());
    else
        grainer.setPitchMix (0.0f, 1.0f, 0.0f);

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
    const bool grainOn = grainOnParam->load() > 0.5f;
    const bool delayOn = delayOnParam->load() > 0.5f;
    const bool reverbOn = reverbOnParam->load() > 0.5f;
    const float gMix = grainOn ? juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f) : 0.0f;
    const float dMix = delayOn ? juce::jlimit (0.0f, 1.0f, delayMixParam->load() * 0.01f) : 0.0f;
    const float rMix = reverbOn ? juce::jlimit (0.0f, 1.0f, reverbMixParam->load() * 0.01f) : 0.0f;
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

    // Master output level, applied last over the finished mix.
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float mg = outputGain.getNextValue();
        for (int ch = 0; ch < numOut; ++ch)
            buffer.getWritePointer (ch)[i] *= mg;
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

    // Five modules laid out side by side, each its own raised panel. The knobs
    // are consumed in order by `knobGroups`; within a module they fill two per
    // row (an odd one leading on a row of its own), except Reverb which stacks
    // one per row so its panel stays narrow. Each module's knob caps carry that
    // module's own colour.
    constexpr int kLeadKnob = 86; // the two lead knobs, ~30% up on the face default

    const juce::Colour kGrainCol { 0xff805d93 };  // purple
    const juce::Colour kPitchCol { 0xfff49fbc };  // pink
    const juce::Colour kRandomCol { 0xffffd3ba }; // peach
    const juce::Colour kDelayCol { 0xff9ebd6e };  // green
    const juce::Colour kReverbCol { 0xff169873 }; // teal

    spec.knobs = {
        // Grain
        { .parameterID = kMixID, .caption = "Mix", .capFill = kGrainCol },
        { .parameterID = kSizeID,
          .caption = "Size",
          .capFill = kGrainCol,
          .liveValueText = [this] { return sizeReadout(); } },
        { .parameterID = kDensityID,
          .caption = "Destiny",
          .capFill = kGrainCol,
          .liveValueText = [this] { return densityReadout(); } },
        { .parameterID = kShapeID,
          .caption = "Shape",
          .capFill = kGrainCol,
          .capIcon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
          { drawGrainShapeIcon (g, r, c, shapeParam->load() * 0.01f); } },

        // Pitch
        { .parameterID = kPitchLowID, .caption = "Low", .capFill = kPitchCol },
        { .parameterID = kPitchUnisonID, .caption = "Unison", .capFill = kPitchCol },
        { .parameterID = kPitchHighID, .caption = "High", .capFill = kPitchCol },
        { .parameterID = kDetuneID, .caption = "Detune", .capFill = kPitchCol },

        // Random - Stereo leads (larger), Reverse and Scatter share the row below
        { .parameterID = kStereoID, .caption = "Stereo", .capFill = kRandomCol, .diameter = kLeadKnob },
        { .parameterID = kReverseID, .caption = "Reverse", .capFill = kRandomCol },
        { .parameterID = kScatterID, .caption = "Scatter", .capFill = kRandomCol },

        // Delay - Mix leads (larger), Time and Feedback share the row below
        { .parameterID = kDelayMixID, .caption = "Mix", .capFill = kDelayCol, .diameter = kLeadKnob },
        { .parameterID = kDelayTimeID,
          .caption = "Time",
          .capFill = kDelayCol,
          .liveValueText = [this] { return delayTimeReadout(); } },
        { .parameterID = kDelayFeedbackID, .caption = "Feedback", .capFill = kDelayCol },

        // Reverb
        { .parameterID = kDecayID, .caption = "Decay", .capFill = kReverbCol },
        { .parameterID = kReverbMixID, .caption = "Mix", .capFill = kReverbCol },
    };

    // Lavender-white panels standing off the cool grey face, each with its own
    // mark centred at the top (placeholder art for now).
    const juce::Colour kCardFill { 0xffe9e8f0 };
    spec.knobGroups = {
        { .caption = "Grain", .count = 4, .columns = 2, .fill = kCardFill, .icon = drawGroupMarkPlaceholder },
        { .caption = "Pitch", .count = 4, .columns = 2, .fill = kCardFill, .icon = drawGroupMarkPlaceholder },
        { .caption = "Random", .count = 3, .columns = 2, .fill = kCardFill, .icon = drawGroupMarkPlaceholder },
        { .caption = "Delay", .count = 3, .columns = 2, .fill = kCardFill, .icon = drawGroupMarkPlaceholder },
        { .caption = "Reverb", .count = 2, .columns = 1, .fill = kCardFill, .icon = drawGroupMarkPlaceholder },
    };
    spec.knobGroupsHorizontal = true;
    spec.filledKnobGroups = true;

    // A Sync / ms button under Size, under Destiny, and under the delay Time
    // knob: pressed, the knob picks a note division and the reading is the
    // division label; released, it is the free unit at the host tempo. The
    // toggle is silent - `onClick` only nudges the knob to its remembered
    // position for the mode being entered.
    spec.toggles = {
        { .parameterID = kSizeSyncID,
          .caption = "Sync",
          .afterKnobIndex = 1,
          .centeredBelow = true,
          .belowGap = 10,
          .onClick = [this] { onSizeSyncToggled(); },
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kDensitySyncID,
          .caption = "Sync",
          .afterKnobIndex = 2,
          .centeredBelow = true,
          .belowGap = 10,
          .onClick = [this] { onDensitySyncToggled(); },
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kDelaySyncID,
          .caption = "Sync",
          .afterKnobIndex = 12,
          .centeredBelow = true,
          .belowGap = 10,
          .onClick = [this] { onDelaySyncToggled(); },
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },

        // A power button in the top-right of each module panel: off feeds that
        // section its no-op values (see processBlock) without moving its knobs.
        { .parameterID = kGrainOnID,
          .caption = "On",
          .groupPanelIndex = 0,
          .icon = drawPowerIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kPitchOnID,
          .caption = "On",
          .groupPanelIndex = 1,
          .icon = drawPowerIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kRandomOnID,
          .caption = "On",
          .groupPanelIndex = 2,
          .icon = drawPowerIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kDelayOnID,
          .caption = "On",
          .groupPanelIndex = 3,
          .icon = drawPowerIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = kReverbOnID,
          .caption = "On",
          .groupPanelIndex = 4,
          .icon = drawPowerIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
    };

    // Live / Freeze rides in the strip across the top.
    spec.slideToggle = ee::ui::SlideToggleSpec { .parameterID = kFreezeID, .labelOff = "Live", .labelOn = "Freeze" };

    // Preset bar, centred in the same strip: list / save on the left, name in
    // the middle, prev / next on the right. Backed by the file store.
    spec.presetBar = ee::ui::PresetBarSpec {
        .names = [this] { return presets.names(); },
        .currentIndex = [this] { return presets.currentIndex(); },
        .onSelect = [this] (int i) { presets.select (i); },
        .onSave = [this] { presets.save(); },
        .onSaveAsNew = [this] { presets.saveAsNew(); },
        .onPrev = [this] { presets.step (-1); },
        .onNext = [this] { presets.step (1); },
        .width = 300,
    };

    // The empty line above the logo: a scope showing the grain cloud on the
    // left and its delay repeats fading right, over a faint reverb wash. Still
    // and knob-tracking for now - the parameter IDs are all read normalised.
    spec.grainScope = ee::ui::GrainScopeSpec {
        .sizeID = kSizeID,
        .densityID = kDensityID,
        .scatterID = kScatterID,
        .stereoID = kStereoID,
        .pitchLowID = kPitchLowID,
        .pitchHighID = kPitchHighID,
        .delayTimeID = kDelayTimeID,
        .delayFeedbackID = kDelayFeedbackID,
        .delayMixID = kDelayMixID,
        .reverbDecayID = kDecayID,
        .reverbMixID = kReverbMixID,
        .height = 66,
    };

    // Logo and name share the bottom row, centred and nudged down a touch.
    spec.titleBesideLogo = true;
    spec.titleRowCentred = true;
    spec.titleRowDrop = 4;

    // Master level, hard against the top-right of the switch strip. One text
    // line - the caption at rest, the reading while it is turned.
    spec.topRightKnob = ee::ui::KnobSpec { .parameterID = kVolumeID, .caption = "Level", .captionUntilTouched = true };
    spec.topRightKnobDiameter = 40;

    // Five modules side by side make the face wide rather than tall. Small caps,
    // and a row gap inside each module wide enough for the Sync buttons that
    // hang under Size, Destiny and Time to clear the knobs below them.
    spec.knobDiameter = 66;
    spec.knobRowGap = 54;
    spec.width = 1264;
    spec.height = 620;

    // Peak Wah's white theme, but the face is a cool light-grey box (matching
    // the design) so the lavender-white panels read as raised cards on it.
    auto theme = ee::ui::PedalTheme::white();
    theme.panel = juce::Colour (0xffd5d5df);
    theme.background = juce::Colour (0xffcfcfda);

    auto* editor = new ee::ui::PedalEditor (*this, apvts, spec, theme);

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
