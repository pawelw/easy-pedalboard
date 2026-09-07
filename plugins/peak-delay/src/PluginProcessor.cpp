#include "PluginProcessor.h"

#include "PeakDelayWebEditor.h"
#include "TimeMap.h"
#include "ee/plugin/ParamText.h"

namespace
{
using ee::plugin::percentToText;

constexpr const char* kLeftTimeID = "ltime";
constexpr const char* kRightTimeID = "rtime";
constexpr const char* kSyncID = "sync";
constexpr const char* kTimeUnitID = "timeunit"; // false = note division text, true = ms

// How the two delay lines are wired up - see ee::dsp::TapeDelay::Routing. A
// choice rather than two booleans: the three are mutually exclusive positions
// of one switch, and a host should see them by name.
constexpr const char* kTypeID = "dtype";
constexpr const char* kFeedbackID = "fb";
constexpr const char* kMixID = "mix";

// The two footer sections. "mod" and "tape" keep the ids they shipped with,
// and Drift keeps "mod"'s original meaning too - it is the delay line's own
// modulation, the same stage that parameter always drove. Tape's "tape" is now
// the Wear of a Wear+Flutter pair, so a saved session's value still lands on
// the control it was set for.
constexpr const char* kDriftID = "mod";
constexpr const char* kPhaserID = "phaser";
constexpr const char* kWearID = "tape";
constexpr const char* kFlutterID = "flutter";

// Where the Tape section sits in the chain. Only Tape has one - see
// PluginProcessor.h's tapeIsPost().
constexpr const char* kTapePreID = "tapepre";

// The Filter section: the two cuts either end of the band, on the repeats.
constexpr const char* kLoCutID = "locut";
constexpr const char* kHiCutID = "hicut";

// The header's two faders, either end of the whole pedal.
constexpr const char* kInGainID = "ingain";
constexpr const char* kOutGainID = "outgain";

constexpr const char* kOnID = "on";

constexpr int kDefaultDivision = 5; // 1/8
constexpr float kDefaultTime01 = ee::peakdelay::time01ForDivision (kDefaultDivision);

constexpr float kGainRampSeconds = 0.02f;

// How long the Tape router takes to move the section from one side of the
// delay to the other. Slow on purpose: what actually travels is Wear and
// Flutter, so this is a hand turning one knob down while another comes up, and
// a quarter of a second is about how fast a hand does that. Anything much
// shorter starts to read as a step in the drive rather than as a move.
constexpr double kPlacementSeconds = 0.25;

// How much of the tape stage's own Wear travel this pedal's knob reaches: a
// fully-turned Wear here drives the machine half as hard as a fully-turned Wear
// on Peak Tape.
//
// Not in TapeMachineConfig.h with the rest of the tape voicing, and not a change
// to the stage itself. The machine is shared, Peak Tape's knob still reaches all
// of it, and re-voicing the engine would move both pedals - which is the thing
// the shared stage exists to prevent. What is different here is only how far
// *this* face's knob turns it, which is this pedal's business: on Peak Tape the
// machine is the effect and its top end is the point, while here it is a colour
// on a delay and the top of that range swamped the repeats long before the knob
// ran out. Halving the reach spreads the useful part across the whole travel
// instead of the first half of it.
//
// It is deliberately a scale on the amount rather than a smaller parameter
// range: the knob still reads 0-100 %, because what it is a percentage of is
// "as worn as this pedal goes", not "as worn as the machine goes".
constexpr float kWearScale = 0.5f;

// The Input/Output faders' range. Asymmetric on purpose: they are a trim and a
// level, not a gain stage, so there is more cut than boost.
constexpr float kMinGainDb = -24.0f;
constexpr float kMaxGainDb = 12.0f;

/** "0.0 dB", "+3.5 dB", "-12.0 dB" - signed, because a fader whose whole job
    is "up or down from where it was" reads badly without the sign. Local, not
    in ee/plugin/ParamText.h: see that header on why the decibelsToText the
    other pedals have were deliberately left apart. */
juce::String gainDbToText (float value, int)
{
    const juce::String sign = value > 0.05f ? "+" : "";
    return sign + juce::String (value, 1) + " dB";
}

// What counts as a note, for the scope's playheads. A block has to clear
// kOnsetThreshold outright - -46 dBFS, below anything played on purpose and
// above a quiet room - and stand kOnsetOverFloor above whatever is still
// ringing, and no second note can start for kOnsetHoldSeconds afterwards.
//
// Tuned against synthetic plucks at several tempos and block sizes, which is
// the only way to pick numbers like these: 1.5 rather than a safer 1.8 because
// 1.8 misses more than half the notes in a run at 150 ms, and 70 ms rather than
// 120 because the refractory is meant to be a backstop against one note's own
// attack, not a speed limit on playing. A held chord still counts once.
constexpr float kOnsetThreshold = 0.005f;
constexpr float kOnsetOverFloor = 1.5f;
constexpr double kOnsetHoldSeconds = 0.07;

// How fast the "still ringing" floor forgets the last note. Long enough that a
// chord's own decay doesn't retrigger on its way down, short enough that the
// next note in a phrase has something to stand above.
constexpr double kOnsetFloorSeconds = 0.15;
juce::String infinitySymbol()
{
    return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
}

juce::String freqToText (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 1) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

// Both read as "nothing is being cut" at the resting end of their travel - the
// same two strings Peak EQ's corner knobs print, because these are the same
// pair of filters and should say the same thing about themselves.
juce::String loCutToText (float hz, int)
{
    return hz <= PeakDelayProcessor::kLoCutMinHz + 0.5f ? "0 Hz" : freqToText (hz);
}

juce::String hiCutToText (float hz, int)
{
    return hz >= PeakDelayProcessor::kHiCutMaxHz - 0.5f ? infinitySymbol() : freqToText (hz);
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
    typeParam = apvts.getRawParameterValue (kTypeID);
    timeUnitParam = apvts.getRawParameterValue (kTimeUnitID);
    feedbackParam = apvts.getRawParameterValue (kFeedbackID);
    mixParam = apvts.getRawParameterValue (kMixID);
    driftParam = apvts.getRawParameterValue (kDriftID);
    phaserParam = apvts.getRawParameterValue (kPhaserID);
    wearParam = apvts.getRawParameterValue (kWearID);
    flutterParam = apvts.getRawParameterValue (kFlutterID);
    loCutParam = apvts.getRawParameterValue (kLoCutID);
    hiCutParam = apvts.getRawParameterValue (kHiCutID);
    tapePreParam = apvts.getRawParameterValue (kTapePreID);
    inGainParam = apvts.getRawParameterValue (kInGainID);
    outGainParam = apvts.getRawParameterValue (kOutGainID);
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

bool PeakDelayProcessor::isSynced() const
{
    return timeUnitParam == nullptr || timeUnitParam->load() < 0.5f;
}

ee::dsp::TapeDelay::Routing PeakDelayProcessor::routing() const noexcept
{
    using Routing = ee::dsp::TapeDelay::Routing;

    switch (typeParam != nullptr ? juce::roundToInt (typeParam->load()) : 0)
    {
    case 1:
        return Routing::wide;
    case 2:
        return Routing::pingPong;
    default:
        return Routing::normal;
    }
}

bool PeakDelayProcessor::tapeIsPost() const noexcept
{
    // The parameter reads "is the tape stage in front of the delay", so this is
    // its inverse - named, like isSynced() above, for what processBlock asks.
    return tapePreParam != nullptr && tapePreParam->load() < 0.5f;
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakDelayProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // One normalised knob each; the Sync pill decides what it means (see
    // TimeMap.h). The host-facing text assumes the synced reading, the same way
    // Peak Trem & Pan's Rate does; the editor overrides it live off the pill.
    const auto timeAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return ee::peakdelay::timeMap().toText (v, true, ee::peakdelay::kReferenceBpm); });

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kLeftTimeID, 1 }, "Left Time",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             kDefaultTime01, timeAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kRightTimeID, 1 }, "Right Time",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             kDefaultTime01, timeAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSyncID, 1 }, "Sync L/R", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kTimeUnitID, 1 }, "Time Unit", false));

    // Normal first, so a session saved before this parameter existed loads at
    // index 0 and sounds exactly as it did.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kTypeID, 1 }, "Delay Type",
                                                              juce::StringArray { "Normal", "Wide", "Ping Pong" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kFeedbackID, 1 }, "Feedback", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kMixID, 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

    const auto percentRange = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto percentAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    // The four stage knobs all open at 0, so the pedal is a clean delay until
    // something is dialled in - the same resting position Tape and Mod have
    // always had, rather than the handoff mock's illustrative 22/44/30/18.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kWearID, 1 }, "Wear", percentRange,
                                                             0.0f, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kFlutterID, 1 }, "Flutter",
                                                             percentRange, 0.0f, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDriftID, 1 }, "Drift", percentRange,
                                                             0.0f, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kPhaserID, 1 }, "Phaser", percentRange,
                                                             0.0f, percentAttributes));

    // The Filter section. Ranges, skews and defaults are Peak EQ's, down to the
    // skew centres: the same two cuts, so the same knob position should mean
    // the same frequency on either pedal.
    auto loCutRange = juce::NormalisableRange<float> (kLoCutMinHz, kLoCutMaxHz);
    loCutRange.setSkewForCentre (120.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kLoCutID, 1 }, "Low Cut", loCutRange, kLoCutMinHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (loCutToText)));

    auto hiCutRange = juce::NormalisableRange<float> (kHiCutMinHz, kHiCutMaxHz);
    hiCutRange.setSkewForCentre (4000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kHiCutID, 1 }, "High Cut", hiCutRange, kHiCutMaxHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hiCutToText)));

    // A boolean rather than a choice: two states, and the face's router just
    // flips the flag. Named for the side it defaults to; the host text reads
    // Pre/Post, so that never shows up in a DAW.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kTapePreID, 1 }, "Tape Placement", true,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool pre, int) { return juce::String (pre ? "Pre" : "Post"); })));

    const auto gainRange = juce::NormalisableRange<float> (kMinGainDb, kMaxGainDb, 0.1f);
    const auto gainAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (gainDbToText);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kInGainID, 1 }, "Input", gainRange,
                                                             0.0f, gainAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kOutGainID, 1 }, "Output", gainRange,
                                                             0.0f, gainAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

