#include "PluginProcessor.h"

#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#include <cmath>

namespace
{
using ee::plugin::kRampSeconds;
using ee::plugin::percentToText;

constexpr const char* kOnID = "on";
constexpr const char* kModeID = "tune.mode";
constexpr const char* kKeyID = "tune.key";
constexpr const char* kOctaveID = "octave";
constexpr const char* kDecayID = "decay";
constexpr const char* kDampingID = "damping";
constexpr const char* kCouplingID = "coupling";
constexpr const char* kSpreadID = "spread";
constexpr const char* kBloomID = "bloom";
constexpr const char* kSensID = "sensitivity";
constexpr const char* kMixID = "mix";
constexpr const char* kFreezeID = "freeze";
constexpr const char* kLearnID = "learn";

const juce::StringArray kModeNames { "Octaves", "Fifths", "Harmonic", "Major", "Minor", "Chroma" };
const juce::StringArray kKeyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** A ring / chain-link glyph for the Learn button - "lock the bank onto what I
    am playing". Two interlocked rounded rectangles, the way Peak Delay draws
    its tempo-link. */
void drawLearnIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto box = area.reduced (area.getWidth() * 0.12f);
    g.setColour (colour);
    const float t = juce::jmax (1.4f, box.getWidth() * 0.12f);
    const float w = box.getWidth() * 0.62f;
    const float h = box.getHeight() * 0.46f;
    g.drawRoundedRectangle (box.getX(), box.getCentreY() - h, w, h * 2.0f, h, t);
    g.drawRoundedRectangle (box.getRight() - w, box.getCentreY() - h, w, h * 2.0f, h, t);
}
} // namespace

PeakSympathyProcessor::PeakSympathyProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    onParam = apvts.getRawParameterValue (kOnID);
    modeParam = apvts.getRawParameterValue (kModeID);
    keyParam = apvts.getRawParameterValue (kKeyID);
    octaveParam = apvts.getRawParameterValue (kOctaveID);
    decayParam = apvts.getRawParameterValue (kDecayID);
    dampingParam = apvts.getRawParameterValue (kDampingID);
    couplingParam = apvts.getRawParameterValue (kCouplingID);
    spreadParam = apvts.getRawParameterValue (kSpreadID);
    bloomParam = apvts.getRawParameterValue (kBloomID);
    sensParam = apvts.getRawParameterValue (kSensID);
    mixParam = apvts.getRawParameterValue (kMixID);
    freezeParam = apvts.getRawParameterValue (kFreezeID);
    learnParam = apvts.getRawParameterValue (kLearnID);

    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };

    apvts.addParameterListener (kLearnID, this);
}

PeakSympathyProcessor::~PeakSympathyProcessor()
{
    apvts.removeParameterListener (kLearnID, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakSympathyProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kModeID, 1 }, "Tuning", kModeNames,
                                                              3)); // Major (JI)

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kKeyID, 1 }, "Key", kKeyNames,
                                                              9)); // A -> 110 Hz root

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { kOctaveID, 1 }, "Octave", ee::dsp::sympathy::kOctaveMin, ee::dsp::sympathy::kOctaveMax, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return juce::String (v > 0 ? "+" : "") + juce::String (v); })));

    // 0..100 maps log to kDecayMinSeconds..kDecayMaxSeconds in the engine.
    layout.add (
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDecayID, 1 }, "Decay", percent, 45.0f, pct));
    // 0 dark .. 100 bright.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDampingID, 1 }, "Damping", percent,
                                                             55.0f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kCouplingID, 1 }, "Coupling", percent,
                                                             25.0f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kSpreadID, 1 }, "Spread", percent,
                                                             25.0f, pct));
    layout.add (
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kBloomID, 1 }, "Bloom", percent, 0.0f, pct));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kSensID, 1 }, "Sensitivity", percent,
                                                             55.0f, pct));
    layout.add (
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMixID, 1 }, "Mix", percent, 50.0f, pct));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kFreezeID, 1 }, "Freeze", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kLearnID, 1 }, "Key Learn", false));

    return layout;
}

void PeakSympathyProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    bank.prepare (sampleRate);
    bank.reset();

    dryBuffer.setSize (kMaxChannels, maxBlock, false, false, true);
    wetBuffer.setSize (kMaxChannels, maxBlock, false, false, true);
    dryGain.assign (static_cast<size_t> (maxBlock), 1.0f);

    // ~46 ms of history at any sample rate, enough for the pitch tracker to
    // reach down to kLearnMinHz.
    pitchRing.assign (static_cast<size_t> (juce::jmax (2048, static_cast<int> (sampleRate * 0.05))), 0.0f);
    pitchWork.assign (pitchRing.size(), 0.0f);
    pitchRingWrite = 0;
    learnCandidate = -1;
    learnHoldSamples = 0;

    const bool engaged = onParam->load() > 0.5f;
    wetMix.reset (sampleRate, kRampSeconds);
    wetMix.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void PeakSympathyProcessor::releaseResources() {}

bool PeakSympathyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

juce::String PeakSympathyProcessor::keyReadout() const
{
    if (learnParam->load() > 0.5f)
    {
        const int k = learnedKey.load();
        if (k >= 0)
            return kKeyNames[k] + juce::String (" \xE2\x86\x90"); // "A <-"  (tracking)
        return juce::String ("listening\xE2\x80\xA6");
    }
    const int idx = juce::jlimit (0, 11, static_cast<int> (std::lround (keyParam->load())));
    return kKeyNames[idx];
}

int PeakSympathyProcessor::trackKey (const float* mono, int numSamples)
{
    const int ringLen = static_cast<int> (pitchRing.size());

    for (int i = 0; i < numSamples; ++i)
    {
        pitchRing[static_cast<size_t> (pitchRingWrite)] = mono[i];
        if (++pitchRingWrite >= ringLen)
            pitchRingWrite = 0;
    }

    // Unwrap the ring into oldest-first order (pitchWork is preallocated, so no
    // audio-thread allocation).
    float* x = pitchWork.data();
    for (int i = 0; i < ringLen; ++i)
        x[i] = pitchRing[static_cast<size_t> ((pitchRingWrite + i) % ringLen)];

    const int minLag = juce::jmax (2, static_cast<int> (sampleRate / ee::dsp::sympathy::kLearnMaxHz));
    const int maxLag = juce::jmin (ringLen - 1, static_cast<int> (sampleRate / ee::dsp::sympathy::kLearnMinHz));
    const int window = ringLen - maxLag;
    if (window < 256 || maxLag <= minLag)
        return learnedKey.load();

    double energy = 0.0;
    for (int i = 0; i < window; ++i)
        energy += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    if (energy < 1.0e-4)
    {
        learnCandidate = -1;
        learnHoldSamples = 0;
        return learnedKey.load();
    }

    // YIN-style squared-difference function; take its clearest minimum.
    double bestVal = 1.0e30;
    int bestLag = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double d = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const float diff = x[static_cast<size_t> (i)] - x[static_cast<size_t> (i + lag)];
            d += static_cast<double> (diff) * diff;
        }
        if (d < bestVal)
        {
            bestVal = d;
            bestLag = lag;
        }
    }

    // Periodicity gate: the best difference must be well below the window
    // energy, or the input is not a pitched note.
    if (bestLag < 1 || bestVal > 0.30 * 2.0 * energy)
    {
        learnCandidate = -1;
        learnHoldSamples = 0;
        return learnedKey.load();
    }

    // Parabolic interpolation around the minimum for sub-sample lag.
    double lagStar = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        double dm = 0.0, d0 = bestVal, dp = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const float a = x[static_cast<size_t> (i)] - x[static_cast<size_t> (i + bestLag - 1)];
            const float b = x[static_cast<size_t> (i)] - x[static_cast<size_t> (i + bestLag + 1)];
            dm += static_cast<double> (a) * a;
            dp += static_cast<double> (b) * b;
        }
        const double denom = dm - 2.0 * d0 + dp;
        if (std::abs (denom) > 1.0e-12)
            lagStar = bestLag + 0.5 * (dm - dp) / denom;
    }

    const double f0 = sampleRate / lagStar;
    if (f0 < ee::dsp::sympathy::kLearnMinHz || f0 > ee::dsp::sympathy::kLearnMaxHz)
        return learnedKey.load();

    const double semis = 12.0 * std::log2 (f0 / ee::dsp::sympathy::kRootBaseHz);
    const int nearest = static_cast<int> (std::lround (semis));
    const double cents = 100.0 * (semis - nearest);
    const int key = ((nearest % 12) + 12) % 12;

    if (std::abs (cents) > ee::dsp::sympathy::kLearnStableCents)
    {
        learnCandidate = -1;
        learnHoldSamples = 0;
        return learnedKey.load();
    }

    if (key == learnCandidate)
        learnHoldSamples += numSamples;
    else
    {
        learnCandidate = key;
        learnHoldSamples = numSamples;
    }

    if (learnHoldSamples >= static_cast<int> (sampleRate * ee::dsp::sympathy::kLearnHoldMs * 0.001))
        learnedKey.store (key);

    return learnedKey.load();
}

void PeakSympathyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
    if (numOut == 0 || numSamples == 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numIn == 1)
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    const int numCh = juce::jmin (numOut, int { kMaxChannels });
    if (numCh == 0)
        return;

    if (numSamples > dryBuffer.getNumSamples())
    {
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
        wetBuffer.setSize (kMaxChannels, numSamples, false, false, true);
        dryGain.assign (static_cast<size_t> (numSamples), 1.0f);
    }
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // --- push the knobs to the engine --------------------------------------
    const auto mode = static_cast<ee::dsp::sympathy::TuningMode> (
        juce::jlimit (0, static_cast<int> (ee::dsp::sympathy::TuningMode::count) - 1,
                      static_cast<int> (std::lround (modeParam->load()))));

    int key = juce::jlimit (0, 11, static_cast<int> (std::lround (keyParam->load())));
    const bool learning = learnParam->load() > 0.5f;
    if (learning)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (numCh > 1 ? 1 : 0);
        // mono mix into a scratch, then track
        auto* mono = wetBuffer.getWritePointer (0); // borrow ch 0 briefly, cleared below
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (l[i] + r[i]);
        const int k = trackKey (mono, numSamples);
        if (k >= 0)
            key = k;
    }

    bank.setTuning (mode, key);
    bank.setOctave (static_cast<int> (std::lround (octaveParam->load())));
    bank.setDecay01 (decayParam->load() * 0.01f);
    bank.setDamping01 (dampingParam->load() * 0.01f);
    bank.setCoupling01 (couplingParam->load() * 0.01f);
    bank.setSpread01 (spreadParam->load() * 0.01f);
    bank.setBloom01 (bloomParam->load() * 0.01f);
    bank.setSensitivity01 (sensParam->load() * 0.01f);
    bank.setFreeze (freezeParam->load() > 0.5f);

    // --- render ----------------------------------------------------------
    wetBuffer.clear();
    const float* inL = dryBuffer.getReadPointer (0);
    const float* inR = dryBuffer.getReadPointer (numCh > 1 ? 1 : 0);

    bank.updateBlock (numSamples);
    bank.render (inL, inR, wetBuffer.getWritePointer (0), wetBuffer.getWritePointer (1), dryGain.data(), numSamples);

    // --- mix: Mix knob + the per-sample envelope dry duck ----------------
    const float mix = mixParam->load() * 0.01f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        const float* dry = dryBuffer.getReadPointer (ch);
        const float* wet = wetBuffer.getReadPointer (juce::jmin (ch, 1));
        for (int i = 0; i < numSamples; ++i)
            out[i] = (1.0f - mix) * dry[i] * dryGain[static_cast<size_t> (i)] + mix * wet[i];
    }

    // --- bypass crossfade to the untouched dry -------------------------
    wetMix.setTargetValue (onParam->load() > 0.5f ? 1.0f : 0.0f);
    ee::plugin::crossfadeToDry (buffer, dryBuffer, wetMix, numCh, numSamples);
}

