#include "PluginProcessor.h"

#include "ee/ui/PedalEditor.h"

namespace {
constexpr const char *kLevelID = "level";
constexpr const char *kLoCutID = "locut";
constexpr const char *kHiCutID = "hicut";
constexpr const char *kOnID = "on";

juce::String infinitySymbol() {
  return juce::String(juce::CharPointer_UTF8("\xe2\x88\x9e"));
}

juce::String freqToText(float hz) {
  if (hz >= 1000.0f)
    return juce::String(hz / 1000.0f, 1) + " kHz";
  return juce::String(juce::roundToInt(hz)) + " Hz";
}

juce::String loCutToText(float hz, int) {
  // Off = not cutting anything, i.e. a 0 Hz high-pass.
  return hz <= EasyEqProcessor::kLoCutMinHz + 0.5f ? "0 Hz" : freqToText(hz);
}

juce::String hiCutToText(float hz, int) {
  return hz >= EasyEqProcessor::kHiCutMaxHz - 0.5f ? infinitySymbol() : freqToText(hz);
}

// Band parameter IDs, low to high, in the same order as
// EasyEqProcessor::kBandFrequencies.
constexpr std::array<const char *, EasyEqProcessor::kNumBands> kBandIDs { {
    "b100", "b200", "b400", "b800", "b1k6", "b3k2", "b6k4"
} };

// Captions printed under each fader.
constexpr std::array<const char *, EasyEqProcessor::kNumBands> kBandCaptions { {
    "100", "200", "400", "800", "1.6k", "3.2k", "6.4k"
} };

// Broad bells that overlap from one band to the next, the way the GE-7's do.
constexpr float kBandQ = 1.4f;

constexpr float kGainDb = 15.0f;   // +/- travel on every fader
constexpr float kRampSeconds = 0.02f;

juce::String decibelsToText(float value, int) {
  // Number only - the row is narrow and a graphic EQ's scale is understood to
  // be in dB.
  const int rounded = juce::roundToInt(value);
  return (rounded > 0 ? "+" : "") + juce::String(rounded);
}
} // namespace

EasyEqProcessor::EasyEqProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  for (int i = 0; i < kNumBands; ++i)
    bandParams[static_cast<size_t>(i)] =
        apvts.getRawParameterValue(kBandIDs[static_cast<size_t>(i)]);

  levelParam = apvts.getRawParameterValue(kLevelID);
  loCutParam = apvts.getRawParameterValue(kLoCutID);
  hiCutParam = apvts.getRawParameterValue(kHiCutID);
  onParam = apvts.getRawParameterValue(kOnID);
}

juce::AudioProcessorValueTreeState::ParameterLayout
EasyEqProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  const auto dbRange = juce::NormalisableRange<float>(-kGainDb, kGainDb, 0.1f);
  const auto dbAttributes =
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(decibelsToText);

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kLevelID, 1}, "Level", dbRange, 0.0f, dbAttributes));

  const char *bandNames[kNumBands] = {"100 Hz",  "200 Hz",  "400 Hz", "800 Hz",
                                      "1.6 kHz", "3.2 kHz", "6.4 kHz"};

  for (int i = 0; i < kNumBands; ++i)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kBandIDs[static_cast<size_t>(i)], 1},
        bandNames[i], dbRange, 0.0f, dbAttributes));

  auto loCutRange =
      juce::NormalisableRange<float>(kLoCutMinHz, kLoCutMaxHz);
  loCutRange.setSkewForCentre(120.0f);
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kLoCutID, 1}, "Low Cut", loCutRange, kLoCutMinHz,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(loCutToText)));

  auto hiCutRange =
      juce::NormalisableRange<float>(kHiCutMinHz, kHiCutMaxHz);
  hiCutRange.setSkewForCentre(4000.0f);
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kHiCutID, 1}, "High Cut", hiCutRange, kHiCutMaxHz,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(hiCutToText)));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kOnID, 1}, "On", true));

  return layout;
}

void EasyEqProcessor::updateFilters(bool force) {
  for (int b = 0; b < kNumBands; ++b) {
    const float db = bandParams[static_cast<size_t>(b)]->load();

    if (!force && std::abs(db - bandGainDb[static_cast<size_t>(b)]) < 1.0e-3f)
      continue;

    bandGainDb[static_cast<size_t>(b)] = db;

    // One coefficient object, shared by both channels' filter for this band.
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, kBandFrequencies[static_cast<size_t>(b)], kBandQ,
        juce::Decibels::decibelsToGain(db));

    for (int ch = 0; ch < kMaxChannels; ++ch)
      filters[static_cast<size_t>(ch)][static_cast<size_t>(b)].coefficients = coeffs;
  }

  const float lo = loCutParam->load();
  if (force || std::abs(lo - loCutHz) > 1.0e-3f) {
    loCutHz = lo;
    hiPassActive = lo > kLoCutMinHz + 0.5f;
    if (hiPassActive) {
      auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, lo);
      for (int ch = 0; ch < kMaxChannels; ++ch)
        hiPass[static_cast<size_t>(ch)].coefficients = c;
    }
  }

  const float hi = hiCutParam->load();
  if (force || std::abs(hi - hiCutHz) > 1.0e-3f) {
    hiCutHz = hi;
    loPassActive = hi < kHiCutMaxHz - 0.5f;
    if (loPassActive) {
      auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, hi);
      for (int ch = 0; ch < kMaxChannels; ++ch)
        loPass[static_cast<size_t>(ch)].coefficients = c;
    }
  }
}

