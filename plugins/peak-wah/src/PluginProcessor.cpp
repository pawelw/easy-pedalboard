#include "PluginProcessor.h"

#include "RateMap.h"

#include "ee/dsp/AutoWahConfig.h"
#include "ee/ui/PedalEditor.h"

#include <cmath>

namespace {
constexpr const char *kAmountID = "amount";
constexpr const char *kFreqID = "freq";
constexpr const char *kQID = "q";
constexpr const char *kMixID = "mix";
constexpr const char *kDecayID = "decay";
constexpr const char *kStereoID = "stereo";
constexpr const char *kShapeID = "shape";
constexpr const char *kTimeID = "time";      // meaning set by Sync (free ms / synced note)
constexpr const char *kTypeID = "ftype";     // 0 = Low, 1 = Band, 2 = High
constexpr const char *kRandomID = "random";
constexpr const char *kSyncID = "sync";
constexpr const char *kOnID = "on";

constexpr float kRampSeconds = 0.02f;
constexpr float kDefaultFreePeriodMs = 400.0f;

juce::String percentToText(float value, int) {
  return juce::String(juce::roundToInt(value)) + " %";
}

juce::String hzToText(float value, int) {
  return juce::String(juce::roundToInt(value)) + " Hz";
}

float freqHzFor(float pct) {
  const float t = std::pow(juce::jlimit(0.0f, 1.0f, pct * 0.01f),
                           ee::dsp::autowah::kFreqKnobSkew);
  return ee::dsp::autowah::kFreqMinHz *
         std::pow(ee::dsp::autowah::kFreqMaxHz / ee::dsp::autowah::kFreqMinHz, t);
}
} // namespace

PeakWahProcessor::PeakWahProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  amountParam = apvts.getRawParameterValue(kAmountID);
  freqParam = apvts.getRawParameterValue(kFreqID);
  qParam = apvts.getRawParameterValue(kQID);
  mixParam = apvts.getRawParameterValue(kMixID);
  decayParam = apvts.getRawParameterValue(kDecayID);
  stereoParam = apvts.getRawParameterValue(kStereoID);
  shapeParam = apvts.getRawParameterValue(kShapeID);
  timeParam = apvts.getRawParameterValue(kTimeID);
  typeParam = apvts.getRawParameterValue(kTypeID);
  randomParam = apvts.getRawParameterValue(kRandomID);
  syncParam = apvts.getRawParameterValue(kSyncID);
  onParam = apvts.getRawParameterValue(kOnID);

  storedFreeRate01.store(ee::peakwah::rate01ForFreePeriodMs(kDefaultFreePeriodMs));
  storedSyncRate01.store(timeParam->load());
}

void PeakWahProcessor::onSyncToggled() {
  // syncParam already carries the new state by the time the click callback runs.
  const bool nowSynced = syncParam->load() > 0.5f;
  const float current = timeParam->load();

  if (nowSynced)
    storedFreeRate01.store(current);
  else
    storedSyncRate01.store(current);

  const float target =
      nowSynced ? storedSyncRate01.load() : storedFreeRate01.load();

  if (auto *time = apvts.getParameter(kTimeID))
    time->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, target));
}

juce::AudioProcessorValueTreeState::ParameterLayout
PeakWahProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  const auto percent = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);
  const auto pctAttr =
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percentToText);

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kAmountID, 1}, "Amount", percent,
      ee::dsp::autowah::kDefaultAmountPct, pctAttr));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kFreqID, 1}, "Freq", percent,
      ee::dsp::autowah::kDefaultFreqPct,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return hzToText(freqHzFor(v), 0); })));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kQID, 1}, "Q", percent,
      ee::dsp::autowah::kDefaultQPct, pctAttr));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kMixID, 1}, "Mix", percent,
      ee::dsp::autowah::kDefaultMixPct, pctAttr));

  // How fast the wobble flattens after you stop playing; fully up latches it on.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kDecayID, 1}, "Decay", percent,
      ee::dsp::autowah::kDefaultDecayPct, pctAttr));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kShapeID, 1}, "Shape", percent,
      ee::dsp::autowah::kDefaultShapePct, pctAttr));

  // One normalised knob; the Sync switch decides what it means, and each mode's
  // last position is remembered. Up = faster in both modes. The host-facing text
  // assumes the synced reading; the editor overrides it live.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kTimeID, 1}, "Time",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.5f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return ee::peakwah::rateToText(v, true); })));

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{kTypeID, 1}, "Type",
      juce::StringArray{"Low", "Band", "High"}, ee::dsp::autowah::kDefaultType));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kStereoID, 1}, "Stereo", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kRandomID, 1}, "Random", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kSyncID, 1}, "Sync", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kOnID, 1}, "On", true));

  return layout;
}

