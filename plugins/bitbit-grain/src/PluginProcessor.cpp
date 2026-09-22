#include "PluginProcessor.h"

#include "ee/dsp/GrainerConfig.h"
#include "ee/dsp/RateMap.h"
#include "ee/dsp/TubeDriveConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/LfoBreakpointJson.h"
#include "ee/plugin/ModRoutingJson.h"
#include "ee/plugin/ParamText.h"
#include "ee/plugin/StateVersion.h"
#include "BitBitGrainWebEditor.h"

#include <cmath>

namespace
{
using ee::plugin::percentToText;

constexpr const char* kSizeID = "size";
constexpr const char* kDensityID = "density";
constexpr const char* kSizeSyncID = "ssync";
constexpr const char* kDensitySyncID = "dsync";
constexpr const char* kWindowID = "window";
constexpr const char* kWindowSyncID = "wsync";
constexpr const char* kTimeID = "time";
constexpr const char* kFeedbackID = "feedback";
constexpr const char* kStretchID = "stretch";
constexpr const char* kFreezeID = "freeze";
constexpr const char* kWidthID = "width";
constexpr const char* kShapeID = "shape";
constexpr const char* kShapeFamilyID = "shapefamily";
constexpr const char* kScatterID = "scatter";
constexpr const char* kReverseID = "reverse";
constexpr const char* kStereoID = "stereo";
constexpr const char* kModID = "mod";
constexpr const char* kBitID = "bit";
constexpr const char* kScaleID = "scale";
constexpr const char* kRootID = "root";
constexpr const char* kPitchLowID = "plow";
constexpr const char* kPitchUnisonID = "puni";
constexpr const char* kPitchHighID = "phigh";
constexpr const char* kPitchMixID = "pmix";
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
// The mixer: two independent levels where there used to be one crossfade.
constexpr const char* kDryLevelID = "dry";
constexpr const char* kGrainLevelID = "grains";
constexpr const char* kMixLinkID = "mlink";
constexpr const char* kFilterID = "filter";
constexpr const char* kResoID = "reso";
constexpr const char* kDriveID = "drive";
constexpr const char* kOnID = "on";
constexpr const char* kLevelID = "level";

constexpr const char* kLfoRateID = "lforate";
constexpr const char* kLfoSyncID = "lfosync";
constexpr const char* kLfoOnID = "lfoon";

// The Mod tab's LFO Rate: a plain 0..1 knob, no duration-vs-rate ambiguity
// like Size/Density/Window have, so it uses the shared ee::dsp::RateMap
// (period-only) rather than GrainSyncMap - the same map shape BitBit Trem&Pan's
// own Rate knob uses. A free constant rather than a per-instance member: the
// map carries no state of its own, and createParameterLayout() is static and
// needs to reach it too (for the parameter's own host-facing text).
//
// maxPeriodMs is 8000 (was 4000) so the slowest free-running cycle reaches a
// full 8 seconds - Grain's LFO is meant for slow, evolving sweeps as much as
// audible throb, and 4s topped out too soon for that. skewCentreMs moved from
// 500 to 1000 to match: the knob's physical halfway point is where
// freePeriodMs lands by construction (NormalisableRange::setSkewForCentre),
// so leaving it at 500 while the slow end doubled would have pushed the whole
// 1-8s "slow" half of the range into the top quarter of the knob's travel.
// 1000ms keeps the middle of the knob feeling like the middle of the sweep.
constexpr ee::dsp::RateMap kLfoRateMap { 30.0f, 8000.0f, 1000.0f };

// The Mod tab's own synced choices - deliberately its own small table rather
// than the shared ee::dsp::TempoDivision.h one every other synced control
// here (Wah, Trem&Pan, the Alpine/Modulation tremolo, Delay's own time map)
// still reads unchanged: 1/8 fastest to 4 bars slowest, no triplets or
// dotted values. A hand-drawn breakpoint shape reads differently at an odd
// multiple than a straight delay repeat does, and the shared table's extra
// granularity was mostly unused clutter on this one knob - simplifying it
// here has no reach into anything else that syncs to tempo.
constexpr ee::dsp::TempoDivision kLfoDivisions[] = {
    { "1/8", 0.5f }, { "1/4", 1.0f }, { "1/2", 2.0f }, { "1/1", 4.0f }, { "2", 8.0f }, { "4", 16.0f },
};
constexpr int kNumLfoDivisions = static_cast<int> (sizeof (kLfoDivisions) / sizeof (kLfoDivisions[0]));

int lfoSyncedDivisionIndex (float rate01) noexcept
{
    const int last = kNumLfoDivisions - 1;
    return juce::jlimit (0, last,
                         juce::roundToInt ((1.0f - juce::jlimit (0.0f, 1.0f, rate01)) * static_cast<float> (last)));
}

float lfoSyncedDivisionBeats (float rate01) noexcept
{
    return kLfoDivisions[lfoSyncedDivisionIndex (rate01)].beats;
}

// Mirrors ee::dsp::RateMap::rateToText's own shape, synced branch swapped for
// kLfoDivisions above; the free-running branch still reads kLfoRateMap
// directly (RateMap::freePeriodMs and its ms-formatting are unaffected by
// any of this - only the synced table changed).
juce::String lfoRateToText (float rate01, bool synced)
{
    if (synced)
        return kLfoDivisions[lfoSyncedDivisionIndex (rate01)].label;

    return kLfoRateMap.rateToText (rate01, false);
}

float lfoRateToPeriodSeconds (float rate01, bool synced, double bpm) noexcept
{
    if (synced)
        return lfoSyncedDivisionBeats (rate01) * static_cast<float> (60.0 / juce::jmax (1.0, bpm));

    return kLfoRateMap.rateToPeriodSeconds (rate01, false, bpm);
}

// Per-module enable switches, one per face panel.
constexpr const char* kGrainOnID = "grainon";
constexpr const char* kPitchOnID = "pitchon";
constexpr const char* kScaleOnID = "scaleon";
constexpr const char* kRandomOnID = "randon";
constexpr const char* kDelayOnID = "delon";
constexpr const char* kReverbOnID = "revon";

// State-tree properties: the knob position each Sync switch is not currently
// showing, so flipping the switch and flipping it back lands where it started.
// Only Size/Density/Window have this - the delay's own version went with the
// single dtime knob it used to belong to; ltime/rtime mirror BitBit Delay's
// simpler scheme instead (see BitBitGrainProcessor::parameterChanged).
constexpr const char* kSizeFreeProp = "sizeFree01";
constexpr const char* kSizeSyncProp = "sizeSync01";
constexpr const char* kDensityFreeProp = "densityFree01";
constexpr const char* kDensitySyncProp = "densitySync01";
constexpr const char* kWindowFreeProp = "windowFree01";
constexpr const char* kWindowSyncProp = "windowSync01";

// The Mod tab's breakpoint shape, as JSON (ee/plugin/LfoBreakpointJson.h).
// Unlike the six properties above, this one is written directly onto the
// *live* apvts.state whenever the shape changes (see
// BitBitGrainProcessor::setLfoBreakpointsFromJson), not only into a local copy
// inside getStateInformation - so PresetStore::saveUser's own copyState(),
// which copies that same live tree, picks it up too. See CLAUDE.md's Presets
// section on why the older pattern above only survives a DAW session, never
// a saved preset.
constexpr const char* kLfoBreakpointsProp = "lfoBreakpoints";

// The Mod tab's drag-and-drop routing, as JSON (ee/plugin/ModRoutingJson.h) -
// same live-tree treatment as kLfoBreakpointsProp, for the same reason.
constexpr const char* kLfoRoutingProp = "lfoRouting";

/** What a brand-new instance (or a preset saved before the Mod tab existed)
    opens with - a plain sine, matching lfoShapes.js's own sineBreakpoints()
    exactly so the JS editor and the audio engine agree on what "Sine" means. */
std::vector<ee::dsp::LfoBreakpoint> defaultLfoBreakpoints()
{
    return {
        { 0.0f, 0.0f, -0.5f, false },
        { 0.25f, 1.0f, 0.5f, false },
        { 0.5f, 0.0f, -0.5f, false },
        { 0.75f, -1.0f, 0.5f, false },
    };
}

// The reverb network is normalised to ~0.42 RMS gain; this is BitBit Reverb's
// trim, kept so a given Decay lands at the same level on both pedals.
constexpr float kWetTrim = 1.1f;

// The dry note now stays fully present through the whole Reverb Mix travel
// (added back in full in processBlock - see the note by dryPartL there)
// rather than fading to nothing at Mix 100% like it used to. That was a real
// fix, but it means the same knob position now layers a full-strength note
// on top of the tank where before the tank had the room to itself, which
// read as the reverb having gotten too big / doubled. The gap is exactly
// proportional to (1 - cos(rMix * halfPi)): zero at Mix 0% (nothing there
// changed) and largest at Mix 100% (the dry note used to vanish entirely).
// kReverbWetHeadroom gives some of that room back on the tank's own gain -
// about -3.5 dB of it at Mix 100%, tapering to none at Mix 0%.
constexpr float kReverbWetHeadroom = 0.35f;

float reverbWetGain (float rMix, float halfPi)
{
    const float rd = std::cos (rMix * halfPi);
    return std::sin (rMix * halfPi) * kWetTrim * (1.0f - kReverbWetHeadroom * (1.0f - rd));
}

constexpr float kGainRampSeconds = ee::plugin::kRampSeconds;

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

/** Reverb Low Cut's host-facing text - same shape as BitBit Reverb's own
    hertzToText (plugins/bitbit-reverb/src/PluginProcessor.cpp). */
juce::String hertzToText (float value, int)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (value)) + " Hz";
}

