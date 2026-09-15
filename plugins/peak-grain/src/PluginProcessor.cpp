#include "PluginProcessor.h"

#include "ee/dsp/GrainerConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "PeakGrainWebEditor.h"

#include <cmath>

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
constexpr const char* kModID = "mod";
constexpr const char* kBitID = "bit";
constexpr const char* kDetuneID = "detune";
constexpr const char* kPitchLowID = "plow";
constexpr const char* kPitchUnisonID = "puni";
constexpr const char* kPitchHighID = "phigh";
constexpr const char* kDelayLeftTimeID = "ltime";
constexpr const char* kDelayRightTimeID = "rtime";
constexpr const char* kDelayLinkID = "dlink";
constexpr const char* kDelaySyncID = "dtsync";
constexpr const char* kDelayTypeID = "dtype";
constexpr const char* kDelayFeedbackID = "dfb";
constexpr const char* kDelayMixID = "dmix";
constexpr const char* kDecayID = "decay";
constexpr const char* kReverbLoCutID = "rlocut";
constexpr const char* kReverbMixID = "rmix";
constexpr const char* kMixID = "mix";
constexpr const char* kOnID = "on";
constexpr const char* kLevelID = "level";

// Per-module enable switches, one per face panel.
constexpr const char* kGrainOnID = "grainon";
constexpr const char* kPitchOnID = "pitchon";
constexpr const char* kRandomOnID = "randon";
constexpr const char* kDelayOnID = "delon";
constexpr const char* kReverbOnID = "revon";

// State-tree properties: the knob position each Sync switch is not currently
// showing, so flipping the switch and flipping it back lands where it started.
// Only Size/Density have this - the delay's own version went with the single
// dtime knob it used to belong to; ltime/rtime mirror Peak Delay's simpler
// scheme instead (see PeakGrainProcessor::parameterChanged).
constexpr const char* kSizeFreeProp = "sizeFree01";
constexpr const char* kSizeSyncProp = "sizeSync01";
constexpr const char* kDensityFreeProp = "densityFree01";
constexpr const char* kDensitySyncProp = "densitySync01";

// The reverb network is normalised to ~0.42 RMS gain; this is Peak Reverb's
// trim, kept so a given Decay lands at the same level on both pedals.
constexpr float kWetTrim = 1.1f;

constexpr float kGainRampSeconds = ee::plugin::kRampSeconds;

/** Detune's host-facing text. Signed, like Stretch's signedPercentToText
    below - Detune is bipolar (-7..+7 semitones), so which side of zero a
    reading is on is as much the point as the figure itself. */
juce::String semitonesToText (float value, int)
{
    return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " st";
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

/** Reverb Low Cut's host-facing text - same shape as Peak Reverb's own
    hertzToText (plugins/peak-reverb/src/PluginProcessor.cpp). */
juce::String hertzToText (float value, int)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (value)) + " Hz";
}

/** The three tempo-sync maps, built once from GrainerConfig ranges. Size and
    the delay time knobs are durations (knob up = longer, free unit ms);
    Density is a rate (knob up = faster, free unit grains/second). */
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
} // namespace