double PeakDelayProcessor::readPlayHeadBpm()
{
    double bpm = 120.0;

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    bpm = juce::jlimit (20.0, 300.0, bpm);
    lastKnownBpm.store (bpm, std::memory_order_relaxed);

    return bpm;
}

double PeakDelayProcessor::currentBpm() const
{
    return lastKnownBpm.load (std::memory_order_relaxed);
}

juce::String PeakDelayProcessor::timeReadout (const std::atomic<float>* timeParam) const
{
    const float time01 = timeParam != nullptr ? timeParam->load() : kDefaultTime01;
    return ee::peakdelay::timeMap().toText (time01, isSynced(), currentBpm());
}

float PeakDelayProcessor::timeMs (const std::atomic<float>* timeParam) const
{
    const float time01 = timeParam != nullptr ? timeParam->load() : kDefaultTime01;
    return ee::peakdelay::timeMap().value (time01, isSynced(), currentBpm());
}

juce::String PeakDelayProcessor::timeMsReadout (const std::atomic<float>* timeParam) const
{
    return juce::String (juce::roundToInt (timeMs (timeParam))) + " ms";
}

void PeakDelayProcessor::mirrorTime (const juce::String& from, const juce::String& to)
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
        mirrorTime (kRightTimeID, kLeftTimeID);
    else
        mirrorTime (kLeftTimeID, kRightTimeID);

    mirroring = false;
}

void PeakDelayProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    sr = sampleRate;

    tapeIn.prepare (sampleRate);
    tapeOut.prepare (sampleRate);
    phaser.prepare (sampleRate);
    delay.prepare (sampleRate);

    // One channel per filter object, so each keeps its own state - the same
    // shape Peak EQ prepares its cuts in.
    const juce::dsp::ProcessSpec filterSpec { sampleRate, static_cast<juce::uint32> (maxBlock), 1 };

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        hiPass[static_cast<size_t> (ch)].prepare (filterSpec);
        hiPass[static_cast<size_t> (ch)].reset();
        loPass[static_cast<size_t> (ch)].prepare (filterSpec);
        loPass[static_cast<size_t> (ch)].reset();
    }

    updateFilters (true);

    // The phaser runs on Peak Phase's own default voicing - one knob here, so
    // everything but the amount is fixed once and never touched again.
    phaser.setRateHz (ee::dsp::phaser::kDefaultRateHz);
    phaser.setDepth01 (ee::dsp::phaser::kDefaultDepthPct * 0.01f);

    // Constant whatever the router says: both tape stages run exactly once per
    // block on either side of the delay, so what the host has to compensate
    // never moves. Drift is inside the delay line and the phaser is a wet/dry
    // blend, so neither adds any.
    // The dry path's, which is the one a host compensates against: it runs
    // through the pre section and nothing else, whatever the router says.
    setLatencySamples (tapeIn.latencySamples());

    // The post section sits between the delay and the output, so its latency
    // lands on the repeats and would push the first one late by that much.
    // Taken off the delay's own time instead, so the gap the Time knob names is
    // the gap you hear - in either placement, since both are always in circuit.
    postLatencySeconds = static_cast<float> (tapeOut.latencySamples() / sampleRate);

    peakLevelSmoothed = 0.0f;
    onsetFloor = 0.0f;
    onsetHoldSamples = 0;
    inputLevelUi.store (0.0f, std::memory_order_relaxed);

    stageBuffer.setSize (2, maxBlock, false, true, true);
    inputBuffer.setSize (2, maxBlock, false, true, true);
    wetBuffer.setSize (2, maxBlock, false, true, true);
    mixBuffer.setSize (2, maxBlock, false, true, true);
    modBuffer.setSize (2, maxBlock, false, true, true);
    engageRamp.assign (static_cast<size_t> (maxBlock), 0.0f);

    dryGain.reset (sampleRate, kGainRampSeconds);
    wetGain.reset (sampleRate, kGainRampSeconds);
    engageGain.reset (sampleRate, kGainRampSeconds);
    inGain.reset (sampleRate, kGainRampSeconds);
    outGain.reset (sampleRate, kGainRampSeconds);

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    tapePlacement.reset (sampleRate, kPlacementSeconds);
    tapePlacement.setCurrentAndTargetValue (tapeIsPost() ? 1.0f : 0.0f);
    updateTapeAmounts (tapePlacement.getCurrentValue());
    tapeIn.transport.snapSmoothing();
    tapeOut.transport.snapSmoothing();

    phaserBlend = juce::jlimit (0.0f, 1.0f, phaserParam->load() * 0.01f);
    delay.setRouting (routing());
    delay.setFeedback (feedbackParam->load() * 0.01f);
    delay.setModulation (driftParam->load() * 0.01f);
    // prepareToPlay is one of the callbacks where the playhead is valid, so
    // the cache starts out holding the host's real tempo rather than 120.
    const double startupBpm = readPlayHeadBpm();
    const bool startupSynced = isSynced();
    delay.setDelaySeconds (
        trimmedDelaySeconds (ee::peakdelay::timeSeconds (leftTimeParam->load(), startupSynced, startupBpm)),
        trimmedDelaySeconds (ee::peakdelay::timeSeconds (rightTimeParam->load(), startupSynced, startupBpm)));
    delay.snapDelays();

    dryGain.setCurrentAndTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setCurrentAndTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi));
    engageGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inGainParam->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outGainParam->load()));
}

