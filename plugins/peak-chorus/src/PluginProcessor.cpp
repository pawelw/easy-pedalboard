#include "PluginProcessor.h"

#include "ee/dsp/ChorusConfig.h"
#include "ee/ui/PedalEditor.h"

namespace {
constexpr const char *kRateID = "rate";
constexpr const char *kDepthID = "depth";
constexpr const char *kPhaseID = "phase";
constexpr const char *kMixID = "mix";
constexpr const char *kOnID = "on";

constexpr float kRampSeconds = 0.02f;

juce::String percentToText(float value, int) {
  return juce::String(juce::roundToInt(value)) + " %";
}

juce::String hzToText(float value, int) {
  return juce::String(value, value < 1.0f ? 2 : 1) + " Hz";
}

juce::String degreesToText(float value, int) {
  return juce::String(juce::roundToInt(value)) + juce::String::fromUTF8("\xc2\xb0");
}
} // namespace

PeakChorusProcessor::PeakChorusProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  rateParam = apvts.getRawParameterValue(kRateID);
  depthParam = apvts.getRawParameterValue(kDepthID);
  phaseParam = apvts.getRawParameterValue(kPhaseID);
  mixParam = apvts.getRawParameterValue(kMixID);
  onParam = apvts.getRawParameterValue(kOnID);
}

juce::AudioProcessorValueTreeState::ParameterLayout
PeakChorusProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  const auto percent = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);
  const auto percentAttributes =
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percentToText);

  // Rate reads a plain LFO frequency. Skewed so the slow, musical end of the
  // range gets most of the knob travel.
  auto rateRange = juce::NormalisableRange<float>(ee::dsp::config::kRateMinHz,
                                                  ee::dsp::config::kRateMaxHz);
  rateRange.setSkewForCentre(ee::dsp::config::kRateSkewCentreHz);
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kRateID, 1}, "Rate", rateRange,
      ee::dsp::config::kDefaultRateHz,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(hzToText)));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kDepthID, 1}, "Depth", percent,
      ee::dsp::config::kDefaultDepthPct, percentAttributes));

  // Phase is the width control: the L/R LFO offset. 0 = narrow, 90 = quadrature
  // spread, 180 = full counter-motion.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kPhaseID, 1}, "Phase",
      juce::NormalisableRange<float>(0.0f, ee::dsp::config::kMaxPhaseDeg, 1.0f),
      ee::dsp::config::kDefaultPhaseDeg,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(degreesToText)));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kMixID, 1}, "Mix", percent,
      ee::dsp::config::kDefaultMixPct, percentAttributes));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kOnID, 1}, "On", true));

  return layout;
}

void PeakChorusProcessor::prepareToPlay(double newSampleRate,
                                        int maximumExpectedSamplesPerBlock) {
  sampleRate = newSampleRate;

  const int maxBlock = juce::jmax(1, maximumExpectedSamplesPerBlock);

  chorus.prepare(newSampleRate);
  chorus.reset();

  dryBuffer.setSize(kMaxChannels, maxBlock, false, false, true);
  scratchBuffer.setSize(1, maxBlock, false, false, true);

  const bool engaged = onParam->load() > 0.5f;
  wetMix.reset(newSampleRate, kRampSeconds);
  wetMix.setCurrentAndTargetValue(engaged ? 1.0f : 0.0f);
}

void PeakChorusProcessor::releaseResources() {}

bool PeakChorusProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto &in = layouts.getMainInputChannelSet();
  const auto &out = layouts.getMainOutputChannelSet();

  if (in.isDisabled() || out.isDisabled())
    return false;

  const bool inOk = in == juce::AudioChannelSet::mono() ||
                    in == juce::AudioChannelSet::stereo();
  const bool outOk = out == juce::AudioChannelSet::mono() ||
                     out == juce::AudioChannelSet::stereo();

  // A wide chorus needs two channels out; stereo-in / mono-out would also throw
  // half the signal away.
  if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
    return false;

  return inOk && outOk;
}

juce::String PeakChorusProcessor::rateReadout() const {
  return hzToText(rateParam->load(), 0);
}

void PeakChorusProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                       juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();
  const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
  const int numOut = juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels());

  if (numOut == 0 || numSamples == 0)
    return;

  // Clear any output channels the input does not feed, then fan a genuine mono
  // input across them so both sides of the chorus have something to spread.
  for (int ch = numIn; ch < numOut; ++ch)
    buffer.clear(ch, 0, numSamples);
  if (numIn == 1)
    for (int ch = 1; ch < numOut; ++ch)
      buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);

  const int numCh = juce::jmin(numOut, int{kMaxChannels});
  if (numCh == 0)
    return;

  // Untouched dry copy for the bypass crossfade.
  if (numSamples > dryBuffer.getNumSamples())
    dryBuffer.setSize(kMaxChannels, numSamples, false, false, true);
  for (int ch = 0; ch < numCh; ++ch)
    dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

  chorus.setRateHz(rateParam->load());
  chorus.setDepth01(juce::jlimit(0.0f, 1.0f, depthParam->load() * 0.01f));
  chorus.setPhaseDegrees(phaseParam->load());
  chorus.setMix01(juce::jlimit(0.0f, 1.0f, mixParam->load() * 0.01f));

  float *left = buffer.getWritePointer(0);
  float *right = numCh >= 2 ? buffer.getWritePointer(1) : nullptr;

  if (right != nullptr) {
    // In and out alias - the engine reads each input sample before it writes the
    // matching output, so this is safe.
    chorus.process(left, right, left, right, numSamples);
  } else {
    // Mono output: run the engine with a throwaway right channel and keep the
    // left. A mono bus cannot carry the width, but the modulation still colours
    // the signal.
    if (numSamples > scratchBuffer.getNumSamples())
      scratchBuffer.setSize(1, numSamples, false, false, true);
    chorus.process(left, nullptr, left, scratchBuffer.getWritePointer(0), numSamples);
  }

  const bool engaged = onParam->load() > 0.5f;
  wetMix.setTargetValue(engaged ? 1.0f : 0.0f);

  // Crossfade to the untouched dry copy when bypassed, so the host on/off never
  // clicks.
  for (int i = 0; i < numSamples; ++i) {
    const float wet = wetMix.getNextValue();
    const float dry = 1.0f - wet;
    for (int ch = 0; ch < numCh; ++ch) {
      float *outSample = buffer.getWritePointer(ch, i);
      *outSample = *outSample * wet + dryBuffer.getSample(ch, i) * dry;
    }
  }
}

juce::AudioProcessorEditor *PeakChorusProcessor::createEditor() {
  ee::ui::PedalSpec spec;
  spec.name = "Peak Chorus";
  spec.version = "v" JucePlugin_VersionString;

  spec.knobs = {
      {.parameterID = kRateID,
       .caption = "Rate",
       .liveValueText = [this] { return rateReadout(); }},
      {kDepthID, "Depth"},
      {kPhaseID, "Phase"},
      {kMixID, "Mix"},
  };

  // Two rows of two, same footprint as Peak Reverb.
  spec.knobsPerRow = 2;
  spec.width = ee::ui::knobRowWidth(spec.knobsPerRow);

  return new ee::ui::PedalEditor(*this, apvts, spec, ee::ui::PedalTheme::sky());
}

void PeakChorusProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (auto xml = apvts.copyState().createXml())
    copyXmlToBinary(*xml, destData);
}

void PeakChorusProcessor::setStateInformation(const void *data, int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    if (xml->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new PeakChorusProcessor();
}