PeakGrainProcessor::PeakGrainProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Every route a whole APVTS tree can arrive by - a host restoring a
    // session, the preset store loading a preset - has to stand down the
    // Left/Right mirror for the length of it; see installState.
    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };

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
    modParam = apvts.getRawParameterValue (kModID);
    bitParam = apvts.getRawParameterValue (kBitID);
    detuneParam = apvts.getRawParameterValue (kDetuneID);
    pitchLowParam = apvts.getRawParameterValue (kPitchLowID);
    pitchUnisonParam = apvts.getRawParameterValue (kPitchUnisonID);
    pitchHighParam = apvts.getRawParameterValue (kPitchHighID);
    leftTimeParam = apvts.getRawParameterValue (kDelayLeftTimeID);
    rightTimeParam = apvts.getRawParameterValue (kDelayRightTimeID);
    delayLinkParam = apvts.getRawParameterValue (kDelayLinkID);
    delaySyncParam = apvts.getRawParameterValue (kDelaySyncID);
    delayTypeParam = apvts.getRawParameterValue (kDelayTypeID);
    delayFeedbackParam = apvts.getRawParameterValue (kDelayFeedbackID);
    delayMixParam = apvts.getRawParameterValue (kDelayMixID);
    decayParam = apvts.getRawParameterValue (kDecayID);
    reverbLoCutParam = apvts.getRawParameterValue (kReverbLoCutID);
    reverbMixParam = apvts.getRawParameterValue (kReverbMixID);
    mixParam = apvts.getRawParameterValue (kMixID);
    onParam = apvts.getRawParameterValue (kOnID);
    grainOnParam = apvts.getRawParameterValue (kGrainOnID);
    pitchOnParam = apvts.getRawParameterValue (kPitchOnID);
    randomOnParam = apvts.getRawParameterValue (kRandomOnID);
    delayOnParam = apvts.getRawParameterValue (kDelayOnID);
    reverbOnParam = apvts.getRawParameterValue (kReverbOnID);
    levelParam = apvts.getRawParameterValue (kLevelID);

    // Seed both mode slots from the parameters' defaults, so the first flip of a
    // Sync switch has somewhere sensible to land before the user has set it.
    sizeFree01 = sizeParam->load();
    sizeSync01 = sizeParam->load();
    densityFree01 = densityParam->load();
    densitySync01 = densityParam->load();

    apvts.addParameterListener (kDelayLeftTimeID, this);
    apvts.addParameterListener (kDelayRightTimeID, this);
    apvts.addParameterListener (kDelayLinkID, this);
    apvts.addParameterListener (kSizeSyncID, this);
    apvts.addParameterListener (kDensitySyncID, this);

#if EE_GRAIN_TRACE
    trace = std::make_unique<GrainTrace> (apvts);
#endif
}

PeakGrainProcessor::~PeakGrainProcessor()
{
    apvts.removeParameterListener (kDelayLeftTimeID, this);
    apvts.removeParameterListener (kDelayRightTimeID, this);
    apvts.removeParameterListener (kDelayLinkID, this);
    apvts.removeParameterListener (kSizeSyncID, this);
    apvts.removeParameterListener (kDensitySyncID, this);
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
    //
    // Meta, all four: flipping ssync/dsync moves size/density
    // (parameterChanged -> onSizeSyncToggled/onDensitySyncToggled), the same
    // "one parameter's change moves another's value" shape as the delay time
    // mirror below - and the same fix, following Peak Alpine's own note on
    // why (PluginProcessor.cpp there): auval's round-trip check fails on
    // exactly this unless every parameter doing it, moved or mover, is
    // flagged.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kSizeID, 1 }, "Size", unit, cfg::kDefaultSize01,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return makeSizeMap().toText (v, false, 120.0); })
            .withMeta (true)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDensityID, 1 }, "Density", unit, cfg::kDefaultDensity01,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return makeDensityMap().toText (v, false, 120.0); })
            .withMeta (true)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSizeSyncID, 1 }, "Size Sync",
                                                            cfg::kDefaultSizeSync,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDensitySyncID, 1 }, "Density Sync",
                                                            cfg::kDefaultDensitySync,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));

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

    // Drift and crush, both per-grain and both fully off at rest - see
    // Grainer::setMod/setBit and GrainerConfig.h's own notes on the engines
    // they're drawn from (TapeDelay's Mod wow, Peak Artifact Amp's Bit).
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kModID, 1 }, "Mod", percent,
                                                             cfg::kDefaultModPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kBitID, 1 }, "Bit", percent,
                                                             cfg::kDefaultBitPct, percentAttributes));

    // Linear, not skewed - see GrainerConfig.h's own note on why.
    auto detuneRange = juce::NormalisableRange<float> (cfg::kMinDetuneSemitones, cfg::kMaxDetuneSemitones);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDetuneID, 1 }, "Detune", detuneRange, cfg::kDefaultDetuneSemitones,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (semitonesToText)));

    // The three pitch groups are weights against each other, not a position on
    // one scale, so each gets its own knob and they are free to overlap.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchLowID, 1 }, "Pitch Low", percent,
                                                             cfg::kDefaultPitchLowPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchUnisonID, 1 }, "Pitch Unison",
                                                             percent, cfg::kDefaultPitchUnisonPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchHighID, 1 }, "Pitch High",
                                                             percent, cfg::kDefaultPitchHighPct, percentAttributes));

    // The post delay: a clean digital delay after the grain stage, before the
    // reverb. Independent Left/Right time knobs, a Sync-L/R link (mirrors Peak
    // Delay's own `sync`/ltime/rtime exactly - see PeakGrainProcessor::
    // parameterChanged), a Normal/Wide/Ping-Pong routing choice, plus Feedback
    // and Mix. dtsync is Grain's own long-standing sense (true = tempo-synced
    // display), used by both time knobs' host-facing text and live readout.
    //
    // Meta, all three (ltime/rtime/dlink): with the link on, turning one time
    // knob moves the other, and turning the link on itself adopts the left
    // value into the right - the same shape and the same fix as Peak Alpine's
    // dly.ltime/rtime/sync (PluginProcessor.cpp there).
    const auto timeAttributes = juce::AudioParameterFloatAttributes()
                                     .withStringFromValueFunction (
                                         [] (float v, int) { return makeDelayMap().toText (v, true, 120.0); })
                                     .withMeta (true);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDelayLeftTimeID, 1 }, "Left Time", unit, cfg::kDefaultDelayTime01, timeAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDelayRightTimeID, 1 }, "Right Time", unit, cfg::kDefaultDelayTime01, timeAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDelayLinkID, 1 }, "Delay Link", true,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDelaySyncID, 1 }, "Delay Sync",
                                                            cfg::kDefaultDelaySync));

    // Normal first, so a session saved before this parameter existed loads at
    // index 0 and sounds exactly as it did.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kDelayTypeID, 1 }, "Delay Type",
                                                              juce::StringArray { "Normal", "Wide", "Ping Pong" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDelayFeedbackID, 1 },
                                                             "Delay Feedback", percent, cfg::kDefaultDelayFeedbackPct,
                                                             percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDelayMixID, 1 }, "Delay Mix", percent,
                                                             cfg::kDefaultDelayMixPct, percentAttributes));

    // The reverb now hears the whole post-delay blend. Decay is straight
    // seconds onto the network; Low Cut is a real knob now (used to be fixed
    // at GrainerTuning::verbLowCutHz); Mix is its own dry/wet.
    auto decayRange = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDecayID, 1 }, "Decay", decayRange, cfg::kDefaultReverbDecaySeconds,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (decaySecondsToText)));

    auto loCutRange = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinLowCutHz, ee::dsp::FdnReverb::kMaxLowCutHz);
    loCutRange.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kReverbLoCutID, 1 }, "Reverb Low Cut", loCutRange, cfg::kDefaultReverbLoCutHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hertzToText)));

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
        juce::ParameterID { kLevelID, 1 }, "Level", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                                           { return juce::String (v, 1) + " dB"; })));

    return layout;
}

