#include "PluginProcessor.h"

#include "Params.h"
#include "BitBitArtifactWebEditor.h"

#include "ee/dsp/BitCrusherConfig.h"
#include "ee/dsp/RingModulatorConfig.h"
#include "ee/dsp/RustConfig.h"
#include "ee/dsp/TubeDriveConfig.h"
#include "ee/plugin/ParamText.h"
#include "ee/plugin/StateVersion.h"

#include <cmath>

namespace
{
using namespace ee::artifact;
using ee::plugin::percentToText;

/** "480 Hz" under 1 kHz, "2.4 kHz" above - for the Bit Crush readouts, which
    are this file's own (the shared hzToText does not fold to kHz). */
juce::String freqText (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 1) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

// The Bit Crush knobs print real units off the same ee::dsp::bitcrush maps the
// engine reads. Rate is resolved at a nominal 48 kHz for the host-facing text -
// like BitBit Delay's synced-at-reference-tempo readout, a fixed stand-in for a
// rate the plugin cannot know here.
constexpr float kCrushRateTextSr = 48000.0f;

juce::String crushBitsToText (float pct, int)
{
    return juce::String (juce::roundToInt (ee::dsp::bitcrush::bitsFor (pct * 0.01f))) + " bit";
}

juce::String crushRateToText (float pct, int)
{
    const int n = ee::dsp::bitcrush::decimationFactorFor (pct * 0.01f);
    if (n <= 1)
        return "Off";
    return freqText (kCrushRateTextSr / static_cast<float> (n));
}

juce::String crushLpToText (float pct, int)
{
    const float hz = ee::dsp::bitcrush::lpHzFor (pct * 0.01f);
    if (hz >= ee::dsp::bitcrush::kLpBypassHz)
        return "Off";
    return freqText (hz);
}

// Amp's Bit readout - the sample-and-hold rate, off
// ee::fx::ArtifactModule::ampRateHzFor (not ee::dsp::bitcrush's map - a
// different curve and a different floor, see ArtifactModule.h's kAmpCrush*
// constants). Resolved at the same nominal 48 kHz the Bit Crush Rate text is.
juce::String ampBitToText (float pct, int)
{
    const float hz = ee::fx::ArtifactModule::ampRateHzFor (pct * 0.01f, kCrushRateTextSr);
    const int n = juce::jmax (1, juce::roundToInt (kCrushRateTextSr / hz));
    if (n <= 1)
        return "Off";
    return freqText (kCrushRateTextSr / static_cast<float> (n));
}

/** A bipolar percent that rests in the middle and reads 0 there, with the sign
    carrying the direction. Amp's Tone and Ring Mod's Rectify both print this
    way. Kept in step with plugins/bitbit-tape's own toneToText. */
juce::String signedPctToText (float value, int)
{
    const int rounded = juce::roundToInt (value);

    if (rounded == 0)
        return "0 %";

    return (rounded > 0 ? "+" : "") + juce::String (rounded) + " %";
}

// Ring Mod readouts, off the same ee::dsp::ringmod maps ee::fx::ArtifactModule
// reads. Freq folds to kHz like the Bit Crush text; Filter prints "Off" once
// the knob is past the bypass point.
juce::String ringFreqToText (float pct, int)
{
    return freqText (ee::dsp::ringmod::freqHzFor (pct * 0.01f));
}

juce::String ringLpToText (float pct, int)
{
    const float hz = ee::dsp::ringmod::lpHzFor (pct * 0.01f);
    if (hz >= ee::dsp::ringmod::kLpBypassHz)
        return "Off";
    return freqText (hz);
}

// Rust's Tone readout, off the same ee::dsp::rust map ee::fx::ArtifactModule
// reads. Grind stays a plain percent. The number here is the Oxide reading; in
// Contact the engine scales the knob down before the map, so the effective
// corner is lower than shown - noted on the face rather than folded in here.
juce::String rustToneToText (float pct, int)
{
    const float hz = ee::dsp::rust::toneHzFor (pct * 0.01f);
    if (hz >= ee::dsp::rust::kToneBypassHz)
        return "Off";
    return freqText (hz);
}

using Attributes = juce::AudioParameterFloatAttributes;

Attributes withText (juce::String (*fn) (float, int))
{
    return Attributes().withStringFromValueFunction (fn);
}
} // namespace

BitBitArtifactProcessor::BitBitArtifactProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    presets.installState = [this] (const juce::ValueTree& tree) { installState (tree); };
}

