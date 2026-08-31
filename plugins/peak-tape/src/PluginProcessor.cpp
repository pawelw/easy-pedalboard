#include "PluginProcessor.h"

#include "ee/dsp/TapeMachineConfig.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

#include "TapeAssets.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace
{
using ee::plugin::kRampSeconds;
using ee::plugin::percentToText;

constexpr const char* kWearID = "wear";
constexpr const char* kFlutterID = "flutter";
constexpr const char* kToneID = "tone";
constexpr const char* kStereoID = "stereo";
constexpr const char* kNoiseID = "noise";
constexpr const char* kSaturationID = "sat";
constexpr const char* kOnID = "on";

/** Tone rests in the middle, and reads 0 there: a bipolar control should
        print the number it is on, with the sign carrying the direction. */
juce::String toneToText (float value, int)
{
    const int rounded = juce::roundToInt (value);

    if (rounded == 0)
        return "0 %";

    return (rounded > 0 ? "+" : "") + juce::String (rounded) + " %";
}
} // namespace

PeakTapeProcessor::PeakTapeProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    wearParam = apvts.getRawParameterValue (kWearID);
    flutterParam = apvts.getRawParameterValue (kFlutterID);
    toneParam = apvts.getRawParameterValue (kToneID);
    stereoParam = apvts.getRawParameterValue (kStereoID);
    noiseParam = apvts.getRawParameterValue (kNoiseID);
    saturationParam = apvts.getRawParameterValue (kSaturationID);
    onParam = apvts.getRawParameterValue (kOnID);

    loadNoiseSample();
}

/** Decodes the embedded tape floor once, at construction. The engine loops it
    from these samples, so the buffer has to outlive every process call - it is a
    member, and the pointers handed over are into it. */
void PeakTapeProcessor::loadNoiseSample()
{
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::MemoryInputStream> (
        TapeAssets::tapenoise_wav, static_cast<size_t> (TapeAssets::tapenoise_wavSize), false);

    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return; // no recording: the engine falls back to synthesised hiss

    const int numSamples = static_cast<int> (juce::jmin (reader->lengthInSamples, juce::int64 (10 * 60 * 44100)));
    const int numChannels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    noiseSample.setSize (numChannels, numSamples);
    reader->read (&noiseSample, 0, numSamples, 0, true, numChannels > 1);
    noiseSampleRate = reader->sampleRate;

    noiseChannels.resize (static_cast<size_t> (numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        noiseChannels[static_cast<size_t> (ch)] = noiseSample.getReadPointer (ch);

    machine.setNoiseSample (noiseChannels.data(), numChannels, numSamples, noiseSampleRate);
}

juce::AudioProcessorValueTreeState::ParameterLayout PeakTapeProcessor::createParameterLayout()
{
    namespace tape = ee::dsp::tape;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto percentAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

    // How tired the tape is. This is Peak Delay's Tape stage - the same engine
    // and the same voicing - on its own knob.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kWearID, 1 }, "Wear", percent,
                                                             tape::kDefaultWearPct, percentAttributes));

    // Depth of the wow, flutter and scrape riding the transport.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kFlutterID, 1 }, "Flutter", percent,
                                                             tape::kDefaultFlutterPct, percentAttributes));

    // Tilt around a fixed pivot, on a knob that rests in the middle: left is
    // dark, right is bright, dead centre is flat and bypasses the stage.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kToneID, 1 }, "Tone", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        tape::kDefaultTonePct, juce::AudioParameterFloatAttributes().withStringFromValueFunction (toneToText)));

    // Width, as a switch rather than a knob: mono is one transport under both
    // channels, stereo opens them onto different points of a slow modulation.
    // On by default - the machine is a stereo one unless you ask otherwise.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kStereoID, 1 }, "Stereo",
                                                            tape::kDefaultStereoOn));

    // The tape floor: a recording of a real one, looped, there whether anything
    // is playing or not. Not gated, and it does not ride the programme. The knob
    // is a straight gain on it, so 100 % is the recording as it was made.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kNoiseID, 1 }, "Noise", percent,
                                                             tape::kDefaultNoisePct, percentAttributes));

    // How hard the record head is driven.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kSaturationID, 1 }, "Saturation",
                                                             percent, tape::kDefaultSaturationPct, percentAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kOnID, 1 }, "On", true));

    return layout;
}

void PeakTapeProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    machine.prepare (sampleRate);

    if (! noiseChannels.empty())
        machine.setNoiseSample (noiseChannels.data(), static_cast<int> (noiseChannels.size()),
                                noiseSample.getNumSamples(), noiseSampleRate);

    machine.reset();

    // The transport reads off a delay line, so the machine is always this far
    // behind - constant whatever the knobs do, so the host can compensate it.
    setLatencySamples (machine.getLatencySamples());

    dryBuffer.setSize (kMaxChannels, maxBlock, false, false, true);

    const bool engaged = onParam->load() > 0.5f;
    wetMix.reset (sampleRate, kRampSeconds);
    wetMix.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void PeakTapeProcessor::releaseResources()
{
    machine.reset();
}