void PeakWahProcessor::prepareToPlay(double newSampleRate,
                                     int maximumExpectedSamplesPerBlock) {
  sampleRate = newSampleRate;

  const int maxBlock = juce::jmax(1, maximumExpectedSamplesPerBlock);

  wah.prepare(newSampleRate);
  wah.reset();

  dryBuffer.setSize(kMaxChannels, maxBlock, false, false, true);

  const bool engaged = onParam->load() > 0.5f;
  wetMix.reset(newSampleRate, kRampSeconds);
  wetMix.setCurrentAndTargetValue(engaged ? 1.0f : 0.0f);

  haveExpectedPpq = false;
  wasPlaying = false;
}

void PeakWahProcessor::releaseResources() {}

bool PeakWahProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto &in = layouts.getMainInputChannelSet();
  const auto &out = layouts.getMainOutputChannelSet();

  if (in.isDisabled() || out.isDisabled())
    return false;

  const bool inOk = in == juce::AudioChannelSet::mono() ||
                    in == juce::AudioChannelSet::stereo();
  const bool outOk = out == juce::AudioChannelSet::mono() ||
                     out == juce::AudioChannelSet::stereo();

  // Stereo in / mono out would fold the L/R sweep back together.
  if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
    return false;

  return inOk && outOk;
}

juce::String PeakWahProcessor::freqReadout() const {
  return hzToText(freqHzFor(freqParam->load()), 0);
}

juce::String PeakWahProcessor::timeReadout() const {
  return ee::peakwah::rateToText(timeParam->load(), syncParam->load() > 0.5f);
}

juce::String PeakWahProcessor::typeReadout() const {
  const int t = juce::jlimit(0, 2, static_cast<int>(typeParam->load() + 0.5f));
  return t == 0 ? "Low" : t == 2 ? "High" : "Band";
}

void PeakWahProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                    juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();
  const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
  const int numOut = juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels());

  if (numOut == 0 || numSamples == 0)
    return;

  for (int ch = numIn; ch < numOut; ++ch)
    buffer.clear(ch, 0, numSamples);
  if (numIn == 1)
    for (int ch = 1; ch < numOut; ++ch)
      buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);

  const int numCh = juce::jmin(numOut, int{kMaxChannels});
  if (numCh == 0)
    return;

  double bpm = 120.0;
  bool havePpq = false;
  bool isPlaying = false;
  double ppqStart = 0.0;
  if (auto *playHead = getPlayHead())
    if (const auto position = playHead->getPosition()) {
      if (const auto hostBpm = position->getBpm())
        bpm = *hostBpm;
      if (const auto ppq = position->getPpqPosition()) {
        ppqStart = *ppq;
        havePpq = std::isfinite(ppqStart);
      }
      isPlaying = position->getIsPlaying();
    }
  if (!std::isfinite(bpm))
    bpm = 120.0;
  bpm = juce::jlimit(20.0, 300.0, bpm);

  const float time01 = timeParam->load();
  const bool synced = syncParam->load() > 0.5f;
  const bool engaged = onParam->load() > 0.5f;

  const float periodSeconds =
      juce::jmax(1.0e-4f, ee::peakwah::rateToPeriodSeconds(time01, synced, bpm));
  wah.setPeriodSeconds(periodSeconds);

  // The LFO always free-runs; when synced to a running transport we also align
  // it to the host grid - a hard snap on the first playing block or a transport
  // jump, otherwise a gentle per-block pull (see AutoWah::nudgePhase).
  if (synced && havePpq && isPlaying) {
    const double cyclesPerQuarter =
        1.0 / juce::jmax(1.0e-4, static_cast<double>(
                                     ee::peakwah::syncedDivisionBeats(time01)));
    const double ppqPerSample = bpm / (60.0 * sampleRate);

    const double target = ppqStart * cyclesPerQuarter;
    const bool jumped =
        !wasPlaying ||
        (haveExpectedPpq && std::abs(ppqStart - expectedPpq) > 0.25);

    if (jumped)
      wah.snapPhase(target);
    else
      wah.nudgePhase(target);

    expectedPpq = ppqStart + numSamples * ppqPerSample;
    haveExpectedPpq = true;
  } else {
    haveExpectedPpq = false;
  }
  wasPlaying = isPlaying;

  wah.setAmount01(amountParam->load() * 0.01f);
  wah.setFreq01(freqParam->load() * 0.01f);
  wah.setQ01(qParam->load() * 0.01f);
  wah.setMix01(mixParam->load() * 0.01f);
  wah.setDecay01(decayParam->load() * 0.01f);
  wah.setStereo(stereoParam->load() > 0.5f);
  wah.setShape01(shapeParam->load() * 0.01f);
  wah.setRandom(randomParam->load() > 0.5f);
  wah.setType(juce::jlimit(0, 2, static_cast<int>(typeParam->load() + 0.5f)));

  if (numSamples > dryBuffer.getNumSamples())
    dryBuffer.setSize(kMaxChannels, numSamples, false, false, true);
  for (int ch = 0; ch < numCh; ++ch)
    dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

  float *left = buffer.getWritePointer(0);
  float *right = numCh >= 2 ? buffer.getWritePointer(1) : nullptr;
  wah.process(left, right, numSamples);

  // Publish the LFO state for the editor's scope.
  lfoPhaseUi.store(static_cast<float>(wah.phase()), std::memory_order_relaxed);
  lfoDepthUi.store(wah.depth01(), std::memory_order_relaxed);

  wetMix.setTargetValue(engaged ? 1.0f : 0.0f);
  for (int i = 0; i < numSamples; ++i) {
    const float wet = wetMix.getNextValue();
    const float dry = 1.0f - wet;
    for (int ch = 0; ch < numCh; ++ch) {
      float *outSample = buffer.getWritePointer(ch, i);
      *outSample = *outSample * wet + dryBuffer.getSample(ch, i) * dry;
      if (!std::isfinite(*outSample))
        *outSample = 0.0f;
    }
  }
}

