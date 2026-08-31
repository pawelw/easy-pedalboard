#include "PluginProcessor.h"

#include "RateMap.h"

#include "ee/dsp/Lfo.h"
#include "ee/plugin/Bypass.h"
#include "ee/plugin/ParamText.h"
#include "ee/ui/PedalEditor.h"

namespace
{
using ee::plugin::kRampSeconds;
using ee::plugin::percentToText;

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

// Bias-tube tremolo. An opto/photocell tremolo just fades the level with a
// smooth LFO; a brownface-style bias tremolo modulates a power tube's bias,
// which does two audible things the clean fade does not:
//
//   1. the ducking envelope stops being a mirror of the LFO - the tube snaps
//      toward cutoff and lingers there, so the throb reads as a harder,
//      flatter-bottomed pulse (kBiasDuckSkew bends the duck curve for this);
//   2. as the operating point nears cutoff the signal grinds - an asymmetric,
//      level-dependent distortion that swells and clears in time with the
//      throb, clean on the loud peaks and dirtiest at the bottom of the dip.
//
// Modelled the same way as ee::dsp::TapeCharacter's stage: a hand-rolled tanh
// with a one-sided bias, no oversampling (the drive is program-dependent and
// mostly gentle), and a one-pole DC blocker to mop up the offset the moving
// bias leaves. The Bias knob crossfades the whole thing in; at 0 the clean
// opto law is untouched.
constexpr float kBiasDuckSkew = 0.6f; // how far the duck curve bends toward a hard pulse
constexpr float kBiasDrive = 10.0f;   // peak extra drive into the tanh at the bottom of the dip
constexpr float kBiasAsym = 0.7f;     // one-sided bias offset - the pulsing even harmonics
constexpr float kBiasTrim = 5.0f;     // output trim that tracks the drive, leaving a little sag
constexpr float kBiasDcHz = 20.0f;    // DC-blocker corner: below the lowest note, above the LFO's pump

// Gain that keeps the perceived level roughly constant as the tremolo depth
// comes up. The tremolo law pins its peak at unity and only ever ducks, so the
// pedal always sounds quieter when engaged. For a symmetric LFO the applied gain
// sweeps linearly over [1 - d, 1], whose mean-square is (1 - d + d^2/3); the
// reciprocal square root of that restores the RMS level. Bounded: ~+2.3 dB at
// 50 %, ~+4.8 dB at full depth. Non-symmetric shapes (exp decay, ramp) lose a
// touch more, so this slightly under-compensates them - deliberately, to keep
// the wet path from ever out-running the dry transients.
float tremoloMakeupGain (float depth01)
{
    const float d = juce::jlimit (0.0f, 1.0f, depth01);
    return 1.0f / std::sqrt (1.0f - d + d * d / 3.0f);
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

    lfoPhase = 0.0;
    expectedPpq = 0.0;
    haveExpectedPpq = false;
    wasPlaying = false;

    modZ1 = 0.0f;
    // ~2.5 ms one-pole.
    modSlewCoeff = 1.0f - std::exp (-1.0f / (0.0025f * static_cast<float> (newSampleRate)));

    for (auto& s : biasDcState)
        s = 0.0f;
    biasDcCoeff = juce::jlimit (
        0.0f, 1.0f,
        1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * kBiasDcHz / static_cast<float> (newSampleRate)));

    dryBuffer.setSize (kMaxChannels, maxBlock, false, false, true);
    modBuffer.assign (static_cast<size_t> (maxBlock), 0.0f);

    const bool engaged = onParam->load() > 0.5f;

    depth.reset (newSampleRate, kRampSeconds);
    depth.setCurrentAndTargetValue (amountParam->load() * 0.01f);

    makeup.reset (newSampleRate, kRampSeconds);
    makeup.setCurrentAndTargetValue (tremoloMakeupGain (amountParam->load() * 0.01f));

