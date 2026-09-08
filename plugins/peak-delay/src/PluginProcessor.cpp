#include "PluginProcessor.h"

#include "PeakDelayWebEditor.h"
#include "TimeMap.h"
#include "ee/plugin/Bypass.h"
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

// The chain's own smoothing time, restated by ee::fx::delaymodule for the
// reason that header gives. This is the one place both are visible.
static_assert (ee::fx::delaymodule::kGainRampSeconds == ee::plugin::kRampSeconds,
               "ee::fx::delaymodule::kGainRampSeconds must track ee::plugin::kRampSeconds");

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
    // Loading a preset is a whole tree arriving at once, which the L/R mirror
    // has to stand down for - see installState.
    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };

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

/** Installs a whole APVTS tree - a host restoring a session, a preset load -
    with the Sync L/R mirror held off for the length of it.

    The mirror below exists to follow a *person* turning one of the two Time
    knobs, and a wholesale install is not that. An install writes the button
    and both times, one parameter at a time and in the file's own order, so the
    mirror is called for the first of the three while the button's own value is
    still the one being replaced: a preset that wants its two sides apart,
    loaded on top of anything that had the button on, was collapsed onto one
    time before its "off" ever arrived. Peak Delay ships three presets like
    that, and a session saved with the button off was restored the same way.

    Reading the button out of the ValueTree instead - which by then does hold
    the new state - is not the fix it looks like: APVTS pushes a parameter's
    value back into the tree on a timer, so during an ordinary knob drag the
    tree is the half that lags, and the knobs would stop tracking each other
    for a moment after every Sync press.

    A tree arriving in one piece is self-consistent by construction, so there
    is nothing for the mirror to do during one anyway. */
void PeakDelayProcessor::installState (const juce::ValueTree& tree)
{
    installingState = true;
    apvts.replaceState (tree);
    installingState = false;
}

void PeakDelayProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (installingState.load())
        return;

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

    // Everything the chain needs, before it prepares: it seeds its own
    // smoothers and builds its filter coefficients from these, so a pedal
    // opened on a loaded preset starts where the preset says rather than ramping
    // to it from a default.
    // prepareToPlay is one of the callbacks where the playhead is valid, so the
    // cache starts out holding the host's real tempo rather than 120.
    const double startupBpm = readPlayHeadBpm();
    pushSettings (startupBpm, isSynced());
    chain.setFilterRestingPoints (kLoCutMinHz, kHiCutMaxHz);
    chain.prepare (sampleRate, maxBlock);

    // The dry path's, which is the one a host compensates against: it runs
    // through the pre section and nothing else, whatever the router says.
    // Constant whatever the router says, so what the host has to compensate
    // never moves.
    setLatencySamples (chain.latencySamples());

    peakLevelSmoothed = 0.0f;
    onsetFloor = 0.0f;
    onsetHoldSamples = 0;
    inputLevelUi.store (0.0f, std::memory_order_relaxed);
}

void PeakDelayProcessor::releaseResources()
{
    chain.reset();
}

double PeakDelayProcessor::getTailLengthSeconds() const
{
    return chain.tailSeconds();
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

/** Every setting the chain takes, in real units, read off the parameters. One
    place rather than two: prepareToPlay and processBlock both need the whole
    set, and a control that only got pushed from one of them would be wrong
    until the next block. */
void PeakDelayProcessor::pushSettings (double bpm, bool synced) noexcept
{
    chain.setTimes (ee::peakdelay::timeSeconds (leftTimeParam->load(), synced, bpm),
                    ee::peakdelay::timeSeconds (rightTimeParam->load(), synced, bpm));
    chain.setFeedback01 (feedbackParam->load() * 0.01f);
    chain.setRouting (routing());
    chain.setTapePost (tapeIsPost());
    chain.setTape (wearParam->load() * 0.01f, flutterParam->load() * 0.01f);
    chain.setDrift01 (driftParam->load() * 0.01f);
    chain.setPhaser01 (phaserParam->load() * 0.01f);
    chain.setFilter (loCutParam->load(), hiCutParam->load());
    chain.setMix01 (mixParam->load() * 0.01f);
    chain.setEngaged (onParam->load() > 0.5f);
    chain.setTrims (juce::Decibels::decibelsToGain (inGainParam->load()),
                    juce::Decibels::decibelsToGain (outGainParam->load()));
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
    pushSettings (readPlayHeadBpm(), isSynced());

    // What the face's scope animates from, taken off the untouched input
    // before anything below writes over the buffer: whether a note is being
    // played, not what the pedal is doing with it. The Input fader is
    // deliberately not in front of this - trimming the input should not make
    // the display think you stopped playing.
    meterInput (buffer.getReadPointer (0), numIn > 1 ? buffer.getReadPointer (1) : buffer.getReadPointer (0),
                numSamples);

    chain.process (buffer, numIn, numOut, numSamples);
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
            installState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakDelayProcessor();
}
