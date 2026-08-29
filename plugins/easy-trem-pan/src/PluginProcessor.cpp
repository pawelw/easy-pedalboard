#include "PluginProcessor.h"

#include "RateMap.h"

#include "ee/dsp/Lfo.h"
#include "ee/ui/PedalEditor.h"

namespace {
constexpr const char *kAmountID = "amount";
constexpr const char *kRateID = "rate";
constexpr const char *kShapeID = "shape";
constexpr const char *kModeID = "mode";   // false = tremolo, true = panning
constexpr const char *kSyncID = "sync";   // true = tempo synced, false = free (ms)
constexpr const char *kOnID = "on";

// State-tree properties for the remembered per-mode Rate positions.
constexpr const char *kStoredSyncRateProp = "storedSyncRate01";
constexpr const char *kStoredFreeRateProp = "storedFreeRate01";

constexpr float kRampSeconds = 0.02f;

// Where the Rate knob lands the first time it is switched to free mode.
constexpr float kDefaultFreePeriodMs = 124.0f;

juce::String percentToText(float value, int) {
  return juce::String(juce::roundToInt(value)) + " %";
}
} // namespace

EasyTremPanProcessor::EasyTremPanProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  amountParam = apvts.getRawParameterValue(kAmountID);
  rateParam = apvts.getRawParameterValue(kRateID);
  shapeParam = apvts.getRawParameterValue(kShapeID);
  modeParam = apvts.getRawParameterValue(kModeID);
  syncParam = apvts.getRawParameterValue(kSyncID);
  onParam = apvts.getRawParameterValue(kOnID);

  // Default free-mode landing spot, and start the "other mode" memory at the
  // Rate parameter's own default so the first sync -> free -> sync round-trip is
  // lossless.
  storedFreeRate01.store(ee::trempan::rate01ForFreePeriodMs(kDefaultFreePeriodMs));
  storedSyncRate01.store(rateParam->load());
}

void EasyTremPanProcessor::onSyncToggled() {
  // syncParam already carries the new state by the time the click callback runs.
  const bool nowSynced = syncParam->load() > 0.5f;
  const float current = rateParam->load();

  // Park where the mode we just left was, then recall where the new mode was.
  if (nowSynced)
    storedFreeRate01.store(current);
  else
    storedSyncRate01.store(current);

  const float target =
      nowSynced ? storedSyncRate01.load() : storedFreeRate01.load();

  if (auto *rate = apvts.getParameter(kRateID))
    rate->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, target));
}

juce::AudioProcessorValueTreeState::ParameterLayout
EasyTremPanProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  const auto percent = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);
  const auto percentAttributes =
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percentToText);

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kAmountID, 1}, "Amount", percent, 50.0f, percentAttributes));

  // One normalised knob; the Sync switch decides what it means, and each mode's
  // last position is remembered (see parameterChanged). Up = faster in both
  // modes. The host-facing text assumes the synced reading; the editor overrides
  // it live.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kRateID, 1}, "Rate",
      juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(
          [](float v, int) { return ee::trempan::rateToText(v, true); })));

  // 0 % exp decay, 25 % ramp, 50 % triangle, 75 % soft square, 100 % rounded
  // rectangle. Defaults to the triangle - the canonical natural tremolo.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kShapeID, 1}, "Shape", percent, 50.0f, percentAttributes));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kModeID, 1}, "Panning", false));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kSyncID, 1}, "Tempo Sync", true));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kOnID, 1}, "On", true));

  return layout;
}

void EasyTremPanProcessor::prepareToPlay(double newSampleRate,
                                         int maximumExpectedSamplesPerBlock) {
  sampleRate = newSampleRate;

  const int maxBlock = juce::jmax(1, maximumExpectedSamplesPerBlock);

  lfoPhase = 0.0;
  expectedPpq = 0.0;
  haveExpectedPpq = false;
  wasPlaying = false;

  modZ1 = 0.0f;
  // ~2.5 ms one-pole.
  modSlewCoeff = 1.0f - std::exp(-1.0f / (0.0025f * static_cast<float>(newSampleRate)));

  dryBuffer.setSize(kMaxChannels, maxBlock, false, false, true);
  modBuffer.assign(static_cast<size_t>(maxBlock), 0.0f);

  const bool engaged = onParam->load() > 0.5f;

  depth.reset(newSampleRate, kRampSeconds);
  depth.setCurrentAndTargetValue(amountParam->load() * 0.01f);

  wetMix.reset(newSampleRate, kRampSeconds);
  wetMix.setCurrentAndTargetValue(engaged ? 1.0f : 0.0f);
}

