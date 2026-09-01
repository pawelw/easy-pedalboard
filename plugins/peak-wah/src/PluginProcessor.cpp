#include "PluginProcessor.h"

#include "RateMap.h"

#include "ee/dsp/AutoWahConfig.h"
#include "ee/dsp/Lfo.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#include <cmath>

namespace
{
using ee::plugin::percentToText;

using ee::plugin::kRampSeconds;

/** Height of one of the three response shapes at 0..1 across the band.
    tap: 0 = low-pass, 1 = band-pass, 2 = high-pass. */
float filterShapeHeight (float t, int tap)
{
    if (tap == 1) // band-pass: centred bump
        return std::exp (-0.5f * std::pow ((t - 0.5f) / 0.16f, 2.0f));

    const float u = tap == 0 ? t : 1.0f - t; // shelf + resonant lip
    return 0.8f / (1.0f + std::pow (u / 0.5f, 4.0f)) + 0.45f * std::exp (-0.5f * std::pow ((u - 0.5f) / 0.09f, 2.0f));
}

/** A little filter-curve glyph for the Type knob's cap. The knob morphs
    continuously, so the curve crossfades with it - low-pass at 0, band-pass at
    ½, high-pass at 1 - the same way the Shape glyph morphs its wave. */
void drawFilterTypeIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float morph01)
{
    const auto r = area.reduced (area.getWidth() * 0.12f, area.getHeight() * 0.26f);
    const float m = juce::jlimit (0.0f, 1.0f, morph01);
    const int lower = m <= 0.5f ? 0 : 1;
    const float blend = m <= 0.5f ? m * 2.0f : (m - 0.5f) * 2.0f;

    juce::Path p;
    const int steps = 40;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float)i / (float)steps;
        const float a = filterShapeHeight (t, lower);
        const float b = filterShapeHeight (t, lower + 1);
        const float v = a + blend * (b - a);
        const float x = r.getX() + t * r.getWidth();
        const float y = r.getBottom() - juce::jlimit (0.0f, 1.15f, v) * r.getHeight();
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** One cycle of the morphing LFO wave, for the Shape knob's cap.

    Drawn mirrored about the centre line. `lfoValue` peaks at +1 at phase 0, so
    a cycle plotted the usual way up starts at the top and dips - it reads as a
    trough, which is not what the pedal sounds like it is doing. Flipped, the
    glyph opens upward: a triangle reads as a peak, a ramp as a rise. The wave
    that is heard is unchanged - only this picture of it is mirrored. */
void drawLfoShapeIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float shape01)
{
    const auto r = area.reduced (area.getWidth() * 0.10f, area.getHeight() * 0.28f);
    const float midY = r.getCentreY();
    const float amp = r.getHeight() * 0.5f;
    juce::Path p;
    const int steps = 48;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float)i / (float)steps;
        const float y = midY + ee::dsp::lfoValue (t, shape01) * amp;
        const float x = r.getX() + t * r.getWidth();
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

constexpr const char* kRangeID = "range";
constexpr const char* kFreqID = "freq";
constexpr const char* kQID = "q";
constexpr const char* kMixID = "mix";
constexpr const char* kDecayID = "decay";
constexpr const char* kStereoID = "stereo";
constexpr const char* kShapeID = "shape";
constexpr const char* kTimeID = "time";  // meaning set by Sync (free ms / synced note)
constexpr const char* kTypeID = "ftype"; // 0 % = Low, 50 % = Band, 100 % = High
constexpr const char* kSyncID = "sync";
constexpr const char* kOnID = "on";

constexpr float kDefaultFreePeriodMs = 400.0f;

// Cap sizes on the face. The row is laid out to the larger of the two, so the
// six plain knobs ask for the smaller and Shape and Type - the pair carrying a
// glyph on the cap - keep the full size.
// The one colour on the face that is not the display's own: the mark on the top
// of the Decay scale, where the sweep latches on and simply runs.
const juce::Colour kLatchGreen { 0xff2f6b46 };

constexpr int kIconKnobDiameter = 104;
constexpr int kPlainKnobDiameter = 88;

juce::String hzToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " Hz";
}

/** The Type knob's reading: the named taps at the three anchors, and how far
    between two of them everywhere else. */
