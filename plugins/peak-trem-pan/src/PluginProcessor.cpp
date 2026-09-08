#include "PluginProcessor.h"

#include "RateMap.h"

#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

namespace
{
using ee::plugin::kRampSeconds;
using ee::plugin::percentToText;

// The engine restates this rather than including ee/plugin - see
// TremoloConfig.h. This is the one place both are visible, so it is the one
// place the tie can be checked.
static_assert (ee::dsp::tremolo::kSmoothingSeconds == kRampSeconds,
               "ee::dsp::tremolo::kSmoothingSeconds must track ee::plugin::kRampSeconds");

constexpr const char* kAmountID = "amount";
constexpr const char* kRateID = "rate";
constexpr const char* kShapeID = "shape";
constexpr const char* kBiasID = "bias"; // 0 = clean opto tremolo, 100 = bias-tube
constexpr const char* kModeID = "mode"; // false = tremolo, true = panning
constexpr const char* kSyncID = "sync"; // true = tempo synced, false = free (ms)
constexpr const char* kOnID = "on";

// State-tree properties for the remembered per-mode Rate positions.
constexpr const char* kStoredSyncRateProp = "storedSyncRate01";
constexpr const char* kStoredFreeRateProp = "storedFreeRate01";

// Where the Rate knob lands the first time it is switched to free mode.
constexpr float kDefaultFreePeriodMs = 124.0f;

/** A plain "ms" wordmark for the tempo-sync button, the same glyph Peak Delay
    carries on its unit toggle. The Rate knob reads a note division when synced
    and a period in milliseconds when free, so the button that flips between the
    two is marked for the free reading rather than lettered "SYNC". The bezel
    hands us a square; the wordmark is wider than it is tall, so it borrows the
    width it needs and is lettered close to the square's height. */
void drawMsIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto box = area.withSizeKeepingCentre (area.getWidth() * 1.6f, area.getHeight());
    g.setColour (colour);
    g.setFont (juce::Font (juce::FontOptions (area.getHeight() * 0.95f)).boldened());
    g.drawText ("ms", box, juce::Justification::centred, false);
}
} // namespace

PeakTremPanProcessor::PeakTremPanProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    amountParam = apvts.getRawParameterValue (kAmountID);
    rateParam = apvts.getRawParameterValue (kRateID);
    shapeParam = apvts.getRawParameterValue (kShapeID);
    biasParam = apvts.getRawParameterValue (kBiasID);
    modeParam = apvts.getRawParameterValue (kModeID);
    syncParam = apvts.getRawParameterValue (kSyncID);
    onParam = apvts.getRawParameterValue (kOnID);

    // Default free-mode landing spot, and start the "other mode" memory at the
    // Rate parameter's own default so the first sync -> free -> sync round-trip is
    // lossless.
    storedFreeRate01.store (ee::trempan::rate01ForFreePeriodMs (kDefaultFreePeriodMs));
    storedSyncRate01.store (rateParam->load());
}

void PeakTremPanProcessor::onSyncToggled()
{
    // syncParam already carries the new state by the time the click callback runs.
    const bool nowSynced = syncParam->load() > 0.5f;
    const float current = rateParam->load();

    // Park where the mode we just left was, then recall where the new mode was.
    if (nowSynced)
        storedFreeRate01.store (current);
    else
        storedSyncRate01.store (current);

    const float target = nowSynced ? storedSyncRate01.load() : storedFreeRate01.load();

    if (auto* rate = apvts.getParameter (kRateID))
        rate->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, target));
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakTremPanProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto percentAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kAmountID, 1 }, "Amount", percent,
                                                             50.0f, percentAttributes));

    // One normalised knob; the Sync switch decides what it means, and each mode's
    // last position is remembered (see parameterChanged). Up = faster in both
    // modes. The host-facing text assumes the synced reading; the editor overrides
    // it live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kRateID, 1 }, "Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return ee::trempan::rateToText (v, true); })));

    // 0 % exp decay, 25 % ramp, 50 % triangle, 75 % soft square, 100 % rounded
    // rectangle. Defaults to the triangle - the canonical natural tremolo.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kShapeID, 1 }, "Shape", percent, 50.0f,
                                                             percentAttributes));

    // 0 % is the clean opto tremolo; turning it up crossfades in the bias-tube
    // stage. Defaults off so the pedal still opens sounding like it always did.
    // Labelled "Tube" on the face; the parameter ID stays "bias" for the DSP.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kBiasID, 1 }, "Tube", percent, 0.0f,
                                                             percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kModeID, 1 }, "Panning", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSyncID, 1 }, "Tempo Sync", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakTremPanProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    // A host that probes with prepareToPlay(0, 0) would otherwise leave a zero
    // here, and 1.0 / (period * 0) = +inf feeds an inf into the phase accumulator.
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    // Set before prepare: the engine seeds its own smoothers from these, so a
    // pedal opened at a non-default depth does not ramp up to it from silence.
    tremolo.setAmount01 (amountParam->load() * 0.01f);
    tremolo.setShape01 (shapeParam->load() * 0.01f);
    tremolo.setBias01 (biasParam->load() * 0.01f);
    tremolo.setPanning (modeParam->load() > 0.5f);
    tremolo.prepare (newSampleRate, maxBlock);

    dryBuffer.setSize (kMaxChannels, maxBlock, false, false, true);

    const bool engaged = onParam->load() > 0.5f;

    wetMix.reset (newSampleRate, kRampSeconds);
    wetMix.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void PeakTremPanProcessor::releaseResources() {}