    bias.reset (newSampleRate, kRampSeconds);
    bias.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, biasParam->load() * 0.01f));

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

    const float amount01 = juce::jlimit (0.0f, 1.0f, amountParam->load() * 0.01f);
    const float shape01 = juce::jlimit (0.0f, 1.0f, shapeParam->load() * 0.01f);
    const float bias01 = juce::jlimit (0.0f, 1.0f, biasParam->load() * 0.01f);
    const float rate01 = rateParam->load();
    const bool panning = modeParam->load() > 0.5f;
    const bool synced = syncParam->load() > 0.5f;
    const bool engaged = onParam->load() > 0.5f;

    const float periodSeconds = juce::jmax (1.0e-4f, ee::trempan::rateToPeriodSeconds (rate01, synced, bpm));
    double phaseInc = 1.0 / (static_cast<double> (periodSeconds) * sampleRate);
    // A non-finite or negative increment would spin the wrap below forever and
    // wedge the audio thread - the roar you cannot turn down. Anything past a
    // full cycle per sample is already meaningless, so clamp hard.
    if (! std::isfinite (phaseInc) || phaseInc < 0.0)
        phaseInc = 0.0;
    phaseInc = juce::jmin (phaseInc, 1.0);

    // The LFO always free-runs on lfoPhase, so a rate or division change never
    // steps the phase - it just carries on at a new speed. When synced to a
    // running transport we also align it to the host grid: a hard snap only on the
    // first playing block or a transport jump (loop / relocate); otherwise a
    // gentle per-block pull, capped small, so host ppq jitter and division changes
    // stay click-free and just re-settle over a fraction of a second.
    if (synced && havePpq && isPlaying)
    {
        const double cyclesPerQuarter =
            1.0 / juce::jmax (1.0e-4, static_cast<double> (ee::trempan::syncedDivisionBeats (rate01)));
        const double ppqPerSample = bpm / (60.0 * sampleRate);

        double target = ppqStart * cyclesPerQuarter;
        target -= std::floor (target);

        const bool jumped = ! wasPlaying || (haveExpectedPpq && std::abs (ppqStart - expectedPpq) > 0.25);

        if (jumped)
        {
            lfoPhase = target;
        }
        else
        {
            double err = target - lfoPhase;
            err -= std::round (err); // wrap to [-0.5, 0.5]
            lfoPhase += juce::jlimit (-0.006, 0.006, 0.15 * err);
        }

        expectedPpq = ppqStart + numSamples * ppqPerSample;
        haveExpectedPpq = true;
    }
    else
    {
        haveExpectedPpq = false; // next playing block re-aligns from scratch
    }
    wasPlaying = isPlaying;

    // Self-heal if a bad host value ever slipped a non-finite into the state -
    // otherwise a single NaN here would stick and roar. Every running state
    // variable that feeds the next block has to be covered: the bias-tube DC
    // blocker latches just as hard as the LFO phase does.
    if (! std::isfinite (lfoPhase))
        lfoPhase = 0.0;
    if (! std::isfinite (modZ1))
        modZ1 = 0.0f;
    for (auto& s : biasDcState)
        if (! std::isfinite (s))
            s = 0.0f;

    depth.setTargetValue (amount01);
    // Only the tremolo law ducks; the panning branch is already equal-power, so
    // it needs no make-up and its target stays at unity.
    makeup.setTargetValue (panning ? 1.0f : tremoloMakeupGain (amount01));
    // Bias is a tremolo-only colour; in panning mode it stays parked at 0.
    bias.setTargetValue (panning ? 0.0f : bias01);
    wetMix.setTargetValue (engaged ? 1.0f : 0.0f);

    if (numSamples > static_cast<int> (modBuffer.size()))
        modBuffer.assign (static_cast<size_t> (numSamples), 0.0f);

    // One shaped LFO value per sample, slew-limited so nothing steps the gain in a
    // single sample. Same helper the UI preview uses, so the drawing still tracks.
    for (int i = 0; i < numSamples; ++i)
    {
        const float raw = ee::dsp::lfoValue (static_cast<float> (lfoPhase), shape01);
        modZ1 += modSlewCoeff * (raw - modZ1);
        modBuffer[static_cast<size_t> (i)] = modZ1;

        lfoPhase += phaseInc;
        // Branchless wrap to [0, 1). The old `while (lfoPhase >= 1.0)` spun forever
        // if lfoPhase ever went non-finite; std::floor is O(1) whatever it holds,
        // and a non-finite result is caught by the self-heal at the next block.
        lfoPhase -= std::floor (lfoPhase);
    }

    if (numSamples > dryBuffer.getNumSamples())
        dryBuffer.setSize (kMaxChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    if (panning && numCh >= 2)
    {
        // Equal-power auto-pan, sample accurate so it tracks the LFO exactly.
        // juce::dsp::Panner is the obvious reuse here, but it bakes in a fixed 50 ms
        // gain ramp that swallows anything moving at an LFO rate, so the pan law is
        // applied directly - unity in the centre, +3 dB / silence at the extremes.
        constexpr float kCentreComp = juce::MathConstants<float>::sqrt2;
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const float d = depth.getNextValue();
            const float pan = juce::jlimit (-1.0f, 1.0f, d * modBuffer[static_cast<size_t> (i)]);
            const float angle = (pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;

            left[i] *= std::cos (angle) * kCentreComp;
            right[i] *= std::sin (angle) * kCentreComp;
        }
        makeup.skip (numSamples);
        bias.skip (numSamples);
    }
    else if (! panning)
    {
        // Tremolo: LFO at +1 is unity, at -1 is (1 - depth). JUCE has no tremolo
        // primitive, so the gain law is written out here. The make-up factor lifts
        // the whole envelope so bringing the depth up doesn't just make it quieter.
        // Bias (0 = clean opto, 1 = full bias-tube) reshapes the ducking envelope
        // and folds in a throb-synced asymmetric drive; see the constants above.
        for (int i = 0; i < numSamples; ++i)
        {
            const float d = depth.getNextValue();
            const float mk = makeup.getNextValue();
            const float b01 = bias.getNextValue();
            const float rawDuck = 0.5f - 0.5f * modBuffer[static_cast<size_t> (i)]; // 0 loud .. 1 quiet

            // Bend the duck toward a harder, flatter-bottomed pulse as Bias comes up.
            // Exponent 1 (b01 = 0) leaves the LFO shape exactly as the opto law had it.
            const float duck =
                b01 > 0.0f ? std::pow (juce::jlimit (0.0f, 1.0f, rawDuck), 1.0f - kBiasDuckSkew * b01) : rawDuck;

            const float g = mk * (1.0f - d * duck);

            if (b01 <= 0.0f)
            {
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
                continue;
            }

            // Drive rises with the (shaped) dip, so the grind swells and clears in
            // time with the throb. One-sided offset -> pulsing even harmonics; the
            // trim tracks the drive so the level only sags a little.
            const float driveAmt = b01 * d * duck;
            const float k = 1.0f + kBiasDrive * driveAmt;
            const float trim = 1.0f / (1.0f + kBiasTrim * driveAmt);
            const float tanhAsym = std::tanh (kBiasAsym * driveAmt);

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* s = buffer.getWritePointer (ch) + i;
                const float clean = *s * g;

                float coloured = (std::tanh (clean * k + kBiasAsym * driveAmt) - tanhAsym) * trim;
                biasDcState[ch] += biasDcCoeff * (coloured - biasDcState[ch]);
                coloured -= biasDcState[ch];

                *s = clean + b01 * (coloured - clean);
            }
        }
    }
    else
    {
        // Panning asked for on a mono output: nothing sensible to sweep, leave dry.
        depth.skip (numSamples);
        makeup.skip (numSamples);
        bias.skip (numSamples);
    }

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

    // Tempo-sync toggle centred above the Rate knob, reusing Peak Delay's
    // MiniToggle and its amber lit colour.
    spec.toggles = {
        { .parameterID = kSyncID,
          .caption = "Sync",
          .afterKnobIndex = 1,
          .litColour = juce::Colour (0xffffaa33),
          .centeredAbove = true,
          .onClick = [this] { onSyncToggled(); } },
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