juce::String filterTypeToText (float pct, int)
{
    const float p = juce::jlimit (0.0f, 100.0f, pct);

    if (p <= 2.0f)
        return "Low";
    if (p >= 98.0f)
        return "High";
    if (std::abs (p - 50.0f) <= 2.0f)
        return "Band";

    const bool lower = p < 50.0f;
    const int mix = juce::roundToInt (lower ? p * 2.0f : (p - 50.0f) * 2.0f);
    return juce::String (lower ? "Low-Band " : "Band-High ") + juce::String (mix) + " %";
}

float freqHzFor (float pct)
{
    const float t = std::pow (juce::jlimit (0.0f, 1.0f, pct * 0.01f), ee::dsp::autowah::kFreqKnobSkew);
    return ee::dsp::autowah::kFreqMinHz * std::pow (ee::dsp::autowah::kFreqMaxHz / ee::dsp::autowah::kFreqMinHz, t);
}
} // namespace

PeakWahProcessor::PeakWahProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    rangeParam = apvts.getRawParameterValue (kRangeID);
    freqParam = apvts.getRawParameterValue (kFreqID);
    qParam = apvts.getRawParameterValue (kQID);
    mixParam = apvts.getRawParameterValue (kMixID);
    decayParam = apvts.getRawParameterValue (kDecayID);
    stereoParam = apvts.getRawParameterValue (kStereoID);
    shapeParam = apvts.getRawParameterValue (kShapeID);
    timeParam = apvts.getRawParameterValue (kTimeID);
    typeParam = apvts.getRawParameterValue (kTypeID);
    syncParam = apvts.getRawParameterValue (kSyncID);
    onParam = apvts.getRawParameterValue (kOnID);

    storedFreeRate01.store (ee::peakwah::rate01ForFreePeriodMs (kDefaultFreePeriodMs));
    storedSyncRate01.store (timeParam->load());
}

void PeakWahProcessor::onSyncToggled()
{
    // syncParam already carries the new state by the time the click callback runs.
    const bool nowSynced = syncParam->load() > 0.5f;
    const float current = timeParam->load();

    if (nowSynced)
        storedFreeRate01.store (current);
    else
        storedSyncRate01.store (current);

    const float target = nowSynced ? storedSyncRate01.load() : storedFreeRate01.load();

    if (auto* time = apvts.getParameter (kTimeID))
        time->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, target));
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakWahProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto pctAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kRangeID, 1 }, "Range", percent,
                                                             ee::dsp::autowah::kDefaultRangePct, pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kFreqID, 1 }, "Freq", percent, ee::dsp::autowah::kDefaultFreqPct,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                                           { return hzToText (freqHzFor (v), 0); })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kQID, 1 }, "Q", percent,
                                                             ee::dsp::autowah::kDefaultQPct, pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMixID, 1 }, "Mix", percent,
                                                             ee::dsp::autowah::kDefaultMixPct, pctAttr));

    // How fast the wobble flattens after you stop playing; fully up latches it on.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kDecayID, 1 }, "Decay", percent,
                                                             ee::dsp::autowah::kDefaultDecayPct, pctAttr));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kShapeID, 1 }, "Shape", percent,
                                                             ee::dsp::autowah::kDefaultShapePct, pctAttr));

    // One normalised knob; the Sync switch decides what it means, and each mode's
    // last position is remembered. Up = faster in both modes. The host-facing text
    // assumes the synced reading; the editor overrides it live.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTimeID, 1 }, "Time", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return ee::peakwah::rateToText (v, true); })));

    // Continuous, not a three-way switch: the tank's low-, band- and high-pass
    // taps crossfade into each other the way Shape morphs the LFO wave.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTypeID, 1 }, "Type", percent, ee::dsp::autowah::kDefaultTypePct,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (filterTypeToText)));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kStereoID, 1 }, "Stereo", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kSyncID, 1 }, "Sync", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakWahProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate;

    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    wah.prepare (newSampleRate);
    wah.reset();

    dryBuffer.setSize (kMaxChannels, maxBlock, false, false, true);

    const bool engaged = onParam->load() > 0.5f;
    wetMix.reset (newSampleRate, kRampSeconds);
    wetMix.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);

    haveExpectedPpq = false;
    wasPlaying = false;
}