void PeakDelayProcessor::releaseResources()
{
    tapeIn.reset();
    tapeOut.reset();
    phaser.reset();
    delay.reset();

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        hiPass[static_cast<size_t> (ch)].reset();
        loPass[static_cast<size_t> (ch)].reset();
    }
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

void PeakDelayProcessor::updateTapeAmounts (float placement) noexcept
{
    // Wear is scaled back to half the stage's travel - see kWearScale. Flutter
    // is not: the transport's wobble is a movement rather than a drive, and at
    // the top of its range it is still musical on a repeat.
    const float wear = juce::jlimit (0.0f, 1.0f, wearParam->load() * 0.01f) * kWearScale;
    const float flutter = juce::jlimit (0.0f, 1.0f, flutterParam->load() * 0.01f);

    // The router is a fader between the two placements, not a switch. A
    // straight linear split rather than equal power: at the halfway point the
    // section really is half as driven in each place, which is what keeps the
    // total colour roughly constant across the move - the two are the same
    // stage in series, not two takes of one signal being summed.
    tapeIn.setAmounts (wear * (1.0f - placement), flutter * (1.0f - placement));
    tapeOut.setAmounts (wear * placement, flutter * placement);
}

float PeakDelayProcessor::trimmedDelaySeconds (float seconds) const noexcept
{
    // Never below the delay line's own floor, which at the shortest time the
    // knob reaches is nowhere near - 62 ms against 6 ms of trim.
    return juce::jmax (0.001f, seconds - postLatencySeconds);
}