/** The mixer faders' law. One power curve rather than a piecewise fit, chosen
    so that all three points a mixer actually needs land exactly: silence at
    0 %, unity at cfg::kLevelUnityPct, and +cfg::kLevelBoostDb at the top of the
    travel. The exponent falls out of the last two - (100/unity)^k = the boost
    as a gain - so moving either constant keeps all three true. */
const float kLevelExponent = std::log (juce::Decibels::decibelsToGain (ee::dsp::config::kLevelBoostDb)) /
                             std::log (100.0f / ee::dsp::config::kLevelUnityPct);

float levelGainFor (float pct)
{
    return std::pow (juce::jmax (0.0f, pct) / ee::dsp::config::kLevelUnityPct, kLevelExponent);
}

/** Those faders read in decibels: the percentage is only where the thumb sits,
    and what a mixer wants to know is how far off unity it is. */
juce::String levelToText (float pct, int)
{
    const float gain = levelGainFor (pct);

    if (gain <= 0.0001f)
        return "-inf dB";

    const float db = juce::Decibels::gainToDecibels (gain);

    return (db > 0.05f ? juce::String ("+") : juce::String()) + juce::String (db, 1) + " dB";
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
    return { r, false, ee::dsp::kGrainDensityDivisions, ee::dsp::kNumGrainDensityDivisions };
}

/** Same shape as makeSizeMap(): a duration, so the free range is built in
    milliseconds even though GrainerConfig.h's WINDOW constants are seconds
    (matching Grainer::setAttackReachSeconds' own unit) - value() below
    converts back. */
ee::dsp::GrainSyncMap makeWindowMap()
{
    namespace cfg = ee::dsp::config;
    juce::NormalisableRange<float> r (cfg::kMinWindowSeconds * 1000.0f, cfg::kMaxWindowSeconds * 1000.0f);
    r.setSkewForCentre (cfg::kWindowSkewSeconds * 1000.0f);
    return { r, true };
}

ee::dsp::GrainSyncMap makeDelayMap()
{
    namespace cfg = ee::dsp::config;
    juce::NormalisableRange<float> r (cfg::kMinTimeMs, cfg::kMaxTimeMs);
    r.setSkewForCentre (cfg::kTimeSkewMs);
    return { r, true };
}
} // namespace

BitBitGrainProcessor::BitBitGrainProcessor()
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
    windowMap = makeWindowMap();
    delayMap = makeDelayMap();

    sizeParam = apvts.getRawParameterValue (kSizeID);
    densityParam = apvts.getRawParameterValue (kDensityID);
    sizeSyncParam = apvts.getRawParameterValue (kSizeSyncID);
    densitySyncParam = apvts.getRawParameterValue (kDensitySyncID);
    windowParam = apvts.getRawParameterValue (kWindowID);
    windowSyncParam = apvts.getRawParameterValue (kWindowSyncID);
    timeParam = apvts.getRawParameterValue (kTimeID);
    feedbackParam = apvts.getRawParameterValue (kFeedbackID);
    stretchParam = apvts.getRawParameterValue (kStretchID);
    freezeParam = apvts.getRawParameterValue (kFreezeID);
    widthParam = apvts.getRawParameterValue (kWidthID);
    shapeParam = apvts.getRawParameterValue (kShapeID);
    shapeFamilyParam = apvts.getRawParameterValue (kShapeFamilyID);
    scatterParam = apvts.getRawParameterValue (kScatterID);
    reverseParam = apvts.getRawParameterValue (kReverseID);
    stereoParam = apvts.getRawParameterValue (kStereoID);
    modParam = apvts.getRawParameterValue (kModID);
    bitParam = apvts.getRawParameterValue (kBitID);
    scaleParam = apvts.getRawParameterValue (kScaleID);
    rootParam = apvts.getRawParameterValue (kRootID);
    pitchLowParam = apvts.getRawParameterValue (kPitchLowID);
    pitchUnisonParam = apvts.getRawParameterValue (kPitchUnisonID);
    pitchHighParam = apvts.getRawParameterValue (kPitchHighID);
    pitchMixParam = apvts.getRawParameterValue (kPitchMixID);
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
    dryLevelParam = apvts.getRawParameterValue (kDryLevelID);
    grainLevelParam = apvts.getRawParameterValue (kGrainLevelID);
    mixLinkParam = apvts.getRawParameterValue (kMixLinkID);
    filterParam = apvts.getRawParameterValue (kFilterID);
    resoParam = apvts.getRawParameterValue (kResoID);
    driveParam = apvts.getRawParameterValue (kDriveID);
    onParam = apvts.getRawParameterValue (kOnID);
    lfoRateParam = apvts.getRawParameterValue (kLfoRateID);
    lfoSyncParam = apvts.getRawParameterValue (kLfoSyncID);
    lfoOnParam = apvts.getRawParameterValue (kLfoOnID);
    scaleOnParam = apvts.getRawParameterValue (kScaleOnID);
    delayOnParam = apvts.getRawParameterValue (kDelayOnID);
    reverbOnParam = apvts.getRawParameterValue (kReverbOnID);
    levelParam = apvts.getRawParameterValue (kLevelID);

    // Seed both mode slots from the parameters' defaults, so the first flip of a
    // Sync switch has somewhere sensible to land before the user has set it.
    sizeFree01 = sizeParam->load();
    sizeSync01 = sizeParam->load();
    densityFree01 = densityParam->load();
    densitySync01 = densityParam->load();
    windowFree01 = windowParam->load();
    windowSync01 = windowParam->load();

    apvts.addParameterListener (kDelayLeftTimeID, this);
    apvts.addParameterListener (kDelayRightTimeID, this);
    apvts.addParameterListener (kDelayLinkID, this);
    apvts.addParameterListener (kDryLevelID, this);
    apvts.addParameterListener (kGrainLevelID, this);
    apvts.addParameterListener (kMixLinkID, this);
    apvts.addParameterListener (kSizeSyncID, this);
    apvts.addParameterListener (kDensitySyncID, this);
    apvts.addParameterListener (kWindowSyncID, this);

    refreshLfoStateFromApvts();

