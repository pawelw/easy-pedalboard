#include "PluginProcessor.h"

#include "ee/ui/PedalEditor.h"

namespace {
constexpr const char *kLevelID = "level";
constexpr const char *kToneID = "tone";
constexpr const char *kDriveID = "drive";
constexpr const char *kOnID = "on";

constexpr float kRampSeconds = 0.02f;

// Output knob travel. The engine already roughly loudness-compensates Drive, so
// this is a trim: unity at noon, a little boost on tap for slamming the amp in
// front, and enough cut to tuck the pedal under a clean sound.
constexpr float kLevelMinDb = -30.0f;
constexpr float kLevelMaxDb = 6.0f;

juce::String percentToText(float value, int) {
  return juce::String(juce::roundToInt(value)) + " %";
}

juce::String decibelsToText(float value, int) {
  return juce::String(value, 1) + " dB";
}
} // namespace

PeakOverdriveProcessor::PeakOverdriveProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  levelParam = apvts.getRawParameterValue(kLevelID);
  toneParam = apvts.getRawParameterValue(kToneID);
  driveParam = apvts.getRawParameterValue(kDriveID);
  onParam = apvts.getRawParameterValue(kOnID);
}

juce::AudioProcessorValueTreeState::ParameterLayout
PeakOverdriveProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  const auto percent = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);
  const auto percentAttributes =
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percentToText);

  // Level first, to match a Boss OD's face order (Level, Tone, Drive).
  auto levelRange = juce::NormalisableRange<float>(kLevelMinDb, kLevelMaxDb, 0.1f);
  levelRange.setSkewForCentre(-6.0f);
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kLevelID, 1}, "Level", levelRange, 0.0f,
      juce::AudioParameterFloatAttributes().withStringFromValueFunction(decibelsToText)));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kToneID, 1}, "Tone", percent,
      ee::dsp::config::kDefaultTone01 * 100.0f, percentAttributes));

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kDriveID, 1}, "Drive", percent,
      ee::dsp::config::kDefaultDrive01 * 100.0f, percentAttributes));

  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{kOnID, 1}, "On", true));

  return layout;
}

void PeakOverdriveProcessor::prepareToPlay(double newSampleRate,
                                           int maximumExpectedSamplesPerBlock) {
  sampleRate = newSampleRate;

  const int maxBlock = juce::jmax(1, maximumExpectedSamplesPerBlock);

  overdrive.prepare(newSampleRate);
  overdrive.reset();

  dryBuffer.setSize(kMaxChannels, maxBlock, false, false, true);

  levelGain.reset(newSampleRate, kRampSeconds);
  levelGain.setCurrentAndTargetValue(
      juce::Decibels::decibelsToGain(levelParam->load()));

  const bool engaged = onParam->load() > 0.5f;
  wetMix.reset(newSampleRate, kRampSeconds);
  wetMix.setCurrentAndTargetValue(engaged ? 1.0f : 0.0f);
}

void PeakOverdriveProcessor::releaseResources() {}

bool PeakOverdriveProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto &in = layouts.getMainInputChannelSet();
  const auto &out = layouts.getMainOutputChannelSet();

  if (in.isDisabled() || out.isDisabled() || in != out)
    return false;

  return in == juce::AudioChannelSet::mono() ||
         in == juce::AudioChannelSet::stereo();
}

void PeakOverdriveProcessor::processBlock(juce::AudioBuffer<float> &buffer,
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

  // Untouched dry copy for the bypass crossfade.
  if (numSamples > dryBuffer.getNumSamples())
    dryBuffer.setSize(kMaxChannels, numSamples, false, false, true);
  for (int ch = 0; ch < numCh; ++ch)
    dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

  overdrive.setDrive01(juce::jlimit(0.0f, 1.0f, driveParam->load() * 0.01f));
  overdrive.setTone01(juce::jlimit(0.0f, 1.0f, toneParam->load() * 0.01f));

  float *left = buffer.getWritePointer(0);
  float *right = numCh >= 2 ? buffer.getWritePointer(1) : nullptr;
  overdrive.process(left, right, numSamples);

  const bool engaged = onParam->load() > 0.5f;
  levelGain.setTargetValue(juce::Decibels::decibelsToGain(levelParam->load()));
  wetMix.setTargetValue(engaged ? 1.0f : 0.0f);

  // Output level, then a crossfade to the untouched dry copy when bypassed so
  // the host on/off never clicks.
  for (int i = 0; i < numSamples; ++i) {
    const float g = levelGain.getNextValue();
    const float wet = wetMix.getNextValue();
    const float dry = 1.0f - wet;
    for (int ch = 0; ch < numCh; ++ch) {
      float *outSample = buffer.getWritePointer(ch, i);
      *outSample = (*outSample * g) * wet + dryBuffer.getSample(ch, i) * dry;
    }
  }
}

juce::AudioProcessorEditor *PeakOverdriveProcessor::createEditor() {
  ee::ui::PedalSpec spec;
  spec.name = "Peak Overdrive";
  spec.version = "v" JucePlugin_VersionString;

  // Level and Drive on top, Tone centred in a row of its own below - the small
  // footprint of Peak Reverb (two knob columns), not a wide three-across face.
  spec.knobs = {
      {kLevelID, "Level"},
      {kDriveID, "Drive"},
      {kToneID, "Tone"},
  };

  spec.knobsPerRow = 2;
  spec.width = ee::ui::knobRowWidth(spec.knobsPerRow);

  return new ee::ui::PedalEditor(*this, apvts, spec, ee::ui::PedalTheme::yellow());
}

void PeakOverdriveProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (auto xml = apvts.copyState().createXml())
    copyXmlToBinary(*xml, destData);
}

void PeakOverdriveProcessor::setStateInformation(const void *data,
                                                 int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    if (xml->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new PeakOverdriveProcessor();
}