void PeakWahProcessor::releaseResources() {}

bool PeakWahProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // Stereo in / mono out would fold the L/R sweep back together.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

juce::String PeakWahProcessor::freqReadout() const
{
    return hzToText (freqHzFor (freqParam->load()), 0);
}

juce::String PeakWahProcessor::timeReadout() const
{
    return ee::peakwah::rateToText (timeParam->load(), syncParam->load() > 0.5f);
}

juce::String PeakWahProcessor::typeReadout() const
{
    return filterTypeToText (typeParam->load(), 0);
}

void PeakWahProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    const float time01 = timeParam->load();
    const bool synced = syncParam->load() > 0.5f;
    const bool engaged = onParam->load() > 0.5f;

    const float periodSeconds = juce::jmax (1.0e-4f, ee::peakwah::rateToPeriodSeconds (time01, synced, bpm));
    wah.setPeriodSeconds (periodSeconds);

    // The LFO always free-runs; when synced to a running transport we also align
    // it to the host grid - a hard snap on the first playing block or a transport
    // jump, otherwise a gentle per-block pull (see AutoWah::nudgePhase).
    if (synced && havePpq && isPlaying)
    {
        const double cyclesPerQuarter =
            1.0 / juce::jmax (1.0e-4, static_cast<double> (ee::peakwah::syncedDivisionBeats (time01)));
        const double ppqPerSample = bpm / (60.0 * sampleRate);

        const double target = ppqStart * cyclesPerQuarter;
        const bool jumped = ! wasPlaying || (haveExpectedPpq && std::abs (ppqStart - expectedPpq) > 0.25);

        if (jumped)
            wah.snapPhase (target);
        else
            wah.nudgePhase (target);

        expectedPpq = ppqStart + numSamples * ppqPerSample;
        haveExpectedPpq = true;
    }
    else
    {
        haveExpectedPpq = false;
    }
    wasPlaying = isPlaying;

    wah.setRange01 (rangeParam->load() * 0.01f);
    wah.setFreq01 (freqParam->load() * 0.01f);
    wah.setQ01 (qParam->load() * 0.01f);
    wah.setMix01 (mixParam->load() * 0.01f);
    wah.setDecay01 (decayParam->load() * 0.01f);
    wah.setStereo (stereoParam->load() > 0.5f);
    wah.setShape01 (shapeParam->load() * 0.01f);
    wah.setTypeMorph01 (typeParam->load() * 0.01f);

    if (numSamples > dryBuffer.getNumSamples())
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    float* left = buffer.getWritePointer (0);
    float* right = numCh >= 2 ? buffer.getWritePointer (1) : nullptr;
    wah.process (left, right, numSamples);

    // Publish the LFO state for the editor's response scope.
    lfoModLUi.store (wah.modL(), std::memory_order_relaxed);
    lfoModRUi.store (wah.modR(), std::memory_order_relaxed);

    wetMix.setTargetValue (engaged ? 1.0f : 0.0f);
    ee::plugin::crossfadeToDry (buffer, dryBuffer, wetMix, numCh, numSamples);
}