#if EE_GRAIN_TRACE
    trace = std::make_unique<GrainTrace> (apvts);
#endif
}

BitBitGrainProcessor::~BitBitGrainProcessor()
{
    apvts.removeParameterListener (kDelayLeftTimeID, this);
    apvts.removeParameterListener (kDelayRightTimeID, this);
    apvts.removeParameterListener (kDelayLinkID, this);
    apvts.removeParameterListener (kDryLevelID, this);
    apvts.removeParameterListener (kGrainLevelID, this);
    apvts.removeParameterListener (kMixLinkID, this);
    apvts.removeParameterListener (kSizeSyncID, this);
    apvts.removeParameterListener (kDensitySyncID, this);
    apvts.removeParameterListener (kWindowSyncID, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout BitBitGrainProcessor::createParameterLayout()
{
    namespace cfg = ee::dsp::config;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto percentAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    const auto unit = juce::NormalisableRange<float> (0.0f, 1.0f);

    // Size, Density and Window are one normalised knob each; the shared Sync
    // switch beside them decides whether that maps to a free unit or a note
    // division for all three at once (see GrainSyncPill in GrainFace.jsx).
    // The host-facing text assumes the free reading - the editor overrides it
    // with one that follows the switch and the host tempo.
    //
    // Meta, all six: flipping ssync/dsync/wsync moves size/density/window
    // (parameterChanged -> onSizeSyncToggled/onDensitySyncToggled/
    // onWindowSyncToggled), the same "one parameter's change moves another's
    // value" shape as the delay time mirror below - and the same fix,
    // following BitBit Alpine's own note on why (PluginProcessor.cpp there):
    // auval's round-trip check fails on exactly this unless every parameter
    // doing it, moved or mover, is flagged.
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

    // How long a struck note keeps drawing the cloud back to its own attack
    // rather than moving on to whatever Time/Scatter currently offer - see
    // ee::dsp::Grainer::setAttackReachSeconds and GrainerConfig.h's WINDOW
    // section. Short is one clean pass; long is a cloud that keeps re-singing
    // the same attack for several seconds.
    //
    // Not on the face any more: its slot is the Feedback knob now (a cloud that
    // keeps going after the input stops is exactly what Feedback is for). The
    // parameter stays registered - its id is in the golden file and saved
    // sessions carry it - but nothing reads it: the reach is fixed at
    // GrainerConfig.h's kFixedAttackReachSeconds.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kWindowID, 1 }, "Window", unit, cfg::kDefaultWindow01,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return makeWindowMap().toText (v, false, 120.0); })
            .withMeta (true)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSizeSyncID, 1 }, "Size Sync",
                                                            cfg::kDefaultSizeSync,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDensitySyncID, 1 }, "Density Sync",
                                                            cfg::kDefaultDensitySync,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kWindowSyncID, 1 }, "Window Sync",
                                                            cfg::kDefaultWindowSync,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));

    // The granular delay half. Feedback is a face knob (a dB taper - see
    // GrainerConfig.h's FEEDBACK section); Time and Stretch are not, and run at
    // these fixed defaults.
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

    // Wide, on Grain's first row: how far off centre a grain lands with
    // Random's Spray closed - see GrainerConfig.h's WIDE. (Was a Mono/Stereo
    // switch; the id is unchanged.)
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kWidthID, 1 }, "Wide", percent,
                                                             cfg::kDefaultWidePct, percentAttributes));

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
    // they're drawn from (TapeDelay's Mod wow, BitBit Artifact Amp's Bit).
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kModID, 1 }, "Mod", percent,
                                                             cfg::kDefaultModPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kBitID, 1 }, "Bit", percent,
                                                             cfg::kDefaultBitPct, percentAttributes));

    // What the Low/High pitch groups' interval choices are quantized to -
    // see GrainerConfig.h's SCALE section. Order matches ee::dsp::config::
    // kScales exactly; Major/C first so a session saved before these existed
    // loads at index 0 either way.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kScaleID, 1 }, "Scale",
        juce::StringArray { "Major", "Minor", "Pentatonic Major", "Pentatonic Minor", "Chromatic" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kRootID, 1 }, "Root",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    // The three pitch groups are weights against each other, not a position on
    // one scale, so each gets its own knob and they are free to overlap.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchLowID, 1 }, "Pitch Low", percent,
                                                             cfg::kDefaultPitchLowPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchUnisonID, 1 }, "Pitch Unison",
                                                             percent, cfg::kDefaultPitchUnisonPct, percentAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchHighID, 1 }, "Pitch High",
                                                             percent, cfg::kDefaultPitchHighPct, percentAttributes));

    // How much of the scale reaches the sound: this crossfades the High group
    // (the only one Root and Scale colour) back towards unison and leaves
    // Low's octaves alone - see processBlock's own blend. Defaults to 100 so a
    // session saved before this parameter existed still hears the full spread
    // it always did.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPitchMixID, 1 }, "Pitch Mix", percent,
                                                             cfg::kDefaultPitchMixPct, percentAttributes));

    // The post delay: a clean digital delay after the grain stage, before the
    // reverb. Independent Left/Right time knobs, a Sync-L/R link (mirrors BitBit
    // Delay's own `sync`/ltime/rtime exactly - see BitBitGrainProcessor::
    // parameterChanged), a Normal/Wide/Ping-Pong routing choice, plus Feedback
    // and Mix. dtsync is Grain's own long-standing sense (true = tempo-synced
    // display), used by both time knobs' host-facing text and live readout.
    //
    // Meta, all three (ltime/rtime/dlink): with the link on, turning one time
    // knob moves the other, and turning the link on itself adopts the left
    // value into the right - the same shape and the same fix as BitBit Alpine's
    // dly.ltime/rtime/sync (PluginProcessor.cpp there).
    const auto timeAttributes =
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return makeDelayMap().toText (v, true, 120.0); })
            .withMeta (true);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDelayLeftTimeID, 1 }, "Left Time",
                                                             unit, cfg::kDefaultDelayTime01, timeAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDelayRightTimeID, 1 }, "Right Time",
                                                             unit, cfg::kDefaultDelayTime01, timeAttributes));

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

    // Decay is straight seconds onto the network; Low Cut is a real knob now
    // (used to be fixed at GrainerTuning::verbLowCutHz); Mix is its own
    // dry/wet. Reverb always hears the grain cloud alone now - see
    // processBlock's own note on where that tap sits (there used to be a
    // "Reverb Source" choice, Global vs Grains; Global is gone, and with only
    // one choice left the parameter went with it).
    auto decayRange = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDecayID, 1 }, "Decay", decayRange, cfg::kDefaultReverbDecaySeconds,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (decaySecondsToText)));

    auto loCutRange =
        juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinLowCutHz, ee::dsp::FdnReverb::kMaxLowCutHz);
    loCutRange.setSkewForCentre (180.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kReverbLoCutID, 1 }, "Reverb Low Cut", loCutRange, cfg::kDefaultReverbLoCutHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hertzToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kReverbMixID, 1 }, "Reverb Mix",
                                                             percent, cfg::kDefaultReverbMixPct, percentAttributes));

    // Two independent levels rather than one crossfade knob: a crossfade
    // cannot give you full dry and a full cloud at once, which is the whole
    // point of a mixer. Meta, all three: with the link on, moving one fader
    // moves the other, and auval fails a parameter that moves another without
    // the flag - the same rule ltime/rtime/dlink live by above.
    const auto levelAttributes =
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (levelToText).withMeta (true);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDryLevelID, 1 }, "Dry", percent,
                                                             cfg::kDefaultDryLevelPct, levelAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kGrainLevelID, 1 }, "Grains", percent,
                                                             cfg::kDefaultGrainLevelPct, levelAttributes));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kMixLinkID, 1 }, "Mixer Link", false,
                                                            juce::AudioParameterBoolAttributes().withMeta (true)));

    // The cloud's lowpass cutoff - see GrainerConfig.h's CLOUD FILTER section.
    // Unipolar and resting wide open, so a fresh instance sounds exactly as it
    // did when the corner was fixed; winding it down darkens the cloud. The
    // highpass that used to share this knob is hidden now and never moves.
    // Printed in Hz rather than as a percentage: the knob is a cutoff, and the
    // scope under it on the face already carries the shape.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kFilterID, 1 }, "Filter", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int)
            {
                namespace cfg = ee::dsp::config;
                const float open = juce::jlimit (0.0f, 1.0f, v * 0.01f);
                const float hz =
                    cfg::kCloudLowpassHz * std::pow (cfg::kCloudLowpassMinHz / cfg::kCloudLowpassHz, 1.0f - open);

                if (hz >= 1000.0f)
                    return juce::String (hz / 1000.0f, 1) + " kHz";

                return juce::String (juce::roundToInt (hz)) + " Hz";
            })));

    // How much the Filter knob's cutoff runs through a resonant 4-pole
    // ladder instead of the plain one-pole above - see Grainer::
    // setCloudResonance and LadderFilter.h. Rests at 0: kept off the face's
    // main sweep on purpose, a fresh instance or an old preset with no reso
    // saved yet sounds identical to before this knob existed. Plain percent,
    // not a modulation target (see resoParam's own note in PluginProcessor.h).
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kResoID, 1 }, "Reso", percent, 0.0f,
                                                             percentAttributes));

    // Amp's own single-knob tube drive (see ee/dsp/TubeDrive.h), on the grain
    // cloud alone - same "grains only" reach as Filter just above, not the dry
    // path. Same fitted engine and default as BitBit Artifact's amp.drive.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDriveID, 1 }, "Drive", percent,
                                                             ee::dsp::tubedrive::kDefaultDrivePct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    // The Mod tab's LFO. The breakpoint shape itself is not a parameter (see
    // kLfoBreakpointsProp) - it is not host-automatable, the same way a
    // preset's saved wave shape on any synth is data, not a knob.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kLfoRateID, 1 }, "Mod Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                                           { return lfoRateToText (v, true); })));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kLfoSyncID, 1 }, "Mod Sync", false));
    // The Mod tab's own master switch - off silences every assignment at once
    // (modulatedValue() below) without having to remove them one by one, the
    // same "off leaves everything else alone" contract every other module's
    // own on/off carries. Default on so an instance with assignments already
    // routed keeps sounding as it did before this switch existed.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kLfoOnID, 1 }, "Mod On", true));

    // Grain On / Random On kept registered for old presets and host
    // automation lanes that still reference grainon/randon - nothing reads
    // them any more (see PluginProcessor.h's own note by scaleOnParam).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kGrainOnID, 1 }, "Grain On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kPitchOnID, 1 }, "Pitch On", true));

    // The Scale block's own switch. Off is exactly Scale Mix at 0 - the face
    // offers both because they are the same thing said two ways, one a
    // gesture and one a blend.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kScaleOnID, 1 }, "Scale On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kRandomOnID, 1 }, "Random On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kDelayOnID, 1 }, "Delay On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kReverbOnID, 1 }, "Reverb On", true));

    // Master output level, in dB, applied to the whole wet+dry mix last.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kLevelID, 1 }, "Level", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                                           { return juce::String (v, 1) + " dB"; })));

    // Which window the Shape knob morphs - ee::dsp::Grainer::ShapeFamily, in
    // the same order. Appended last and last for good, per CLAUDE.md's rule
    // for an engine choice: this index is what every saved session and preset
    // keys on. Triangle is index 0 and the default, so an old session with no
    // opinion about this parameter loads exactly as it always has.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kShapeFamilyID, 1 }, "Shape Family",
        juce::StringArray { "Triangle", "Gaussian", "Sinc", "Spike" }, 0));

    return layout;
}