void EasyTremPanProcessor::releaseResources() {}

bool EasyTremPanProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto &in = layouts.getMainInputChannelSet();
  const auto &out = layouts.getMainOutputChannelSet();

  if (in.isDisabled() || out.isDisabled())
    return false;

  const bool inOk = in == juce::AudioChannelSet::mono() ||
                    in == juce::AudioChannelSet::stereo();
  const bool outOk = out == juce::AudioChannelSet::mono() ||
                     out == juce::AudioChannelSet::stereo();

  // Panning needs two channels out; a stereo-in / mono-out would also throw
  // half the signal away.
  if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
    return false;

  return inOk && outOk;
}

juce::String EasyTremPanProcessor::rateReadout() const {
  return ee::trempan::rateToText(rateParam->load(), syncParam->load() > 0.5f);
}

void EasyTremPanProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                        juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();
  const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
  const int numOut = juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels());

  if (numOut == 0 || numSamples == 0)
    return;

  // Clear any output channels the input does not feed, then fan a genuine mono
  // input out across them so the pan has something to move.
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

  const float amount01 = juce::jlimit(0.0f, 1.0f, amountParam->load() * 0.01f);
  const float shape01 = juce::jlimit(0.0f, 1.0f, shapeParam->load() * 0.01f);
  const float rate01 = rateParam->load();
  const bool panning = modeParam->load() > 0.5f;
  const bool synced = syncParam->load() > 0.5f;
  const bool engaged = onParam->load() > 0.5f;

  const float periodSeconds = juce::jmax(
      1.0e-4f, ee::trempan::rateToPeriodSeconds(rate01, synced, bpm));
  const double phaseInc = 1.0 / (static_cast<double>(periodSeconds) * sampleRate);

  // The LFO always free-runs on lfoPhase, so a rate or division change never
  // steps the phase - it just carries on at a new speed. When synced to a
  // running transport we also align it to the host grid: a hard snap only on the
  // first playing block or a transport jump (loop / relocate); otherwise a
  // gentle per-block pull, capped small, so host ppq jitter and division changes
  // stay click-free and just re-settle over a fraction of a second.
  if (synced && havePpq && isPlaying) {
    const double cyclesPerQuarter =
        1.0 / juce::jmax(1.0e-4, static_cast<double>(
                                     ee::trempan::syncedDivisionBeats(rate01)));
    const double ppqPerSample = bpm / (60.0 * sampleRate);

    double target = ppqStart * cyclesPerQuarter;
    target -= std::floor(target);

    const bool jumped =
        !wasPlaying ||
        (haveExpectedPpq && std::abs(ppqStart - expectedPpq) > 0.25);

    if (jumped) {
      lfoPhase = target;
    } else {
      double err = target - lfoPhase;
      err -= std::round(err);   // wrap to [-0.5, 0.5]
      lfoPhase += juce::jlimit(-0.006, 0.006, 0.15 * err);
    }

    expectedPpq = ppqStart + numSamples * ppqPerSample;
    haveExpectedPpq = true;
  } else {
    haveExpectedPpq = false;   // next playing block re-aligns from scratch
  }
  wasPlaying = isPlaying;

  // Self-heal if a bad host value ever slipped a non-finite into the state -
  // otherwise a single NaN here would stick and roar.
  if (!std::isfinite(lfoPhase))
    lfoPhase = 0.0;
  if (!std::isfinite(modZ1))
    modZ1 = 0.0f;

  depth.setTargetValue(amount01);
  wetMix.setTargetValue(engaged ? 1.0f : 0.0f);

  if (numSamples > static_cast<int>(modBuffer.size()))
    modBuffer.assign(static_cast<size_t>(numSamples), 0.0f);

  // One shaped LFO value per sample, slew-limited so nothing steps the gain in a
  // single sample. Same helper the UI preview uses, so the drawing still tracks.
  for (int i = 0; i < numSamples; ++i) {
    const float raw = ee::dsp::lfoValue(static_cast<float>(lfoPhase), shape01);
    modZ1 += modSlewCoeff * (raw - modZ1);
    modBuffer[static_cast<size_t>(i)] = modZ1;

    lfoPhase += phaseInc;
    while (lfoPhase >= 1.0)
      lfoPhase -= 1.0;
  }

  if (numSamples > dryBuffer.getNumSamples())
    dryBuffer.setSize(kMaxChannels, numSamples, false, false, true);
  for (int ch = 0; ch < numCh; ++ch)
    dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

  if (panning && numCh >= 2) {
    // Equal-power auto-pan, sample accurate so it tracks the LFO exactly.
    // juce::dsp::Panner is the obvious reuse here, but it bakes in a fixed 50 ms
    // gain ramp that swallows anything moving at an LFO rate, so the pan law is
    // applied directly - unity in the centre, +3 dB / silence at the extremes.
    constexpr float kCentreComp = juce::MathConstants<float>::sqrt2;
    float *left = buffer.getWritePointer(0);
    float *right = buffer.getWritePointer(1);

    for (int i = 0; i < numSamples; ++i) {
      const float d = depth.getNextValue();
      const float pan = juce::jlimit(-1.0f, 1.0f, d * modBuffer[static_cast<size_t>(i)]);
      const float angle = (pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;

      left[i] *= std::cos(angle) * kCentreComp;
      right[i] *= std::sin(angle) * kCentreComp;
    }
  } else if (!panning) {
    // Tremolo: LFO at +1 is unity, at -1 is (1 - depth). JUCE has no tremolo
    // primitive, so the gain law is written out here.
    for (int i = 0; i < numSamples; ++i) {
      const float d = depth.getNextValue();
      const float g = 1.0f - d * (0.5f - 0.5f * modBuffer[static_cast<size_t>(i)]);
      for (int ch = 0; ch < numCh; ++ch)
        buffer.getWritePointer(ch)[i] *= g;
    }
  } else {
    // Panning asked for on a mono output: nothing sensible to sweep, leave dry.
    depth.skip(numSamples);
  }

  // Crossfade to the untouched dry copy when bypassed, so the host on/off never
  // clicks.
  for (int i = 0; i < numSamples; ++i) {
    const float wet = wetMix.getNextValue();
    const float dry = 1.0f - wet;
    for (int ch = 0; ch < numCh; ++ch) {
      float *out = buffer.getWritePointer(ch, i);
      *out = *out * wet + dryBuffer.getSample(ch, i) * dry;
    }
  }
}