void EasyEqProcessor::prepareToPlay(double newSampleRate,
                                    int maximumExpectedSamplesPerBlock) {
  sampleRate = newSampleRate;

  const int maxBlock = juce::jmax(1, maximumExpectedSamplesPerBlock);

  const juce::dsp::ProcessSpec spec{newSampleRate,
                                    static_cast<juce::uint32>(maxBlock), 1};

  for (auto &chain : filters)
    for (auto &filter : chain) {
      filter.prepare(spec);
      filter.reset();
    }

  for (int ch = 0; ch < kMaxChannels; ++ch) {
    hiPass[static_cast<size_t>(ch)].prepare(spec);
    hiPass[static_cast<size_t>(ch)].reset();
    loPass[static_cast<size_t>(ch)].prepare(spec);
    loPass[static_cast<size_t>(ch)].reset();
  }

  updateFilters(true);

  dryBuffer.setSize(2, maxBlock, false, false, true);

  const bool engaged = onParam->load() > 0.5f;

  levelGain.reset(newSampleRate, kRampSeconds);
  levelGain.setCurrentAndTargetValue(
      engaged ? juce::Decibels::decibelsToGain(levelParam->load()) : 1.0f);

  wetMix.reset(newSampleRate, kRampSeconds);
  wetMix.setCurrentAndTargetValue(engaged ? 1.0f : 0.0f);
}

void EasyEqProcessor::releaseResources() {
  for (auto &chain : filters)
    for (auto &filter : chain)
      filter.reset();

  for (int ch = 0; ch < kMaxChannels; ++ch) {
    hiPass[static_cast<size_t>(ch)].reset();
    loPass[static_cast<size_t>(ch)].reset();
  }
}

bool EasyEqProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const {
  const auto &in = layouts.getMainInputChannelSet();
  const auto &out = layouts.getMainOutputChannelSet();

  if (in.isDisabled() || out.isDisabled() || in != out)
    return false;

  return in == juce::AudioChannelSet::mono() ||
         in == juce::AudioChannelSet::stereo();
}

void EasyEqProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                   juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();
  const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
  const int numOut = juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels());

  if (numOut == 0 || numSamples == 0)
    return;

  for (int ch = numIn; ch < numOut; ++ch)
    buffer.clear(ch, 0, numSamples);

  const int numCh = juce::jmin(juce::jmin(numIn, numOut), int{kMaxChannels});
  if (numCh == 0)
    return;

  const bool engaged = onParam->load() > 0.5f;

  updateFilters(false);
  levelGain.setTargetValue(engaged ? juce::Decibels::decibelsToGain(levelParam->load())
                                   : 1.0f);
  wetMix.setTargetValue(engaged ? 1.0f : 0.0f);

  // Dry copy kept for the bypass crossfade, so switching off never clicks.
  if (numSamples > dryBuffer.getNumSamples())
    dryBuffer.setSize(2, numSamples, false, false, true);

  for (int ch = 0; ch < numCh; ++ch)
    dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

  // Low cut, seven peak bands, high cut - in series, per channel.
  for (int ch = 0; ch < numCh; ++ch) {
    float *data = buffer.getWritePointer(ch);
    auto &chain = filters[static_cast<size_t>(ch)];
    auto &hp = hiPass[static_cast<size_t>(ch)];
    auto &lp = loPass[static_cast<size_t>(ch)];

    for (int i = 0; i < numSamples; ++i) {
      float s = data[i];
      if (hiPassActive)
        s = hp.processSample(s);
      for (auto &filter : chain)
        s = filter.processSample(s);
      if (loPassActive)
        s = lp.processSample(s);
      data[i] = s;
    }

    for (auto &filter : chain)
      filter.snapToZero();
    hp.snapToZero();
    lp.snapToZero();
  }

  // Make-up level, then crossfade to the dry copy when bypassed.
  for (int i = 0; i < numSamples; ++i) {
    const float g = levelGain.getNextValue();
    const float wet = wetMix.getNextValue();
    const float dry = 1.0f - wet;

    for (int ch = 0; ch < numCh; ++ch) {
      float *out = buffer.getWritePointer(ch, i);
      *out = (*out * g) * wet + dryBuffer.getSample(ch, i) * dry;
    }
  }
}

juce::AudioProcessorEditor *EasyEqProcessor::createEditor() {
  ee::ui::PedalSpec spec;
  spec.name = "Easy EQ";
  spec.tagline = "Seven-band graphic EQ";
  spec.version = "v" JucePlugin_VersionString;

  // Level is make-up gain, not part of the frequency response: a light-grey
  // node (black outline) sets it apart, and the curve skips it.
  spec.sliders.push_back({.parameterID = kLevelID,
                          .caption = "LEVEL",
                          .fill = juce::Colour(0xffd6d6d6),
                          .joinCurve = false});
  for (int i = 0; i < kNumBands; ++i)
    spec.sliders.push_back({.parameterID = kBandIDs[static_cast<size_t>(i)],
                            .caption = kBandCaptions[static_cast<size_t>(i)],
                            .axisHz = kBandFrequencies[static_cast<size_t>(i)]});

  // Top-right utility knobs: low cut (shades the grid from the left as it opens)
  // and high cut (from the right). No captions, value only.
  spec.cornerKnobs = {
      {.parameterID = kLoCutID, .compact = true, .cutSide = ee::ui::CutSide::low},
      {.parameterID = kHiCutID, .compact = true, .cutSide = ee::ui::CutSide::high,
       .invertedArc = true},
  };

  // Same width as Easy Reverb so the two line up on a rack.
  spec.width = 340;

  // Shares Easy Delay's theme so the two pedals sit together on a rack.
  return new ee::ui::PedalEditor(*this, apvts, spec, ee::ui::PedalTheme::silver());
}

void EasyEqProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (auto xml = apvts.copyState().createXml())
    copyXmlToBinary(*xml, destData);
}

void EasyEqProcessor::setStateInformation(const void *data, int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    if (xml->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new EasyEqProcessor();
}