double BitBitGrainProcessor::readPlayHeadBpm()
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

juce::String BitBitGrainProcessor::sizeReadout() const
{
    // Free-running, not tempo-synced (see processBlock's sizeSynced) - a
    // grain's length is a duration a listener hears, not a rhythmic
    // position, and reading it off a tempo division made the knob jump
    // between the division's ms lengths as it turned instead of sweeping
    // smoothly across kMinGrainMs..kMaxGrainMs. bpm plays no part any more;
    // currentBpm() is not called here.
    namespace cfg = ee::dsp::config;
    const float ms = juce::jlimit (cfg::kMinGrainMs, cfg::kMaxGrainMs, sizeMap.value (sizeParam->load(), false, 0.0));
    return ms >= 1000.0f ? juce::String (ms * 0.001f, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms";
}

juce::String BitBitGrainProcessor::densityReadout() const
{
    return densityMap.toText (densityParam->load(), true, currentBpm());
}

juce::String BitBitGrainProcessor::windowReadout() const
{
    // Always seconds, synced or not - same reasoning as sizeReadout(): a
    // listener hears how long the cloud keeps ringing, not a rhythmic
    // position, and synced mode can pick a division longer than
    // kMaxWindowSeconds at a slow enough tempo while
    // Grainer::setAttackReachSeconds silently clamps to it regardless, so the
    // readout clamps to the same range before formatting rather than showing
    // a division length nothing is sounding.
    namespace cfg = ee::dsp::config;
    const float ms = juce::jlimit (cfg::kMinWindowSeconds * 1000.0f, cfg::kMaxWindowSeconds * 1000.0f,
                                   windowMap.value (windowParam->load(), true, currentBpm()));
    return ms >= 1000.0f ? juce::String (ms * 0.001f, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms";
}

juce::String BitBitGrainProcessor::leftTimeReadout() const
{
    return delayMap.toText (leftTimeParam->load(), delaySyncParam->load() > 0.5f, currentBpm());
}

juce::String BitBitGrainProcessor::rightTimeReadout() const
{
    return delayMap.toText (rightTimeParam->load(), delaySyncParam->load() > 0.5f, currentBpm());
}

juce::String BitBitGrainProcessor::lfoRateReadout() const
{
    return lfoRateToText (lfoRateParam->load(), lfoSyncParam->load() > 0.5f);
}

juce::String BitBitGrainProcessor::lfoBreakpointsAsJson() const
{
    return apvts.state.getProperty (kLfoBreakpointsProp, "").toString();
}

void BitBitGrainProcessor::setLfoBreakpointsFromJson (const juce::String& json)
{
    auto points = ee::plugin::lfoBreakpointsFromJson (json);
    if (points.empty())
        return;

    currentLfoBreakpoints = points;
    modLfo.setBreakpoints (points);

    // The live tree, not a local copy - see kLfoBreakpointsProp's own note.
    apvts.state.setProperty (kLfoBreakpointsProp, json, nullptr);
}

juce::String BitBitGrainProcessor::lfoRoutingAsJson() const
{
    return apvts.state.getProperty (kLfoRoutingProp, "[]").toString();
}

void BitBitGrainProcessor::setLfoRoutingFromJson (const juce::String& json)
{
    modRouter.setAssignments (ee::plugin::modRoutingFromJson (json));

    // The live tree, not a local copy - see kLfoBreakpointsProp's own note.
    apvts.state.setProperty (kLfoRoutingProp, json, nullptr);
}

float BitBitGrainProcessor::modulatedValue (const char* paramID, float rawValue) const noexcept
{
    if (! modRouter.hasAssignments() || lfoOnParam->load() <= 0.5f)
        return rawValue;

    const float depth = modRouter.depthFor (paramID);
    if (depth == 0.0f)
        return rawValue;

    auto* param = apvts.getParameter (paramID);
    if (param == nullptr)
        return rawValue;

    const float base01 = param->convertTo0to1 (rawValue);
    const float mod01 = juce::jlimit (0.0f, 1.0f, base01 + depth * modLfo.currentValue());
    return param->convertFrom0to1 (mod01);
}

void BitBitGrainProcessor::refreshLfoStateFromApvts()
{
    auto json = apvts.state.getProperty (kLfoBreakpointsProp, "").toString();
    auto points = ee::plugin::lfoBreakpointsFromJson (json);

    if (points.empty())
    {
        points = defaultLfoBreakpoints();
        json = ee::plugin::lfoBreakpointsToJson (points);
        apvts.state.setProperty (kLfoBreakpointsProp, json, nullptr);
    }

    currentLfoBreakpoints = points;
    modLfo.setBreakpoints (points);

    modRouter.setAssignments (
        ee::plugin::modRoutingFromJson (apvts.state.getProperty (kLfoRoutingProp, "[]").toString()));

    lfoGeneration.fetch_add (1, std::memory_order_relaxed);
}

void BitBitGrainProcessor::syncToggled (const char* paramID,
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

void BitBitGrainProcessor::onSizeSyncToggled()
{
    syncToggled (kSizeID, sizeFree01, sizeSync01, sizeSyncParam);
}

void BitBitGrainProcessor::onDensitySyncToggled()
{
    syncToggled (kDensityID, densityFree01, densitySync01, densitySyncParam);
}

void BitBitGrainProcessor::onWindowSyncToggled()
{
    syncToggled (kWindowID, windowFree01, windowSync01, windowSyncParam);
}

ee::dsp::TapeDelay::Routing BitBitGrainProcessor::routing() const noexcept
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

void BitBitGrainProcessor::mirrorTime (const juce::String& from, const juce::String& to)
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
    BitBitDelayProcessor::installState: an install writes the link flag and both
    times one parameter at a time, in the file's own order, so without this a
    preset whose two sides are deliberately apart could be collapsed onto one
    time before the link's own "off" ever arrived. */
void BitBitGrainProcessor::installState (const juce::ValueTree& tree)
{
    installingState = true;
    apvts.replaceState (tree);
    installingState = false;

    refreshLfoStateFromApvts();
}

void BitBitGrainProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (installingState.load())
        return;

    // ssync/dsync/wsync: the face's single Grain "SYNC" pill writes all three
    // flags separately (see GrainFace.jsx); each write lands here on its own
    // and remaps its own knob, so this also does the right thing if a host
    // automates just one of the three.
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
    if (parameterID == kWindowSyncID)
    {
        onWindowSyncToggled();
        return;
    }

    // dry/grains/mlink: the mixer's own link, the same shape as the delay's
    // below. Handled first and returned from, so the two links never see each
    // other's parameters.
    if (parameterID == kDryLevelID || parameterID == kGrainLevelID || parameterID == kMixLinkID)
    {
        const bool mixerLinked =
            parameterID == kMixLinkID ? newValue > 0.5f : (mixLinkParam != nullptr && mixLinkParam->load() > 0.5f);

        if (! mixerLinked)
            return;

        if (mirroring.exchange (true))
            return;

        // Turning the link on adopts Dry, which is the fader the user is most
        // likely to have set deliberately before reaching for the button.
        if (parameterID == kGrainLevelID)
            mirrorTime (kGrainLevelID, kDryLevelID);
        else
            mirrorTime (kDryLevelID, kGrainLevelID);

        mirroring = false;
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

void BitBitGrainProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    grainer.prepare (sampleRate);
    grainer.reset();

    driveStage.prepare (sampleRate);
    driveStage.reset();
    sendDrive.prepare (sampleRate);
    sendDrive.reset();

    delay.prepare (sampleRate);
    delay.reset();
    delay.setModulation (0.0f);
    delay.setRouting (routing());
    snapDelayNextBlock = true;

    modLfo.prepare (sampleRate);
    modLfo.reset();

    reverb.prepare (sampleRate);
    reverb.reset();

    // Set here as well as per block: a host asks for getTailLengthSeconds()
    // before it ever calls processBlock, and without this it is answered from
    // FdnReverb's own default decay rather than the knob.
    reverb.setDecayTime (decayParam->load());
    reverb.setLowCut (reverbLoCutParam->load());

    // Fixed for the life of the plugin: BitBit Grain runs the network plain. Read
    // back from the engine's own tuning, so a value the dev panel has changed
    // survives the host re-preparing us.
    const auto& tuning = grainer.getTuning();
    reverb.setResonance (tuning.verbResonance);
    reverb.setShimmer (ee::dsp::config::kVerbShimmer);

    grainBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
    dryBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
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

    outputLimiter.prepare (sampleRate);
    outputLimiter.setCeilingDb (ee::dsp::config::kLimiterCeilingDb);
    outputLimiter.setAttackMs (ee::dsp::config::kLimiterAttackMs);
    outputLimiter.setReleaseMs (ee::dsp::config::kLimiterReleaseMs);

    const float hp = juce::MathConstants<float>::halfPi;
    const float dryLevel = juce::jlimit (0.0f, 1.0f, dryLevelParam->load() * 0.01f);
    const float grainLevel = juce::jlimit (0.0f, 1.0f, grainLevelParam->load() * 0.01f);
    const float dMix = delayOnParam->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, delayMixParam->load() * 0.01f) : 0.0f;
    const float rMix = reverbOnParam->load() > 0.5f ? juce::jlimit (0.0f, 1.0f, reverbMixParam->load() * 0.01f) : 0.0f;
    const bool engaged = onParam->load() > 0.5f;

    grainDry.setCurrentAndTargetValue (engaged ? dryLevel : 1.0f);
    grainWet.setCurrentAndTargetValue (grainLevel);
    delayDry.setCurrentAndTargetValue (engaged ? std::cos (dMix * hp) : 1.0f);
    delayWet.setCurrentAndTargetValue (std::sin (dMix * hp));
    reverbDry.setCurrentAndTargetValue (engaged ? std::cos (rMix * hp) : 1.0f);
    reverbWet.setCurrentAndTargetValue (reverbWetGain (rMix, hp));
    engageGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);

    readPlayHeadBpm(); // seed lastKnownBpm from whatever the host reports before the first block, if anything
}

void BitBitGrainProcessor::releaseResources()
{
    grainer.reset();
    driveStage.reset();
    sendDrive.reset();
    delay.reset();
    reverb.reset();
    outputLimiter.reset();
}

double BitBitGrainProcessor::getTailLengthSeconds() const
{
    return static_cast<double> (grainer.getTailSeconds() + delay.getTailSeconds() + reverb.getTailSeconds());
}

bool BitBitGrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void BitBitGrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    // Grain's own Size/Density carry no Sync switch on the face any more, so
    // these are hardcoded rather than read off ssync/dsync. Density stays
    // tempo-locked (the grain spawn grid). Size is free-running: turning the
    // knob is a duration a listener hears, and snapping it to a tempo
    // division made it jump between the division's ms lengths instead of
    // sweeping smoothly - see sizeReadout()'s matching change. ssync/dsync
    // (and wsync) are kept registered (see PluginProcessor.h's note by
    // sizeSyncParam) purely so a preset or automation lane that still
    // references them resolves to something; nothing here reads them.
    const bool sizeSynced = false;
    const bool densitySynced = true;
    const bool delaySynced = delaySyncParam->load() > 0.5f;

    // Same three transport facts BitBitTremPanProcessor reads for its own synced
    // LFO: a finite ppq and whether the transport is actually rolling, so the
    // grain spawn timer can be phase-locked to the beat rather than merely
    // rate-matched to it (see ee::dsp::Grainer::Transport).
    bool havePpq = false;
    bool isPlaying = false;
    double ppqStart = 0.0;
    // Grid's bars. A host that does not report the bar start or the
    // time signature gets 4/4 with bars counted from ppq 0.
    double barStartPpq = 0.0;
    double quartersPerBar = 4.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto ppq = position->getPpqPosition())
            {
                ppqStart = *ppq;
                havePpq = std::isfinite (ppqStart);
            }
            if (const auto barStart = position->getPpqPositionOfLastBarStart())
                if (std::isfinite (*barStart))
                    barStartPpq = *barStart;
            if (const auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    quartersPerBar = 4.0 * signature->numerator / signature->denominator;
            isPlaying = position->getIsPlaying();
        }

    ee::dsp::Grainer::Transport grainTransport;
    grainTransport.synced = densitySynced && havePpq && isPlaying;
    grainTransport.playing = isPlaying;
    grainTransport.cyclesPerQuarter =
        1.0 / juce::jmax (1.0e-4, static_cast<double> (densityMap.divisionBeats (densityParam->load())));
    grainTransport.ppqPerSample = bpm / (60.0 * getSampleRate());
    // Grid is not a switch: always on. The live tap needs nothing but a tempo
    // (a host that reports none gets 120), so it holds whether or not the
    // transport is rolling; the frozen bar-locked capture needs a moving ppq
    // and the engine only takes it while `synced` - see GrainerConfig.h's GRID.
    grainTransport.grid = true;
    grainTransport.barStartPpq = barStartPpq;
    grainTransport.quartersPerBar = quartersPerBar;

    // The Mod tab's LFO - same three transport facts, reusing
    // ee::dsp::Tremolo::Transport rather than a parallel struct since this
    // engine's phase-align code is copied from Tremolo's own. Not routed to
    // anything yet (Stage 3); this only ticks the phase forward so the live
    // playhead marker and, later, modulation both read a moving value.
    const bool lfoSynced = lfoSyncParam->load() > 0.5f;
    const float lfoRate01 = lfoRateParam->load();
    modLfo.setPeriodSeconds (lfoRateToPeriodSeconds (lfoRate01, lfoSynced, bpm));

    ee::dsp::Tremolo::Transport lfoTransport;
    lfoTransport.synced = lfoSynced && havePpq && isPlaying;
    lfoTransport.playing = isPlaying;
    lfoTransport.ppqStart = ppqStart;
    lfoTransport.cyclesPerQuarter = 1.0 / juce::jmax (1.0e-4, static_cast<double> (lfoSyncedDivisionBeats (lfoRate01)));
    lfoTransport.ppqPerSample = bpm / (60.0 * getSampleRate());

    // Advanced per chunk, inside the loop below, alongside the modulatable
    // Grain/Pitch/Random setters - a single advance(numSamples, ...) here
    // would leave modLfo.currentValue() fixed for the whole block, stepping
    // once per host callback instead of moving within it.

    // Grain/Pitch/Random carry no enable switch on the face (see the note by
    // scaleOnParam in PluginProcessor.h) - grainon/pitchon/randon are dead
    // parameters, so every knob in those three sections is read unconditionally
    // below. A preset saved with one at 0 (from before those switches were
    // pulled off the face) used to silently and permanently mute the whole
    // section, which is why Pitch's Low/Unison/High could look turned up and
    // do nothing.

    // Size/Density/Window/Shape/Feedback/Scatter/Reverse/Stereo/Mod/Pitch Low/Unison/
    // High/Pitch Mix/Filter/Drive/Bit - every modulation target this pedal
    // has (Delay/Reverb's own scope cut) - are set per chunk inside the loop
    // below instead of here, each read through modulatedValue() so a
    // drag-and-drop LFO assignment actually moves within the block.
    // Everything else in this section has no modulation target and stays a
    // once-per-block read, unchanged.
    grainer.setTimeMs (timeParam->load());
    grainer.setStretch (stretchParam->load() * 0.01f);
    // Harmless to set even when Pitch is off: setPitchMix(0,1,0) inside the
    // loop below means the Low/High groups these feed are never picked either
    // way.
    grainer.setScale (juce::roundToInt (scaleParam->load()), juce::roundToInt (rootParam->load()));
    grainer.setFreeze (freezeParam->load() > 0.5f);
    grainer.setWidth (widthParam->load() * 0.01f);

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
    // No upper clamp: these two go past unity now (see levelGainFor), and
    // jlimit-ing them to 1 would quietly throw the whole boost half away. The
    // safety limiter at the end of the chain is what catches an overcooked mix.
    const float dryLevel = levelGainFor (dryLevelParam->load());
    const float grainLevel = levelGainFor (grainLevelParam->load());
    const float dMix = delayOn ? juce::jlimit (0.0f, 1.0f, delayMixParam->load() * 0.01f) : 0.0f;
    const float rMix = reverbOn ? juce::jlimit (0.0f, 1.0f, reverbMixParam->load() * 0.01f) : 0.0f;
    const bool engaged = onParam->load() > 0.5f;

    // Trails: bypassing opens every stage's dry leg to unity and closes its send
    // to zero, so the grain cloud, the delay repeats and the reverb tail all
    // ring out over the untouched input instead of being chopped off.
    grainDry.setTargetValue (engaged ? dryLevel : 1.0f);
    grainWet.setTargetValue (grainLevel);
    delayDry.setTargetValue (engaged ? std::cos (dMix * hp) : 1.0f);
    delayWet.setTargetValue (std::sin (dMix * hp));
    reverbDry.setTargetValue (engaged ? std::cos (rMix * hp) : 1.0f);
    reverbWet.setTargetValue (reverbWetGain (rMix, hp));
    engageGain.setTargetValue (engaged ? 1.0f : 0.0f);

    // Everything below writes into scratch buffers that prepareToPlay sizes. If
    // it has not run - or ran for a smaller block than the host is now handing
    // us - pass the audio through untouched rather than writing past the end.
    const int scratch =
        juce::jmin (juce::jmin (juce::jmin (grainBuffer.getNumSamples(), dryBuffer.getNumSamples()),
                                juce::jmin (stageBuffer.getNumSamples(), engageBuffer.getNumSamples())),
                    juce::jmin (juce::jmin (delayInBuffer.getNumSamples(), delayWetBuffer.getNumSamples()),
                                juce::jmin (monoBuffer.getNumSamples(), verbBuffer.getNumSamples())));

    if (scratch <= 0 || grainBuffer.getNumChannels() < kMaxChannels || dryBuffer.getNumChannels() < kMaxChannels ||
        stageBuffer.getNumChannels() < kMaxChannels || delayInBuffer.getNumChannels() < kMaxChannels ||
        delayWetBuffer.getNumChannels() < kMaxChannels || verbBuffer.getNumChannels() < kMaxChannels)
        return;

    // A modulation assignment needs re-evaluating more often than once per
    // host callback to read as continuous rather than stepped - kModChunk
    // only shrinks the loop below when something is actually assigned, so an
    // unmodulated instance keeps today's one-pass-per-block behaviour exactly.
    constexpr int kModChunk = 64;
    const int step = modRouter.hasAssignments() ? juce::jmin (juce::jmin (maxBlock, scratch), kModChunk)
                                                : juce::jmin (maxBlock, scratch);

    float wetPeak = 0.0f; // the cloud as mixed in, for the face's cosmos panel and its meter
    float dryPeak = 0.0f; // the dry path at the same point, for the face's other meter

    for (int offset = 0; offset < numSamples; offset += step)
    {
        const int chunk = juce::jmin (step, numSamples - offset);

        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : nullptr;

        float* grainL = grainBuffer.getWritePointer (0);
        float* grainR = grainBuffer.getWritePointer (1);
        float* egBuf = engageBuffer.getWritePointer (0);
        float* mono = monoBuffer.getWritePointer (0); // the reverb's send, however it is arrived at

        // With the interval send weighting dialled in the tank is fed its own
        // bus out of the engine - the same cloud, weighted per grain by how
        // far it was transposed - instead of a fold-down of the finished one.
        // At 0 the engine writes no bus at all and the old fold below stands,
        // so the routing only exists while it is doing something.
        const bool intervalSend = grainer.hasIntervalSend();

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

        lfoTransport.ppqStart = ppqStart + static_cast<double> (offset) * lfoTransport.ppqPerSample;
        modLfo.advance (chunk, lfoTransport);

        // Every modulation target this pedal has (Delay/Reverb's own scope
        // cut) - re-read here, once per chunk, through modulatedValue() so an
        // assignment moves within the block rather than only between host
        // callbacks.
        grainer.setSizeMs (sizeMap.value (modulatedValue (kSizeID, sizeParam->load()), sizeSynced, bpm));
        grainer.setDensityHz (densityMap.value (modulatedValue (kDensityID, densityParam->load()), densitySynced, bpm));
        // Fixed, not the Window parameter: the knob left the face (Feedback
        // took its slot) and 0.2 s - the shortest reach there was - is what
        // keeps the cloud on its regular Time tap rather than re-singing each
        // onset. Whatever a session stored in Window is ignored.
        grainer.setAttackReachSeconds (ee::dsp::config::kFixedAttackReachSeconds);

        // Shape and Smooth are one control - see GrainerConfig.h's SMOOTH
        // section for why the swell only occupies the bottom of the travel.
        const float shape01 = modulatedValue (kShapeID, shapeParam->load()) * 0.01f;
        grainer.setShape (shape01);
        grainer.setSmooth (ee::dsp::config::smoothForShape (shape01));
        grainer.setShapeFamily (juce::roundToInt (shapeFamilyParam->load()));

        // The Feedback knob (it took Window's place on the face). With Pitch
        // Low/High in the mix a repeat is re-pitched on its way round, so an
        // octave-up grain comes back an octave higher again - the same as any
        // pitch-shifting feedback loop, and why the taper is kept gentle.
        grainer.setFeedback (ee::dsp::config::feedbackGainFor (modulatedValue (kFeedbackID, feedbackParam->load())));
        grainer.setScatter (modulatedValue (kScatterID, scatterParam->load()) * 0.01f);
        grainer.setReverse (modulatedValue (kReverseID, reverseParam->load()) * 0.01f);
        grainer.setStereo (modulatedValue (kStereoID, stereoParam->load()) * 0.01f);
        grainer.setMod (modulatedValue (kModID, modParam->load()) * 0.01f);
        grainer.setBit (modulatedValue (kBitID, bitParam->load()) * 0.01f);
        grainer.setCloudFilter (modulatedValue (kFilterID, filterParam->load()) * 0.01f);
        // Not a modulation target (kept small, see PluginProcessor.h's own
        // note on resoParam) - read straight off the knob, the same as Dry/
        // Grains above.
        grainer.setCloudResonance (resoParam->load() * 0.01f);
        driveStage.setDrive01 (modulatedValue (kDriveID, driveParam->load()) * 0.01f);
        sendDrive.setDrive01 (modulatedValue (kDriveID, driveParam->load()) * 0.01f);

        // Pitch Mix belongs to the scale alone, and it colours the High group's
        // interval rather than its weight: closed, every up-grain is a plain
        // octave; open, they are drawn from the Scale/Root table. The Scale
        // switch is the same statement as Mix at 0, so it is applied as one -
        // off simply closes the blend and leaves octaves. Low is plain octaves
        // either way: an octave is consonant against anything and has nothing
        // to do with the key, so there is nothing for a scale control to pull
        // it back from.
        grainer.setScaleBlend (scaleOnParam->load() > 0.5f ? modulatedValue (kPitchMixID, pitchMixParam->load()) * 0.01f
                                                           : 0.0f);
        grainer.setPitchMix (modulatedValue (kPitchLowID, pitchLowParam->load()),
                             modulatedValue (kPitchUnisonID, pitchUnisonParam->load()),
                             modulatedValue (kPitchHighID, pitchHighParam->load()));

        grainTransport.ppqStart = ppqStart + static_cast<double> (offset) * grainTransport.ppqPerSample;
        grainer.process (grainL, grainR, grainL, grainR, intervalSend ? mono : nullptr, chunk, grainTransport);

        // Grain-cloud-only, like the cloud Filter above it - the dry path
        // never reaches this stage.
        driveStage.process (grainL, grainR, chunk);

        // The dry note's own level and the grain send are computed here and
        // kept apart from here on: dryBuffer is added back in full only at
        // the very end (the output stage below), never crossfaded against
        // Delay or Reverb's own Mix, so turning either all the way up still
        // leaves the dry note audible - it silences the plain grain cloud in
        // favour of its own treatment, not the note that triggered it.
        float* dryPartL = dryBuffer.getWritePointer (0);
        float* dryPartR = dryBuffer.getWritePointer (1);
        float* sendL = delayInBuffer.getWritePointer (0);
        float* sendR = delayInBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            const float gd = grainDry.getNextValue();
            const float gw = grainWet.getNextValue();
            const float eg = egBuf[i];
            const float dryR = inR != nullptr ? inR[i] : inL[i];

            // The grain cloud at the level it is actually mixed in at - what
            // both Delay and Reverb hear now, and only what they hear: neither
            // effect ever touches the dry signal, so a struck note itself is
            // never echoed or reverbed, only its cloud is. Finite-guarded here,
            // before anything downstream (the delay line, the reverb tank)
            // gets a chance to latch a poisoned sample into its own feedback.
            float wetL = grainL[i] * gw;
            float wetR = grainR[i] * gw;

            if (! std::isfinite (wetL))
                wetL = 0.0f;
            if (! std::isfinite (wetR))
                wetR = 0.0f;

            float dL = inL[i] * gd;
            float dR = dryR * gd;

            if (! std::isfinite (dL))
                dL = 0.0f;
            if (! std::isfinite (dR))
                dR = 0.0f;

            dryPartL[i] = dL;
            dryPartR[i] = dR;
            sendL[i] = wetL * eg; // grain-only, gated: TapeDelay reads before it writes
            sendR[i] = wetR * eg;

            if (intervalSend)
            {
                // The send bus at the same level and behind the same gate the
                // cloud it was weighted from is mixed in at.
                float s = mono[i] * gw * eg;
                mono[i] = std::isfinite (s) ? s : 0.0f;
            }

            // In place, so Delay's own crossfade and Reverb's own tap below
            // both read the same already-scaled, already-guarded wet signal
            // Delay's send just used. Nothing else reads grainL/R past this
            // point, and the next chunk refills them from the input.
            grainL[i] = wetL;
            grainR[i] = wetR;
            wetPeak = juce::jmax (wetPeak, std::abs (wetL), std::abs (wetR));
            dryPeak = juce::jmax (dryPeak, std::abs (dL), std::abs (dR));
        }

        // Delay: the gated grain-only send into the delay line, blended
        // equal-power back against the ungated grain send - so Mix at 0 is
        // the plain (undelayed) cloud and turning it up crossfades in repeats
        // built from that same cloud. The dry note takes no part in this; it
        // rejoins at the very end regardless of where Mix sits.
        float* delL = delayWetBuffer.getWritePointer (0);
        float* delR = delayWetBuffer.getWritePointer (1);
        delay.process (sendL, sendR, delL, delR, chunk);

        float* postL = stageBuffer.getWritePointer (0); // reuse: post-delay grain chain
        float* postR = stageBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            const float dd = delayDry.getNextValue();
            const float dw = delayWet.getNextValue();
            const float eg = egBuf[i];

            postL[i] = grainL[i] * dd + delL[i] * dw;
            postR[i] = grainR[i] * dd + delR[i] * dw;

            // Reverb's own send: the cloud straight from Grainer::process (via
            // grainL/R above), never the dry signal and never Delay's repeats
            // either - gated by eg, same as Delay's own send, so bypass stops
            // feeding the tank rather than cutting it off mid-ring.
            if (! intervalSend)
                mono[i] = 0.5f * (grainL[i] + grainR[i]) * eg;
        }

        // Drive on the weighted bus too, so the tank hears the cloud coloured
        // as it is downstream.
        if (intervalSend)
            sendDrive.process (mono, nullptr, chunk);

        float* verbL = verbBuffer.getWritePointer (0);
        float* verbR = verbBuffer.getWritePointer (1);
        reverb.process (mono, verbL, verbR, chunk);

        float* outL = buffer.getWritePointer (0, offset);
        float* outR = numOut > 1 ? buffer.getWritePointer (1, offset) : nullptr;

        for (int i = 0; i < chunk; ++i)
        {
            const float rd = reverbDry.getNextValue();
            const float rw = reverbWet.getNextValue();

            // Reverb's own Mix, crossfading the (already Delay-processed)
            // grain chain against the reverb tank - the dry note, again,
            // takes no part; it is added back in full here, the one place
            // it rejoins the signal, so it is exactly as present at Mix 100%
            // as it is at Mix 0.
            float l = dryPartL[i] + (postL[i] * rd + verbL[i] * rw);
            float r = dryPartR[i] + (postR[i] * rd + verbR[i] * rw);

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

    // Safety net, not a voicing stage: a grain landing back on the transient
    // that spawned it can sum past what Level alone anticipated. See
    // GrainerConfig.h's OUTPUT LIMITER section.
    {
        float* outL = buffer.getWritePointer (0);
        float* outR = numOut > 1 ? buffer.getWritePointer (1) : outL;
        outputLimiter.process (outL, outR, numSamples);
    }

    {
        // Fast up, slow down: the reader polls at ~30 Hz, so a decay of about
        // 0.12 s keeps a short hit visible to it while still reading as silence
        // within a second of the cloud stopping. Both are taken where the two
        // faders have just set them and before anything downstream, so each
        // meter reads its own path rather than the finished mix - a loud dry
        // note must not light a cloud that is silent.
        const float decay =
            std::exp (-static_cast<float> (numSamples) / (0.12f * static_cast<float> (getSampleRate())));
        visLevel.store (juce::jmax (wetPeak, visLevel.load (std::memory_order_relaxed) * decay),
                        std::memory_order_relaxed);
        visDryLevel.store (juce::jmax (dryPeak, visDryLevel.load (std::memory_order_relaxed) * decay),
                           std::memory_order_relaxed);
    }

#if EE_GRAIN_TRACE
    float outputPeak = 0.0f;
    for (int ch = 0; ch < numOut; ++ch)
        outputPeak = juce::jmax (outputPeak, buffer.getMagnitude (ch, 0, numSamples));

    trace->observe (inputPeak, outputPeak);
#endif
}