juce::AudioProcessorEditor *PeakWahProcessor::createEditor() {
  ee::ui::PedalSpec spec;
  spec.name = "Peak Wah";
  spec.version = "v" JucePlugin_VersionString;

  spec.knobs = {
      {kAmountID, "Amount"},
      {.parameterID = kFreqID,
       .caption = "Freq",
       .liveValueText = [this] { return freqReadout(); }},
      {kQID, "Q"},
      {kMixID, "Mix"},
  };
  spec.knobsPerRow = 4;
  spec.width = ee::ui::knobRowWidth(4);
  spec.knobDiameter = 74;

  const juce::Colour lit{0xffff4f97};

  spec.subKnobs = {
      {.parameterID = kDecayID, .caption = "Decay"},
      {.parameterID = kShapeID,
       .caption = "Shape",
       .buttonParameterID = kRandomID,
       .buttonCaption = "Rnd",
       .buttonLitColour = lit},
      {.parameterID = kTimeID,
       .caption = "Time",
       .liveValueText = [this] { return timeReadout(); },
       .buttonParameterID = kSyncID,
       .buttonCaption = "Sync",
       .buttonLitColour = lit,
       .buttonOnClick = [this] { onSyncToggled(); }},
      {.parameterID = kTypeID,
       .caption = "Type",
       .liveValueText = [this] { return typeReadout(); }},
  };

  spec.slideToggle = ee::ui::SlideToggleSpec{
      .parameterID = kStereoID, .labelOff = "Mono", .labelOn = "Stereo",
      .accent = juce::Colour{0xffff4f97}};
  spec.slideToggleBottom = true;

  spec.waveDisplay = ee::ui::WaveDisplaySpec{
      .amountID = kAmountID,
      .rateID = kTimeID,
      .shapeID = kShapeID,
      .modeID = kStereoID,
      .height = 40,
      .livePhase = [this] { return lfoPhaseUi.load(std::memory_order_relaxed); },
      .liveDepth = [this] { return lfoDepthUi.load(std::memory_order_relaxed); }};

  return new ee::ui::PedalEditor(*this, apvts, spec, ee::ui::PedalTheme::pink());
}

void PeakWahProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (auto xml = apvts.copyState().createXml())
    copyXmlToBinary(*xml, destData);
}

void PeakWahProcessor::setStateInformation(const void *data, int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    if (xml->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new PeakWahProcessor();
}