void PeakDelayProcessor::meterInput (const float* left, const float* right, int numSamples) noexcept
{
    float blockPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        blockPeak = juce::jmax (blockPeak, std::abs (left[i]), std::abs (right[i]));

    // In seconds rather than a per-block factor, so the follower doesn't chase
    // faster or slower depending on the host's block size - the same shape
    // Peak Wah's signal glow uses.
    const double blockSeconds = numSamples / sr;
    const float attackCoeff = static_cast<float> (std::exp (-blockSeconds / 0.005));
    const float releaseCoeff = static_cast<float> (std::exp (-blockSeconds / 0.30));
    const float coeff = blockPeak > peakLevelSmoothed ? attackCoeff : releaseCoeff;

    peakLevelSmoothed = coeff * peakLevelSmoothed + (1.0f - coeff) * blockPeak;

    const float db = juce::Decibels::gainToDecibels (peakLevelSmoothed, -60.0f);
    inputLevelUi.store (juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 40.0f), std::memory_order_relaxed);

    // The onset test runs off the raw block peak, not the smoothed level: the
    // follower's own attack would smear the very edge this is trying to find.
    // A note counts when it is both audible and clearly louder than whatever is
    // still ringing, which is what stops one long chord counting itself over
    // and over.
    onsetHoldSamples = juce::jmax (0, onsetHoldSamples - numSamples);

    // ...or when sound arrives at all after silence. Without this second way
    // in, a volume swell never counts: it grows by a fraction of a percent per
    // block, the floor tracks every step of the rise, and nothing ever stands
    // above it - so the scope would sit dark through a whole phrase that is
    // audibly being played.
    const bool fromSilence = onsetFloor < kOnsetThreshold;

    if (onsetHoldSamples == 0 && blockPeak > kOnsetThreshold &&
        (fromSilence || blockPeak > onsetFloor * kOnsetOverFloor))
    {
        strikeCountUi.fetch_add (1, std::memory_order_relaxed);
        onsetHoldSamples = static_cast<int> (kOnsetHoldSeconds * sr);
    }

    // The floor jumps to whatever just arrived and falls away over kOnsetFloor-
    // Seconds. In seconds, like the follower above and for the same reason: on
    // a per-block factor a host running 64-sample buffers would drop the floor
    // eight times faster than one running 512, and the same phrase would count
    // a different number of notes on each.
    const float floorCoeff = static_cast<float> (std::exp (-blockSeconds / kOnsetFloorSeconds));
    onsetFloor = blockPeak > onsetFloor ? blockPeak : onsetFloor * floorCoeff;
}

void PeakDelayProcessor::updateFilters (bool force)
{
    const float lo = loCutParam->load();

    if (force || std::abs (lo - loCutHz) > 1.0e-3f)
    {
        loCutHz = lo;
        hiPassActive = lo > kLoCutMinHz + 0.5f;

        if (hiPassActive)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, lo);
            for (int ch = 0; ch < kMaxChannels; ++ch)
                hiPass[static_cast<size_t> (ch)].coefficients = c;
        }
    }

    const float hi = hiCutParam->load();

    if (force || std::abs (hi - hiCutHz) > 1.0e-3f)
    {
        hiCutHz = hi;
        loPassActive = hi < kHiCutMaxHz - 0.5f;

        if (loPassActive)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, hi);
            for (int ch = 0; ch < kMaxChannels; ++ch)
                loPass[static_cast<size_t> (ch)].coefficients = c;
        }
    }
}