bool PeakTapeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // Stereo in / mono out would throw half the signal away.
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono())
        return false;

    return inOk && outOk;
}

void PeakTapeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numOut == 0 || numSamples == 0)
        return;

    // Clear any output channels the input does not feed, then fan a genuine mono
    // input across them so both sides come off the same tape.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numIn == 1)
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    const int numCh = juce::jmin (numOut, int { kMaxChannels });

    // Untouched dry copy for the bypass crossfade.
    if (numSamples > dryBuffer.getNumSamples())
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    machine.setWear01 (wearParam->load() * 0.01f);
    machine.setFlutter01 (flutterParam->load() * 0.01f);
    machine.setTone (toneParam->load() * 0.01f);
    machine.setStereo01 (stereoParam->load() > 0.5f ? 1.0f : 0.0f);
    machine.setNoise01 (noiseParam->load() * 0.01f);
    machine.setSaturation01 (saturationParam->load() * 0.01f);

    float* left = buffer.getWritePointer (0);
    float* right = numCh >= 2 ? buffer.getWritePointer (1) : nullptr;
    machine.process (left, right, numSamples);

    const bool engaged = onParam->load() > 0.5f;
    wetMix.setTargetValue (engaged ? 1.0f : 0.0f);

    // Crossfade to the untouched dry copy when bypassed, so the host on/off
    // never clicks. The machine keeps running underneath either way, so coming
    // back on does not restart the transport mid-wobble.
    ee::plugin::crossfadeToDry (buffer, dryBuffer, wetMix, numCh, numSamples);
}

juce::AudioProcessorEditor* PeakTapeProcessor::createEditor()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Tape";
    spec.tagline = "Analogue warmth, wobble and wear";
    spec.version = "v" JucePlugin_VersionString;

    // Four character knobs at the corners of the block, with Tone in the middle
    // of them. Two columns rather than three: the middle one only ever held
    // Tone, and carrying an empty column just pushed the knobs away from the
    // edges - so the face is a whole column narrower than Peak Delay's.
    spec.knobs = {
        { kSaturationID, "Saturation" }, { kFlutterID, "Flutter" }, { kWearID, "Wear" }, { kNoiseID, "Noise" }
    };

    // Tone gets Peak Reverb's RESO treatment: a small vector cap dead centre of
    // the block with its caption alone under it, rather than a cell of its own.
    // Its arc still grows out of 12 o'clock either way, the centre carries a
    // detent tick, and the knob snaps onto it.
    spec.centreKnob = ee::ui::KnobSpec { .parameterID = kToneID,
                                         .caption = "Tone",
                                         .compact = true,
                                         .compactCaption = true,
                                         .bipolarArc = true,
                                         .centreDetent = true };

    // Width is one thing or the other, so it is the big sliding switch Peak
    // Trem & Pan uses for its mode - centred in the strip above the knobs,
    // since it is the only thing in it and Tone sits under it on the same axis.
    spec.slideToggle = ee::ui::SlideToggleSpec { .parameterID = kStereoID, .labelOff = "Mono", .labelOn = "Stereo" };
    spec.slideToggleCentred = true;
    spec.slideToggleRise = 8; // tighter to the top edge than the shared strip

    // The shared row gap: a small centre cap needs no more room between the rows
    // than Peak Reverb's RESO does, which leaves the height for the caps.
    spec.knobRowGap = 12;

    // Just under the shared cap size. The switch strip costs this face 40 px
    // that Peak Reverb's two rows do not pay, and the block has to fit in what
    // is left - 278 px, against 2 x (cap + 32 px of labels) plus the gap.
    spec.knobDiameter = 100;

    spec.knobsPerRow = 2;

    // 6 % wider than the shared two-column width. The columns take the extra,
    // so the four knobs move out from the middle and Tone gets clear air around
    // it - the only reason this face departs from knobRowWidth().
    spec.width = (ee::ui::knobRowWidth (spec.knobsPerRow) * 106) / 100;

    // Sit the block higher than centring would, so the top row comes up under
    // the switch and the bottom one lifts off the pedal name.
    spec.knobBlockRise = 12;

    // The columns are a good deal wider than the caps, so the four knobs sit out
    // near the edges rather than floating in the middle of their cells.
    spec.knobColumnSpread = 10;

    // A small reel above the name, centred in the gap the knobs leave.
    spec.titleImage = juce::ImageCache::getFromMemory (TapeAssets::tape_png, TapeAssets::tape_pngSize);
    spec.titleImageHeight = 60;
    spec.titleImageTint = juce::Colours::white;

    return new ee::ui::PedalEditor (*this, apvts, spec, ee::ui::PedalTheme::green());
}

void PeakTapeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PeakTapeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PeakTapeProcessor();
}