BitBitArtifactProcessor::~BitBitArtifactProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout BitBitArtifactProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
    const auto pctAttr = Attributes().withStringFromValueFunction (percentToText);

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::on, 1 }, "On", true));

    // All four engines are voiced; the pedal opens on Ring Mod. Filter used to
    // be the third, before it moved to BitBit Alpine's Modulation module.
    layout.add (
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::engine, 1 }, "Engine",
                                                      juce::StringArray { "Ring Mod", "Bit Crush", "Rust", "Amp" }, 0));

    layout.add (
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::mix, 1 }, "Mix", percent, 50.0f, pctAttr));

    // Bit Crush. Knobs are 0..100; the text functions and ee::fx::ArtifactModule
    // both resolve them through the ee::dsp::bitcrush maps.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::crushBits, 1 }, "Bits", percent,
                                                             ee::dsp::bitcrush::kDefaultBitsPct,
                                                             withText (crushBitsToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::crushRate, 1 }, "Rate", percent,
                                                             ee::dsp::bitcrush::kDefaultRatePct,
                                                             withText (crushRateToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::crushLp, 1 }, "Filter", percent,
                                                             ee::dsp::bitcrush::kDefaultLpPct,
                                                             withText (crushLpToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::crushJitter, 1 }, "Jitter",
                                                             percent, ee::dsp::bitcrush::kDefaultJitterPct, pctAttr));

    // Ring Mod. Freq and Filter print real units off the ee::dsp::ringmod maps;
    // Tweak is a plain percent (its meaning - carrier wobble or octave blend -
    // is set by Mode). Rectify is bipolar and rests dead centre, doing nothing
    // there. Blend is the footer Mix, not a knob here.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::ringFreq, 1 }, "Ring Freq",
                                                             percent, ee::dsp::ringmod::kDefaultFreqPct,
                                                             withText (ringFreqToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::ringTweak, 1 }, "Tweak", percent,
                                                             ee::dsp::ringmod::kDefaultTweakPct, pctAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::ringLp, 1 }, "Ring Filter",
                                                             percent, ee::dsp::ringmod::kDefaultLpPct,
                                                             withText (ringLpToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::ringRect, 1 }, "Rectify", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        ee::dsp::ringmod::kDefaultRectifyPct, withText (signedPctToText)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::ringMode, 1 }, "Mode",
                                                              juce::StringArray { "Wobble", "Octave" }, 0));

    // Rust. Two knobs only - Grind (a plain percent) and Tone (real units off
    // the ee::dsp::rust map). Wear and its recovery are fixed inside the engine.
    // Its dry/wet is the footer Mix, not a knob here.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::rustGrind, 1 }, "Grind", percent,
                                                             ee::dsp::rust::kDefaultGrindPct, pctAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::rustTone, 1 }, "Tone", percent,
                                                             ee::dsp::rust::kDefaultTonePct,
                                                             withText (rustToneToText)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::rustMode, 1 }, "Rust Mode",
                                                              juce::StringArray { "Oxide", "Contact" }, 0));

    // Amp. A drive matched to a real reference unit at 100 % (its own knob,
    // plain percent - see ee/dsp/TubeDriveConfig.h) into a sample-rate reducer (Bit - see
    // ee::fx::ArtifactModule::ampRateHzFor) with no bit-depth quantisation and
    // no anti-alias filter, a Mids peaking boost (BitBit EQ's own Mid band/Q,
    // 0..7 dB) and a bipolar Tone tilt (BitBit Tape's, flat and bypassed dead
    // centre). Stereo switches a fixed Haas delay on the right channel on or
    // off. Its dry/wet is the footer Mix.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::ampDrive, 1 }, "Amp Drive",
                                                             percent, ee::dsp::tubedrive::kDefaultDrivePct, pctAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::ampMids, 1 }, "Amp Mids", percent,
                                                             0.0f, pctAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::ampBit, 1 }, "Amp Bit", percent,
                                                             ee::fx::ArtifactModule::kAmpDefaultBitPct,
                                                             withText (ampBitToText)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::ampTone, 1 }, "Amp Tone",
                                                             juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
                                                             0.0f, withText (signedPctToText)));
    layout.add (
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::ampStereo, 1 }, "Amp Stereo", false));

    return layout;
}

void BitBitArtifactProcessor::pushSettings() noexcept
{
    const auto raw = [this] (const char* pid) { return apvts.getRawParameterValue (pid)->load(); };
    const auto pct = [&raw] (const char* pid) { return raw (pid) * 0.01f; };
    const auto flag = [&raw] (const char* pid) { return raw (pid) > 0.5f; };

    module.setEngine (static_cast<int> (raw (id::engine)));
    module.setEngaged (flag (id::on));
    module.setMix01 (pct (id::mix));

    module.setCrush (pct (id::crushBits), pct (id::crushRate), pct (id::crushLp), pct (id::crushJitter));

    module.setRing (pct (id::ringFreq), pct (id::ringTweak), pct (id::ringLp), raw (id::ringRect) * 0.01f,
                    static_cast<int> (raw (id::ringMode)));

    module.setRust (pct (id::rustGrind), pct (id::rustTone), static_cast<int> (raw (id::rustMode)));

    module.setAmp (pct (id::ampDrive), pct (id::ampMids), pct (id::ampBit), raw (id::ampTone) * 0.01f,
                   flag (id::ampStereo));
}

void BitBitArtifactProcessor::installState (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
}

// ---------------------------------------------------------------------- audio

void BitBitArtifactProcessor::prepareToPlay (double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const int maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

    pushSettings();
    module.prepare (sampleRate, maxBlock);
}

void BitBitArtifactProcessor::releaseResources()
{
    module.reset();
}

bool BitBitArtifactProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void BitBitArtifactProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    pushSettings();

    module.process (buffer, numCh, numSamples);
}

juce::AudioProcessorEditor* BitBitArtifactProcessor::createEditor()
{
    return new BitBitArtifactWebEditor (*this);
}

void BitBitArtifactProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = ee::plugin::copyVersionedState (apvts).createXml())
        copyXmlToBinary (*xml, destData);
}

void BitBitArtifactProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = ee::plugin::xmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            installState (ee::plugin::sanitisedState (juce::ValueTree::fromXml (*xml), apvts));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitBitArtifactProcessor();
}