void PeakDelayProcessor::runFilter (float* left, float* right, int numSamples) noexcept
{
    if (! hiPassActive && ! loPassActive)
        return;

    float* io[kMaxChannels] = { left, right };

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        juce::dsp::AudioBlock<float> block (&io[ch], 1, static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> context (block);

        if (hiPassActive)
            hiPass[static_cast<size_t> (ch)].process (context);

        if (loPassActive)
            loPass[static_cast<size_t> (ch)].process (context);
    }
}

void PeakDelayProcessor::runPhaser (float* left, float* right, int numSamples) noexcept
{
    if (phaserBlend <= 0.0f)
        return;

    float* wetL = modBuffer.getWritePointer (0);
    float* wetR = modBuffer.getWritePointer (1);

    phaser.process (left, right, wetL, wetR, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] += (wetL[i] - left[i]) * phaserBlend;
        right[i] += (wetR[i] - right[i]) * phaserBlend;
    }
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

    // The time knobs are note values when synced, so a host that reports no
    // tempo still has to land somewhere musical - currentBpm() falls back to
    // 120. When not synced the knob is a plain millisecond time and the tempo
    // is not consulted at all, so free-running time neither retunes itself on a
    // host tempo change nor stays quantised to the divisions.
    const double bpm = readPlayHeadBpm();
    const bool synced = isSynced();

    delay.setDelaySeconds (trimmedDelaySeconds (ee::peakdelay::timeSeconds (leftTimeParam->load(), synced, bpm)),
                           trimmedDelaySeconds (ee::peakdelay::timeSeconds (rightTimeParam->load(), synced, bpm)));
    delay.setFeedback (feedbackParam->load() * 0.01f);

    // Which way the two lines are wired, this block. Cheap to set every time:
    // the engine ignores a routing it is already in, and the one thing a change
    // does move - Wide's Haas offset - glides there rather than stepping.
    delay.setRouting (routing());

    // Where the tape is, this block. Per block rather than per sample because
    // what it feeds is the two sections' Wear and Flutter, and those are
    // knob-rate settings with their own smoothing behind them - the same path a
    // hand on the Wear knob takes.
    tapePlacement.setTargetValue (tapeIsPost() ? 1.0f : 0.0f);
    updateTapeAmounts (tapePlacement.skip (numSamples));

    phaserBlend = juce::jlimit (0.0f, 1.0f, phaserParam->load() * 0.01f);
    updateFilters (false);

    // Drift is the delay line's own modulation: a slow wow on the tap plus a
    // rolloff, both inside the feedback path, so they compound with every
    // repeat rather than being applied once the way an insert would be. That
    // compounding is the whole point of the knob, and it is why the Mod
    // section has no Pre/Post - see tapeIsPost().
    delay.setModulation (driftParam->load() * 0.01f);

    const float mix = juce::jlimit (0.0f, 1.0f, mixParam->load() * 0.01f);
    const bool engaged = onParam->load() > 0.5f;

    // Trails: bypassing closes the input but leaves the repeats running out.
    dryGain.setTargetValue (engaged ? std::cos (mix * juce::MathConstants<float>::halfPi) : 1.0f);
    wetGain.setTargetValue (std::sin (mix * juce::MathConstants<float>::halfPi));
    engageGain.setTargetValue (engaged ? 1.0f : 0.0f);
    inGain.setTargetValue (juce::Decibels::decibelsToGain (inGainParam->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (outGainParam->load()));

    // What the face's scope animates from, taken off the untouched input
    // before anything below writes over the buffer: whether a note is being
    // played, not what the pedal is doing with it. The Input fader is
    // deliberately not in front of this - trimming the input should not make
    // the display think you stopped playing.
    meterInput (buffer.getReadPointer (0), numIn > 1 ? buffer.getReadPointer (1) : buffer.getReadPointer (0),
                numSamples);

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int chunk = juce::jmin (maxBlock, numSamples - offset);

        const float* inL = buffer.getReadPointer (0, offset);
        const float* inR = numIn > 1 ? buffer.getReadPointer (1, offset) : inL;

        // Walked once and kept: the pre and the post crossfade below both fade
        // against the same ramp, and a SmoothedValue only reads forwards.
        float* engage = engageRamp.data();

        for (int i = 0; i < chunk; ++i)
            engage[i] = engageGain.getNextValue();

        // The pedal's own input, trimmed by the Input fader. Everything past
        // here works on this rather than on the host's buffer, which stays
        // untouched as the bypass crossfade's reference - so a bypassed pedal
        // is unity whatever the fader says.
        float* preL = stageBuffer.getWritePointer (0);
        float* preR = stageBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            const float ig = inGain.getNextValue();

            preL[i] = inL[i] * ig;
            preR[i] = inR[i] * ig;
        }

        // The tape section in front of the delay. Always run, whatever the
        // router says: on Post its Wear and Flutter are at zero, where both
        // stages are bit-exact pass-through, so what it does then is supply the
        // dry path's 6 ms and nothing else. In front it colours the dry signal
        // as well as what goes on to be repeated - a pedal in front of a delay
        // is in front of all of it.
        tapeIn.process (preL, preR, chunk);

        for (int i = 0; i < chunk; ++i)
        {
            preL[i] = inL[i] + (preL[i] - inL[i]) * engage[i];
            preR[i] = inR[i] + (preR[i] - inR[i]) * engage[i];
        }

        float* feedL = inputBuffer.getWritePointer (0);
        float* feedR = inputBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            feedL[i] = preL[i] * engage[i];
            feedR[i] = preR[i] * engage[i];
        }

        float* wetL = wetBuffer.getWritePointer (0);
        float* wetR = wetBuffer.getWritePointer (1);
        delay.process (feedL, feedR, wetL, wetR, chunk);

        // ...and the one on the repeats, on the same terms: always in circuit,
        // silent until the router turns it up. Post puts the tape on the
        // delay's output and nothing else, so the wear and the flutter are on
        // the repeats and the note being played stays clean. It used to run on
        // the finished mix, dry included, which made Post audible on a signal
        // that had never been near the delay, at any Mix, even at zero.
        tapeOut.process (wetL, wetR, chunk);

        // The Filter section, on the repeats and nothing else - see runFilter.
        runFilter (wetL, wetR, chunk);

        // ...and the Mod section's insert half, last of the wet stages. On the
        // repeats and nothing else, for the same reason the tape's Post
        // placement is: a stage on the finished mix is audible on a dry signal
        // that never went near the delay, at any Mix setting and even at zero.
        runPhaser (wetL, wetR, chunk);

        float* mixL = mixBuffer.getWritePointer (0);
        float* mixR = mixBuffer.getWritePointer (1);

        for (int i = 0; i < chunk; ++i)
        {
            const float dg = dryGain.getNextValue();
            const float wg = wetGain.getNextValue();

            mixL[i] = preL[i] * dg + wetL[i] * wg;
            mixR[i] = preR[i] * dg + wetR[i] * wg;
        }

        // ...and what goes after the mix: the Output fader, on its own now
        // that the phaser has moved onto the repeats. stageBuffer is free
        // again by now - the pre pass is finished with and preL/preR have been
        // folded into the mix.

        // With the fader at 0 dB (exactly 1.0 out of decibelsToGain) the copy,
        // the multiply and the crossfade below are all exact, so the output is
        // bit identical to the mix rather than merely close.
        float* postL = stageBuffer.getWritePointer (0);
        float* postR = stageBuffer.getWritePointer (1);

        juce::FloatVectorOperations::copy (postL, mixL, chunk);
        juce::FloatVectorOperations::copy (postR, mixR, chunk);

        for (int i = 0; i < chunk; ++i)
        {
            const float og = outGain.getNextValue();

            postL[i] *= og;
            postR[i] *= og;
        }

        float* outL = buffer.getWritePointer (0, offset);
        float* outR = numOut > 1 ? buffer.getWritePointer (1, offset) : nullptr;

        for (int i = 0; i < chunk; ++i)
        {
            const float e = engage[i];
            const float l = mixL[i] + (postL[i] - mixL[i]) * e;
            const float r = mixR[i] + (postR[i] - mixR[i]) * e;

            if (outR != nullptr)
            {
                outL[i] = l;
                outR[i] = r;
            }
            else
            {
                outL[i] = 0.5f * (l + r);
            }
        }
    }
}

juce::AudioProcessorEditor* PeakDelayProcessor::createEditor()
{
    return new PeakDelayWebEditor (*this);
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