void PeakSympathyProcessor::parameterChanged (const juce::String& id, float newValue)
{
    if (id != kLearnID)
        return;

    if (newValue > 0.5f)
    {
        // Engaged: start a fresh estimate.
        learnedKey.store (-1);
        learnCandidate = -1;
        learnHoldSamples = 0;
    }
    else
    {
        // Released: bank the tracked key into the Key parameter so it sticks.
        const int k = learnedKey.load();
        if (k >= 0)
            if (auto* p = apvts.getParameter (kKeyID))
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (k)));
    }
}

juce::AudioProcessorEditor* PeakSympathyProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Sympathy";
    spec.tagline = "Sixteen tuned strings ring behind the player";
    spec.version = "v" JucePlugin_VersionString;

    spec.knobs = {
        { kModeID, "Tuning" },
        { .parameterID = kKeyID, .caption = "Key", .liveValueText = [this] { return keyReadout(); } },
        { kDecayID, "Decay" },
        { kDampingID, "Damping" },
        { kCouplingID, "Coupling" },
        { kSpreadID, "Spread" },
        { kBloomID, "Bloom" },
        { .parameterID = kOctaveID, .caption = "Octave", .bipolarArc = true, .centreDetent = true },
        { kSensID, "Sens" },
        { kMixID, "Mix" },
    };

    // Live / Freeze rides the strip across the top-left; the preset bar is
    // centred in the same strip.
    spec.slideToggle = ee::ui::SlideToggleSpec { .parameterID = kFreezeID, .labelOff = "Live", .labelOn = "Freeze" };

    spec.presetBar = ee::ui::PresetBarSpec {
        .names =
            [this]
        {
            juce::StringArray all = presets.factoryNames();
            all.addArray (presets.userNames());
            return all;
        },
        .currentIndex =
            [this]
        {
            const auto factory = presets.factoryNames();
            const int f = factory.indexOf (presets.currentName());
            if (presets.currentKind() == ee::plugin::PresetStore::Kind::factory)
                return f;
            const int u = presets.userNames().indexOf (presets.currentName());
            return u < 0 ? -1 : factory.size() + u;
        },
        .onSelect =
            [this] (int i)
        {
            const auto factory = presets.factoryNames();
            if (i < factory.size())
                presets.load (ee::plugin::PresetStore::Kind::factory, factory[i]);
            else
                presets.load (ee::plugin::PresetStore::Kind::user, presets.userNames()[i - factory.size()]);
        },
        .onSave =
            [this]
        {
            if (presets.currentKind() == ee::plugin::PresetStore::Kind::user && presets.currentName().isNotEmpty())
                presets.saveUser (presets.currentName());
            else
                presets.saveUser ("Preset " + juce::String (presets.userNames().size() + 1));
        },
        .onSaveAsNew = [this] { presets.saveUser ("Preset " + juce::String (presets.userNames().size() + 1)); },
        .onPrev = [this] { presets.step (-1); },
        .onNext = [this] { presets.step (1); },
        .width = 300,
    };

    // Key-Learn: a small chain-link button in the gap after the Key knob.
    spec.toggles = {
        { .parameterID = kLearnID, .caption = "Learn", .afterKnobIndex = 1, .icon = drawLearnIcon },
    };

    // Ten knobs, five a row, held to a four-knob body the way Peak Trem & Pan
    // holds four to three - the row layout shrinks the caps to fit.
    spec.knobsPerRow = 5;
    spec.width = ee::ui::knobRowWidth (4);

    return new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::green());
}

void PeakSympathyProcessor::installState (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
}

void PeakSympathyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakSympathyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            installState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakSympathyProcessor();
}