double PeakGrainProcessor::readPlayHeadBpm()
{
    double bpm = 120.0;

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpmValue = position->getBpm())
                bpm = *hostBpmValue;

    bpm = juce::jlimit (20.0, 300.0, bpm);
    lastKnownBpm.store (bpm);
    return bpm;
}

juce::String PeakGrainProcessor::sizeReadout() const
{
    // Always milliseconds, synced or not - a grain's length is a duration a
    // listener hears, not a rhythmic position, so the note-division label
    // toText() would show once synced is not what belongs here.
    //
    // Clamped to what Grainer::setSizeMs() will actually apply
    // (config::kMinGrainMs..kMaxGrainMs): synced mode picks a tempo division
    // with no relation to that range, so at a slow enough tempo the top of
    // the knob's travel can select a division several seconds long while the
    // engine silently caps every grain at kMaxGrainMs (500 ms) regardless -
    // showing the true division length there was a readout that described a
    // sound nothing was making. sizeMap.toMsText()'s own formatting (>=1s
    // shows seconds) is duplicated here rather than reused, since clamping
    // has to happen on the raw ms value before that decision.
    namespace cfg = ee::dsp::config;
    const float ms = juce::jlimit (cfg::kMinGrainMs, cfg::kMaxGrainMs,
                                   sizeMap.value (sizeParam->load(), sizeSyncParam->load() > 0.5f, currentBpm()));
    return ms >= 1000.0f ? juce::String (ms * 0.001f, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms";
}

juce::String PeakGrainProcessor::densityReadout() const
{
    return densityMap.toText (densityParam->load(), densitySyncParam->load() > 0.5f, currentBpm());
}

juce::String PeakGrainProcessor::leftTimeReadout() const
{
    return delayMap.toText (leftTimeParam->load(), delaySyncParam->load() > 0.5f, currentBpm());
}

juce::String PeakGrainProcessor::rightTimeReadout() const
{
    return delayMap.toText (rightTimeParam->load(), delaySyncParam->load() > 0.5f, currentBpm());
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

ee::dsp::TapeDelay::Routing PeakGrainProcessor::routing() const noexcept
{
    using Routing = ee::dsp::TapeDelay::Routing;

    switch (delayTypeParam != nullptr ? juce::roundToInt (delayTypeParam->load()) : 0)
    {
    case 1:
        return Routing::wide;
    case 2:
        return Routing::pingPong;
    default:
        return Routing::normal;
    }
}

void PeakGrainProcessor::mirrorTime (const juce::String& from, const juce::String& to)
{
    auto* source = apvts.getParameter (from);
    auto* destination = apvts.getParameter (to);

    if (source == nullptr || destination == nullptr)
        return;

    const float value = source->getValue();

    if (std::abs (destination->getValue() - value) > 1.0e-6f)
        destination->setValueNotifyingHost (value);
}

/** Installs a whole APVTS tree - a host restoring a session, a preset load -
    with the Sync L/R mirror held off for the length of it. Same reasoning as
    PeakDelayProcessor::installState: an install writes the link flag and both
    times one parameter at a time, in the file's own order, so without this a
    preset whose two sides are deliberately apart could be collapsed onto one
    time before the link's own "off" ever arrived. */
void PeakGrainProcessor::installState (const juce::ValueTree& tree)
{
    installingState = true;
    apvts.replaceState (tree);
    installingState = false;
}

void PeakGrainProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (installingState.load())
        return;

    // ssync/dsync: the face's single Grain "SYNC" pill writes both flags
    // separately (see GrainFace.jsx); each write lands here on its own and
    // remaps its own knob, so this also does the right thing if a host
    // automates just one of the two.
    if (parameterID == kSizeSyncID)
    {
        onSizeSyncToggled();
        return;
    }
    if (parameterID == kDensitySyncID)
    {
        onDensitySyncToggled();
        return;
    }

    // ltime/rtime/dlink: the Sync-L/R mirror. Read the link flag from the
    // callback argument rather than the cached value: the two are not
    // guaranteed to be in step at this point.
    const bool linked =
        parameterID == kDelayLinkID ? newValue > 0.5f : (delayLinkParam != nullptr && delayLinkParam->load() > 0.5f);

    if (! linked)
        return;

    if (mirroring.exchange (true))
        return;

    // Turning the link on adopts the left value, which is the one the user set
    // last in the common case of reaching for the button after dialling left.
    if (parameterID == kDelayRightTimeID)
        mirrorTime (kDelayRightTimeID, kDelayLeftTimeID);
    else
        mirrorTime (kDelayLeftTimeID, kDelayRightTimeID);

    mirroring = false;
}

void PeakGrainProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    grainer.prepare (sampleRate);
    grainer.reset();

    delay.prepare (sampleRate);
    delay.reset();
    delay.setModulation (0.0f);
    delay.setRouting (routing());
    snapDelayNextBlock = true;

    reverb.prepare (sampleRate);
    reverb.reset();

    // Set here as well as per block: a host asks for getTailLengthSeconds()
    // before it ever calls processBlock, and without this it is answered from
    // FdnReverb's own default decay rather than the knob.
    reverb.setDecayTime (decayParam->load());
    reverb.setLowCut (reverbLoCutParam->load());

    // Fixed for the life of the plugin: Peak Grain runs the network plain. Read
    // back from the engine's own tuning, so a value the dev panel has changed
    // survives the host re-preparing us.
    const auto& tuning = grainer.getTuning();
    reverb.setResonance (tuning.verbResonance);
    reverb.setShimmer (ee::dsp::config::kVerbShimmer);

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
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));

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

    readPlayHeadBpm(); // seed lastKnownBpm from whatever the host reports before the first block, if anything
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

    const double bpm = readPlayHeadBpm();
    const bool sizeSynced = sizeSyncParam->load() > 0.5f;
    const bool densitySynced = densitySyncParam->load() > 0.5f;
    const bool delaySynced = delaySyncParam->load() > 0.5f;

    // Same three transport facts PeakTremPanProcessor reads for its own synced
    // LFO: a finite ppq and whether the transport is actually rolling, so the
    // grain spawn timer can be phase-locked to the beat rather than merely
    // rate-matched to it (see ee::dsp::Grainer::Transport).
    bool havePpq = false;
    bool isPlaying = false;
    double ppqStart = 0.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto ppq = position->getPpqPosition())
            {
                ppqStart = *ppq;
                havePpq = std::isfinite (ppqStart);
            }
            isPlaying = position->getIsPlaying();
        }

    ee::dsp::Grainer::Transport grainTransport;
    grainTransport.synced = densitySynced && havePpq && isPlaying;
    grainTransport.playing = isPlaying;
    grainTransport.cyclesPerQuarter =
        1.0 / juce::jmax (1.0e-4, static_cast<double> (densityMap.divisionBeats (densityParam->load())));
    grainTransport.ppqPerSample = bpm / (60.0 * getSampleRate());

    // Each face module has an enable switch. Off leaves the knobs alone but
    // feeds the engine that section's no-op values: Grain's own Bit crush
    // off, Random flat, Pitch pure unison, and (below) the grain / delay /
    // reverb blends fully dry. grainOn is fetched here rather than down by
    // gMix - Bit lives on the Grain section's face now, so it follows that
    // section's own enable the way Detune follows pitchOn.
    const bool grainOn = grainOnParam->load() > 0.5f;
    const bool randomOn = randomOnParam->load() > 0.5f;
    const bool pitchOn = pitchOnParam->load() > 0.5f;

    grainer.setSizeMs (sizeMap.value (sizeParam->load(), sizeSynced, bpm));
    grainer.setDensityHz (densityMap.value (densityParam->load(), densitySynced, bpm));
    grainer.setTimeMs (timeParam->load());
    grainer.setFeedback (feedbackParam->load() * 0.01f);
    grainer.setStretch (stretchParam->load() * 0.01f);
    grainer.setFreeze (freezeParam->load() > 0.5f);
    grainer.setShape (shapeParam->load() * 0.01f);
    grainer.setBit ((grainOn ? bitParam->load() : 0.0f) * 0.01f);
    grainer.setScatter ((randomOn ? scatterParam->load() : 0.0f) * 0.01f);
    grainer.setReverse ((randomOn ? reverseParam->load() : 0.0f) * 0.01f);
    grainer.setStereo ((randomOn ? stereoParam->load() : 0.0f) * 0.01f);
    grainer.setMod ((randomOn ? modParam->load() : 0.0f) * 0.01f);
    grainer.setDetuneSemitones (pitchOn ? detuneParam->load() : 0.0f);
    if (pitchOn)
        grainer.setPitchMix (pitchLowParam->load(), pitchUnisonParam->load(), pitchHighParam->load());
    else
        grainer.setPitchMix (0.0f, 1.0f, 0.0f);

    const float leftSecs = delayMap.value (leftTimeParam->load(), delaySynced, bpm) * 0.001f;
    const float rightSecs = delayMap.value (rightTimeParam->load(), delaySynced, bpm) * 0.001f;
    delay.setDelaySeconds (leftSecs, rightSecs);
    delay.setRouting (routing());
    delay.setFeedback (delayFeedbackParam->load() * 0.01f);
    delay.setModulation (0.0f);

    if (snapDelayNextBlock)
    {
        delay.snapDelays();
        snapDelayNextBlock = false;
    }

    reverb.setDecayTime (decayParam->load());
    reverb.setLowCut (reverbLoCutParam->load());

    const float hp = juce::MathConstants<float>::halfPi;
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

        grainTransport.ppqStart = ppqStart + static_cast<double> (offset) * grainTransport.ppqPerSample;
        grainer.process (grainL, grainR, grainL, grainR, chunk, grainTransport);

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
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));
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
    return new PeakGrainWebEditor (*this);
}

void PeakGrainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    state.setProperty (kSizeFreeProp, sizeFree01.load(), nullptr);
    state.setProperty (kSizeSyncProp, sizeSync01.load(), nullptr);
    state.setProperty (kDensityFreeProp, densityFree01.load(), nullptr);
    state.setProperty (kDensitySyncProp, densitySync01.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakGrainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            installState (juce::ValueTree::fromXml (*xml));

            const auto restore = [this] (const char* prop, std::atomic<float>& slot, const std::atomic<float>* fallback)
            { slot.store (static_cast<float> (apvts.state.getProperty (prop, fallback->load()))); };

            restore (kSizeFreeProp, sizeFree01, sizeParam);
            restore (kSizeSyncProp, sizeSync01, sizeParam);
            restore (kDensityFreeProp, densityFree01, densityParam);
            restore (kDensitySyncProp, densitySync01, densityParam);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakGrainProcessor();
}
