#include "PluginProcessor.h"

#include "ee/dsp/PhaserConfig.h"
#include "ee/ui/PedalEditor.h"

namespace {
constexpr const char *kRateID = "rate";
constexpr const char *kDepthID = "depth";
constexpr const char *kOnID = "on";

constexpr float kRampSeconds = 0.02f;

juce::String percentToText(float value, int) {
  return juce::String(juce::roundToInt(value)) + " %";
}

juce::String hzToText(float value, int) {
  return juce::String(value, value < 1.0f ? 2 : 1) + " Hz";
}
} // namespace

PeakPhaseProcessor::PeakPhaseProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  rateParam = apvts.getRawParameterValue(kRateID);
  depthParam = apvts.getRawParameterValue(kDepthID);
  onParam = apvts.getRawParameterValue(kOnID);
}

juce::AudioProcessorValueTreeState::ParameterLayout
PeakPhaseProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  const auto percent = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);
  const auto percentAttributes =
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percentToText);

  // Rate is a plain LFO frequency, skewed so the slow, musical end of the
  // range gets most of the knob travel.
  auto rateRange = juce::NormalisableRange<float>(ee::dsp::phaser::kRateMinHz,
                                                  ee::dsp::phaser::kRateMaxHz);
  rateRange.setSkewForCentre(ee::dsp::phaser::kRateSkewCentreHz);
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kRateID, 1}, "Rate", rateRange,
      ee::dsp::phaser::kDefaultRateHz,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(hzToText)));

  // Depth widens the frequency sweep about its geometric centre. At 0 the
  // notches sit still mid-spectrum and simply colour the tone.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kDepthID, 1}, "Depth", percent,
      ee::dsp::phaser::kDefaultDepthPct, percentAttributes));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kOnID, 1}, "On", true));

  return layout;
}

void PeakPhaseProcessor::prepareToPlay(double newSampleRate,
                                       int maximumExpectedSamplesPerBlock) {
  sampleRate = newSampleRate;

  const int maxBlock = juce::jmax(1, maximumExpectedSamplesPerBlock);

  phaser.prepare(newSampleRate);
  phaser.reset();

  dryBuffer.setSize(kMaxChannels, maxBlock, false, false, true);
  scratchBuffer.setSize(1, maxBlock, false, false, true);

  const bool engaged = onParam->load() > 0.5f;
  wetMix.reset(newSampleRate, kRampSeconds);
  wetMix.setCurrentAndTargetValue(engaged ? 1.0f : 0.0f);
}

void PeakPhaseProcessor::releaseResources() {}

bool PeakPhaseProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto &in = layouts.getMainInputChannelSet();
  const auto &out = layouts.getMainOutputChannelSet();

  if (in.isDisabled() || out.isDisabled())
    return false;

  const bool inOk = in == juce::AudioChannelSet::mono() ||
                    in == juce::AudioChannelSet::stereo();
  const bool outOk = out == juce::AudioChannelSet::mono() ||
                     out == juce::AudioChannelSet::stereo();

  // A wide phaser needs two channels out; stereo-in / mono-out would also throw
  // half the signal away.
  if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
    return false;

  return inOk && outOk;
}

juce::String PeakPhaseProcessor::rateReadout() const {
  return hzToText(rateParam->load(), 0);
}

void PeakPhaseProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                      juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();
  const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
  const int numOut = juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels());

  if (numOut == 0 || numSamples == 0)
    return;

  // Clear any output channels the input does not feed, then fan a genuine mono
  // input across them so both sides of the phaser have something to spread.
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

  phaser.setRateHz(rateParam->load());
  phaser.setDepth01(juce::jlimit(0.0f, 1.0f, depthParam->load() * 0.01f));

  float *left = buffer.getWritePointer(0);
  float *right = numCh >= 2 ? buffer.getWritePointer(1) : nullptr;

  if (right != nullptr) {
    // In and out alias - the engine reads each input sample before it writes the
    // matching output, so this is safe.
    phaser.process(left, right, left, right, numSamples);
  } else {
    // Mono output: run the engine with a throwaway right channel and keep the
    // left. A mono bus cannot carry the width, but the sweep still colours the
    // signal.
    if (numSamples > scratchBuffer.getNumSamples())
      scratchBuffer.setSize(1, numSamples, false, false, true);
    phaser.process(left, nullptr, left, scratchBuffer.getWritePointer(0), numSamples);
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

juce::AudioProcessorEditor *PeakPhaseProcessor::createEditor() {
  ee::ui::PedalSpec spec;
  spec.name = "Peak Phase";
  spec.version = "v" JucePlugin_VersionString;

  spec.knobs = {
      {.parameterID = kRateID,
       .caption = "Rate",
       .liveValueText = [this] { return rateReadout(); }},
      {kDepthID, "Depth"},
  };

  // One knob per row - a narrow two-control face.
  spec.knobsPerRow = 1;
  spec.width = ee::ui::knobRowWidth(spec.knobsPerRow);

  return new ee::ui::PedalEditor(*this, apvts, spec, ee::ui::PedalTheme::orange());
}

void PeakPhaseProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (auto xml = apvts.copyState().createXml())
    copyXmlToBinary(*xml, destData);
}

void PeakPhaseProcessor::setStateInformation(const void *data, int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    if (xml->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new PeakPhaseProcessor();
}