juce::AudioProcessorEditor *EasyTremPanProcessor::createEditor() {
  ee::ui::PedalSpec spec;
  spec.name = "Easy Trem & Pan";
  spec.version = "v" JucePlugin_VersionString;

  spec.knobs = {
      {kAmountID, "Amount"},
      {.parameterID = kRateID,
       .caption = "Rate",
       .liveValueText = [this] { return rateReadout(); }},
      {kShapeID, "Shape"},
  };

  const juce::Colour cream{0xfffee1b8};

  // Big sliding switch, top-left: Tremolo on the left, Panning on the right.
  spec.slideToggle = ee::ui::SlideToggleSpec{
      .parameterID = kModeID, .labelOff = "Tremolo", .labelOn = "Panning",
      .accent = cream};

  // Tempo-sync toggle centred above the Rate knob, reusing Easy Delay's
  // MiniToggle and its amber lit colour.
  spec.toggles = {
      {.parameterID = kSyncID, .caption = "Sync", .afterKnobIndex = 1,
       .litColour = juce::Colour(0xffffaa33), .centeredAbove = true,
       .onClick = [this] { onSyncToggled(); }},
  };

  spec.waveDisplay = ee::ui::WaveDisplaySpec{.amountID = kAmountID,
                                             .rateID = kRateID,
                                             .shapeID = kShapeID,
                                             .modeID = kModeID};

  spec.knobsPerRow = 3;
  spec.width = ee::ui::knobRowWidth(spec.knobsPerRow);

  return new ee::ui::PedalEditor(*this, apvts, spec, ee::ui::PedalTheme::teal());
}

void EasyTremPanProcessor::getStateInformation(juce::MemoryBlock &destData) {
  auto state = apvts.copyState();
  state.setProperty(kStoredSyncRateProp, storedSyncRate01.load(), nullptr);
  state.setProperty(kStoredFreeRateProp, storedFreeRate01.load(), nullptr);
  if (auto xml = state.createXml())
    copyXmlToBinary(*xml, destData);
}

void EasyTremPanProcessor::setStateInformation(const void *data, int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    if (xml->hasTagName(apvts.state.getType())) {
      apvts.replaceState(juce::ValueTree::fromXml(*xml));

      const float freeDefault =
          ee::trempan::rate01ForFreePeriodMs(kDefaultFreePeriodMs);
      storedSyncRate01.store(static_cast<float>(
          apvts.state.getProperty(kStoredSyncRateProp, rateParam->load())));
      storedFreeRate01.store(static_cast<float>(
          apvts.state.getProperty(kStoredFreeRateProp, freeDefault)));
    }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new EasyTremPanProcessor();
}