juce::AudioProcessorEditor* BitBitGrainProcessor::createEditor()
{
    return new BitBitGrainWebEditor (*this);
}

void BitBitGrainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = ee::plugin::copyVersionedState (apvts);

    state.setProperty (kSizeFreeProp, sizeFree01.load(), nullptr);
    state.setProperty (kSizeSyncProp, sizeSync01.load(), nullptr);
    state.setProperty (kDensityFreeProp, densityFree01.load(), nullptr);
    state.setProperty (kDensitySyncProp, densitySync01.load(), nullptr);
    state.setProperty (kWindowFreeProp, windowFree01.load(), nullptr);
    state.setProperty (kWindowSyncProp, windowSync01.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BitBitGrainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = ee::plugin::xmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            installState (ee::plugin::sanitisedState (juce::ValueTree::fromXml (*xml), apvts));

            const auto restore = [this] (const char* prop, std::atomic<float>& slot, const std::atomic<float>* fallback)
            { slot.store (static_cast<float> (apvts.state.getProperty (prop, fallback->load()))); };

            restore (kSizeFreeProp, sizeFree01, sizeParam);
            restore (kSizeSyncProp, sizeSync01, sizeParam);
            restore (kDensityFreeProp, densityFree01, densityParam);
            restore (kDensitySyncProp, densitySync01, densityParam);
            restore (kWindowFreeProp, windowFree01, windowParam);
            restore (kWindowSyncProp, windowSync01, windowParam);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitBitGrainProcessor();
}