bool PeakTremPanProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // Panning needs two channels out; a stereo-in / mono-out would also throw
    // half the signal away.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

juce::String PeakTremPanProcessor::rateReadout() const
{
    return ee::trempan::rateToText (rateParam->load(), syncParam->load() > 0.5f);
}

void PeakTremPanProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numOut == 0 || numSamples == 0)
        return;

    // Clear any output channels the input does not feed, then fan a genuine mono
    // input out across them so the pan has something to move.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numIn == 1)
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    const int numCh = juce::jmin (numOut, int { kMaxChannels });
    if (numCh == 0)
        return;

    double bpm = 120.0;
    bool havePpq = false;
    bool isPlaying = false;
    double ppqStart = 0.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;
            if (const auto ppq = position->getPpqPosition())
            {
                ppqStart = *ppq;
                havePpq = std::isfinite (ppqStart);
            }
            isPlaying = position->getIsPlaying();
        }
    if (! std::isfinite (bpm))
        bpm = 120.0;
    bpm = juce::jlimit (20.0, 300.0, bpm);

    const float rate01 = rateParam->load();
    const bool panning = modeParam->load() > 0.5f;
    const bool synced = syncParam->load() > 0.5f;
    const bool engaged = onParam->load() > 0.5f;

    tremolo.setAmount01 (amountParam->load() * 0.01f);
    tremolo.setShape01 (shapeParam->load() * 0.01f);
    tremolo.setBias01 (biasParam->load() * 0.01f);
    tremolo.setPanning (panning);
    tremolo.setPeriodSeconds (ee::trempan::rateToPeriodSeconds (rate01, synced, bpm));

    // The Rate knob's division is this pedal's own mapping, so the engine is
    // told how many LFO cycles fit in a quarter note rather than being handed
    // the knob. `synced` for the engine means all three conditions at once -
    // the switch, a finite ppq, and a running transport; `playing` is the
    // transport alone, and the two are not the same test. See Tremolo::Transport.
    ee::dsp::Tremolo::Transport transport;
    transport.synced = synced && havePpq && isPlaying;
    transport.playing = isPlaying;
    transport.ppqStart = ppqStart;
    transport.cyclesPerQuarter =
        1.0 / juce::jmax (1.0e-4, static_cast<double> (ee::trempan::syncedDivisionBeats (rate01)));
    transport.ppqPerSample = bpm / (60.0 * sampleRate);

    wetMix.setTargetValue (engaged ? 1.0f : 0.0f);

    if (numSamples > dryBuffer.getNumSamples())
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    tremolo.process (buffer, numCh, numSamples, transport);

    // Crossfade to the untouched dry copy when bypassed, so the host on/off never
    // clicks.
    ee::plugin::crossfadeToDry (buffer, dryBuffer, wetMix, numCh, numSamples);
}

juce::AudioProcessorEditor* PeakTremPanProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Trem & Pan";
    spec.version = "v" JucePlugin_VersionString;

    spec.knobs = {
        { kAmountID, "Amount" },
        { .parameterID = kRateID, .caption = "Rate", .liveValueText = [this] { return rateReadout(); } },
        { kShapeID, "Shape" },
        { kBiasID, "Tube" },
    };

    const juce::Colour cream { 0xfffee1b8 };

    // Big sliding switch, top-left: Tremolo on the left, Panning on the right.
    spec.slideToggle = ee::ui::SlideToggleSpec {
        .parameterID = kModeID, .labelOff = "Tremolo", .labelOn = "Panning", .accent = cream
    };

    // Tempo-sync toggle centred above the Rate knob. It is Peak Delay's own
    // soft-UI button, borrowed onto this analog face: a rounded-square bezel
    // carrying the "ms" wordmark rather than a lettered "SYNC", inked while
    // synced and pale while the Rate knob is running free in milliseconds.
    spec.toggles = {
        { .parameterID = kSyncID,
          .caption = "Sync",
          .afterKnobIndex = 1,
          .centeredAbove = true,
          .onClick = [this] { onSyncToggled(); },
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
    };

    spec.waveDisplay =
        ee::ui::WaveDisplaySpec { .amountID = kAmountID, .rateID = kRateID, .shapeID = kShapeID, .modeID = kModeID };

    // Four knobs across, but held to the three-knob footprint: the row layout
    // shrinks the caps to fit rather than widening the pedal, so it still racks
    // up flush against the others.
    spec.knobsPerRow = 4;
    spec.width = ee::ui::knobRowWidth (3);

    return new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::teal());
}

void PeakTremPanProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kStoredSyncRateProp, storedSyncRate01.load(), nullptr);
    state.setProperty (kStoredFreeRateProp, storedFreeRate01.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakTremPanProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const float freeDefault = ee::trempan::rate01ForFreePeriodMs (kDefaultFreePeriodMs);
            storedSyncRate01.store (
                static_cast<float> (apvts.state.getProperty (kStoredSyncRateProp, rateParam->load())));
            storedFreeRate01.store (static_cast<float> (apvts.state.getProperty (kStoredFreeRateProp, freeDefault)));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakTremPanProcessor();
}