juce::AudioProcessorEditor* PeakWahProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Wah";

    // Two clusters of four, split by a rule: the filter itself on the left, what
    // moves it on the right. The right-hand four show their reading only while
    // they are being turned.
    const auto modKnob = [] (const char* id, juce::String caption)
    {
        ee::ui::KnobSpec k;
        k.parameterID = id;
        k.caption = std::move (caption);
        k.captionUntilTouched = true;
        return k;
    };

    // Shape and Type are the two knobs whose setting is a shape rather than a
    // number, so their glyph goes on the cap - always in view, and drawn big
    // enough to read. They keep the row's full cap size; the six that only
    // carry a number are a size down, which is what marks the pair out.
    auto shape = modKnob (kShapeID, "Shape");
    shape.capIcon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    { drawLfoShapeIcon (g, r, c, shapeParam->load() * 0.01f); };

    auto time = modKnob (kTimeID, "Time");
    time.liveValueText = [this] { return timeReadout(); };
    time.diameter = kPlainKnobDiameter;

    auto type = modKnob (kTypeID, "Filter Type");
    type.capIcon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    { drawFilterTypeIcon (g, r, c, typeParam->load() * 0.01f); };

    // Decay fully up latches the sweep on for good, which is a different thing
    // from a very long tail rather than more of one - so the end of its scale
    // gets a mark of its own.
    auto decay = modKnob (kDecayID, "Decay");
    decay.diameter = kPlainKnobDiameter;
    decay.endMarker = kLatchGreen;
    decay.endMarkerLabel = juce::String::fromUTF8 ("\u221e");

    spec.knobs = {
        { .parameterID = kMixID, .caption = "Mix" }, // full size: the headline control
        { .parameterID = kFreqID,
          .caption = "Freq",
          .diameter = kPlainKnobDiameter,
          .liveValueText = [this] { return freqReadout(); } },
        decay,
        shape,
        { .parameterID = kQID, .caption = "Q", .diameter = kPlainKnobDiameter },
        { .parameterID = kRangeID, .caption = "Range", .diameter = kPlainKnobDiameter },
        time,
        type,
    };
    spec.knobsPerRow = 4;
    spec.knobDividerAfterColumn = 2;
    spec.knobBlockRise = 10;  // a little clear of the scope below
    spec.displayBandRise = 8; // and the scope off the name row

    // Tighter than the shared gap: each cluster is a 2x2 block, and pulling its
    // rows together is what makes the two blocks read as two, rather than as
    // one grid of eight with a rule through it.
    spec.knobRowGap = 4;

    // Hand-picked rather than `knobRowWidth (4)`, which is the one place in the
    // repo that departs from it. Four shared columns come to 650 and leave the
    // caps swimming; four of these narrower ones fit caps that fill them, with
    // room for the tick ring the digital cap carries outside itself and for the
    // Sync switch under the Time knob, which is wider than a bezel button was.
    spec.width = 566;
    spec.knobDiameter = kIconKnobDiameter;

    // Sync hangs under the Time knob, on the second row of the right cluster,
    // as a switch: Sync on the left, free milliseconds on the right. The
    // parameter is true when synced, so the switch is inverted to put its set
    // state on the left where the label reads first.
    spec.toggles = { { .parameterID = kSyncID,
                       .caption = "Sync",
                       .afterKnobIndex = 6,
                       .centeredBelow = true,
                       .onClick = [this] { onSyncToggled(); },
                       .asSwitch =
                           ee::ui::SlideToggleSpec { .labelOff = "ms", .labelOn = "Sync", .invertPosition = true } } };

    // The emblem and the name pair up at the right end of the bottom row, which
    // hands the row the name had back to the knobs and leaves the left of that
    // row for the Mono/Stereo switch - its labels in black, its first letter
    // flush with the left edge of the scope above it.
    spec.titleBesideLogo = true;
    spec.titleRowAlignRight = true;
    spec.titleRowRightInset = 34; // off the edge, under the last knob column

    spec.slideToggle = ee::ui::SlideToggleSpec {
        .parameterID = kStereoID, .labelOff = "Mono", .labelOn = "Stereo", .labelFlushLeft = true
    };
    spec.slideToggleBottom = true;

    // The resting curve is the only colour on the face; the two swept ones are
    // the pale wash it rides over, so they take the screen's own grey.
    spec.filterScope = ee::ui::FilterScopeSpec { .baseFreqHz = [this] { return freqHzFor (freqParam->load()); },
                                                 .resonance01 = [this] { return qParam->load() * 0.01f; },
                                                 .modL = [this] { return lfoModLUi.load (std::memory_order_relaxed); },
                                                 .modR = [this] { return lfoModRUi.load (std::memory_order_relaxed); },
                                                 .sweepDepth01 = [this] { return rangeParam->load() * 0.01f; },
                                                 .baseColour = juce::Colour { 0xffc2562f },
                                                 .sweepColour = juce::Colour { 0xff9aa0aa },
                                                 .sweepRatioMax = ee::dsp::autowah::kSweepRatioMax,
                                                 .height = 66 };

    return new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::white());
}

void PeakWahProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakWahProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakWahProcessor();
}
