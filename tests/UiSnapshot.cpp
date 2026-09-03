// Renders the pedal UI offscreen to a PNG so the layout can be inspected
// without launching a host.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/dsp/FdnReverb.h"
#include "ee/dsp/GrainSyncMap.h"
#include "ee/dsp/GrainerConfig.h"
#include "ee/dsp/Lfo.h"
#include "ee/dsp/TempoDivision.h"
#include "ee/ui/PedalEditor.h"

#include "BinaryData.h"
#include "TapeAssets.h"

#include <cmath>

#if EE_TAPE_TUNER
#include "TapeTunerPanel.h"
#endif

#if EE_SHIMMER_TUNER
#include "ShimmerTunerPanel.h"
#endif

namespace
{
juce::String secondsToText (float value, int)
{
    return juce::String (value, value < 1.0f ? 2 : 1) + " s";
}

juce::String percentToText (float value, int)
{
    return juce::String (juce::roundToInt (value)) + " %";
}

juce::String hertzToText (float value, int)
{
    if (value <= 20.5f)
        return "off";
    return juce::String (juce::roundToInt (value)) + " Hz";
}

/** Minimal host-free processor carrying the same parameters as Peak Reverb. */
class SnapshotProcessor : public juce::AudioProcessor
{
public:
    explicit SnapshotProcessor (juce::AudioProcessorValueTreeState::ParameterLayout layout = createLayout())
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("In", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", std::move (layout))
    {
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto decayRange = juce::NormalisableRange<float> (0.3f, 8.0f);
        decayRange.setSkewForCentre (2.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "decay", 1 }, "Decay Time", decayRange, 3.2f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (secondsToText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));
        auto lowCutRange = juce::NormalisableRange<float> (20.0f, 800.0f);
        lowCutRange.setSkewForCentre (180.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "locut", 1 }, "Low Cut", lowCutRange, 20.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hertzToText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "res", 1 }, "Resonance", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "shimmer", 1 }, "Shimmer", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Snapshot"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

ee::ui::PedalSpec makeSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Reverb";
    spec.tagline = "Decay drives room size and predelay";
    spec.version = "v0.10.0";
    spec.knobs = { { "decay", "Decay" }, { "mix", "Mix" }, { "shimmer", "Shimmer" }, { "locut", "Low Cut" } };
    spec.centreKnob =
        ee::ui::KnobSpec { .parameterID = "res", .caption = "reso", .compact = true, .compactCaption = true };
    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Delay. */
class DelaySnapshotProcessor : public SnapshotProcessor
{
public:
    DelaySnapshotProcessor() : SnapshotProcessor (createDelayLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createDelayLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto divisions = ee::dsp::tempoDivisionLabels();

        layout.add (
            std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "ltime", 1 }, "Left Time", divisions, 5));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rtime", 1 }, "Right Time",
                                                                  divisions, 5));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sync", 1 }, "Sync L/R", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "timeunit", 1 }, "Time Unit", false));

        for (const auto* id : { "fb", "mix", "mod", "tape" })
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, id, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

/** Mirrors drawLinkIcon in plugins/peak-delay. */
void drawDelayLinkIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    if (side <= 0.0f)
        return;

    const float linkW = side * 0.44f;
    const float linkH = side * 0.74f;
    const float offset = side * 0.19f;
    const float stroke = juce::jmax (1.2f, side * 0.11f);

    juce::Path capsule;
    capsule.addRoundedRectangle (-linkW * 0.5f, -linkH * 0.5f, linkW, linkH, linkW * 0.5f);

    const auto centre = area.getCentre();
    const float diagonal = offset * juce::MathConstants<float>::sqrt2 * 0.5f;

    g.setColour (colour);

    for (const float sign : { -1.0f, 1.0f })
    {
        const auto place = juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f)
                               .translated (centre.x - sign * diagonal, centre.y + sign * diagonal);
        g.strokePath (capsule, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded),
                      place);
    }
}

/** Mirrors drawMsIcon in plugins/peak-delay and plugins/peak-trem-pan (both
    carry the same "ms" wordmark on their tempo/unit toggle). */
void drawMsIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto box = area.withSizeKeepingCentre (area.getWidth() * 1.6f, area.getHeight());
    g.setColour (colour);
    g.setFont (juce::Font (juce::FontOptions (area.getHeight() * 0.95f)).boldened());
    g.drawText ("ms", box, juce::Justification::centred, false);
}

ee::ui::PedalSpec makeDelaySpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Delay";
    spec.tagline = "Tempo-synced stereo delay";
    spec.version = "v0.10.0";
    const juce::Colour tapeCap { 0xff375916 };
    const juce::Colour tapeBorder { 0xff17280b };

    // The only photographic cap on a face of digital ones - see the pedal's own
    // createEditor.
    auto tape = ee::ui::KnobSpec { "tape", "Tape", tapeCap, tapeBorder, tapeCap };
    tape.capStyle = ee::ui::ControlStyle::analog;

    spec.knobs = {
        { .parameterID = "ltime", .caption = "Left Time" },
        { .parameterID = "rtime", .caption = "Right Time" },
        { "fb", "Feedback" },
        { "mix", "Mix" },
        { "mod", "Mod" },
        tape,
    };

    spec.toggles = {
        { .parameterID = "sync", .caption = "Sync", .afterKnobIndex = 0, .icon = drawDelayLinkIcon },
        { .parameterID = "timeunit", .caption = "ms", .afterKnobIndex = 0, .gapRise = -6,
          .icon = drawMsIcon },
    };
    spec.knobsPerRow = 3;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

juce::String decibelsToText (float value, int)
{
    const int rounded = juce::roundToInt (value);
    return (rounded > 0 ? "+" : "") + juce::String (rounded);
}

juce::String freqToText (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 1) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String loCutToText (float hz, int)
{
    return hz <= 20.5f ? juce::String ("0 Hz") : freqToText (hz);
}

juce::String hiCutToText (float hz, int)
{
    return hz >= 19999.5f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : freqToText (hz);
}

/** Minimal host-free processor carrying the same parameters as Peak EQ. */
class EqSnapshotProcessor : public SnapshotProcessor
{
public:
    EqSnapshotProcessor() : SnapshotProcessor (createEqLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createEqLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto dbRange = juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f);
        const auto dbAttributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (decibelsToText);

        for (const auto* id : { "level", "b100", "b200", "b400", "b800", "b1k6", "b3k2", "b6k4" })
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, id, dbRange, 0.0f,
                                                                     dbAttributes));

        auto loCutRange = juce::NormalisableRange<float> (20.0f, 1200.0f);
        loCutRange.setSkewForCentre (120.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "locut", 1 }, "Low Cut", loCutRange, 20.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (loCutToText)));

        auto hiCutRange = juce::NormalisableRange<float> (1200.0f, 20000.0f);
        hiCutRange.setSkewForCentre (4000.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "hicut", 1 }, "High Cut", hiCutRange, 20000.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hiCutToText)));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeEqSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak EQ";
    spec.tagline = "Seven-band graphic EQ";
    spec.version = "v0.10.0";
    spec.sliders = {
        { .parameterID = "level", .caption = "LEVEL", .fill = juce::Colour (0xffd6d6d6), .joinCurve = false },
        { .parameterID = "b100", .caption = "100", .axisHz = 100.0f },
        { .parameterID = "b200", .caption = "200", .axisHz = 200.0f },
        { .parameterID = "b400", .caption = "400", .axisHz = 400.0f },
        { .parameterID = "b800", .caption = "800", .axisHz = 800.0f },
        { .parameterID = "b1k6", .caption = "1.6k", .axisHz = 1600.0f },
        { .parameterID = "b3k2", .caption = "3.2k", .axisHz = 3200.0f },
        { .parameterID = "b6k4", .caption = "6.4k", .axisHz = 6400.0f },
    };
    spec.cornerKnobs = {
        { .parameterID = "locut", .compact = true, .cutSide = ee::ui::CutSide::low },
        { .parameterID = "hicut", .compact = true, .cutSide = ee::ui::CutSide::high, .invertedArc = true },
    };
    spec.groupTrims = {
        { .caption = "LOW", .sliderIndices = { 1, 2 } },
        { .caption = "MID", .sliderIndices = { 3, 4, 5 } },
        { .caption = "HI", .sliderIndices = { 6, 7 } },
    };
    spec.width = 442;
    spec.compactKnobDiameter = 62;
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Trem & Pan. */
class TremPanSnapshotProcessor : public SnapshotProcessor
{
public:
    TremPanSnapshotProcessor() : SnapshotProcessor (createTremPanLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createTremPanLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
        const auto percentAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "amount", 1 }, "Amount", percent,
                                                                 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "rate", 1 }, "Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float, int)
                                                                               { return juce::String ("1/8"); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "shape", 1 }, "Shape", percent,
                                                                 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bias", 1 }, "Tube", percent, 0.0f,
                                                                 percentAttributes));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "mode", 1 }, "Panning", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sync", 1 }, "Tempo Sync", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeTremPanSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Trem & Pan";
    spec.version = "v0.10.0";
    spec.knobs = { { "amount", "Amount" }, { "rate", "Rate" }, { "shape", "Shape" }, { "bias", "Tube" } };

    const juce::Colour cream { 0xfffee1b8 };
    spec.slideToggle =
        ee::ui::SlideToggleSpec { .parameterID = "mode", .labelOff = "Tremolo", .labelOn = "Panning", .accent = cream };
    spec.toggles = {
        { .parameterID = "sync",
          .caption = "Sync",
          .afterKnobIndex = 1,
          .centeredAbove = true,
          .icon = drawMsIcon,
          .controlStyle = ee::ui::ControlStyle::digital },
    };
    spec.waveDisplay =
        ee::ui::WaveDisplaySpec { .amountID = "amount", .rateID = "rate", .shapeID = "shape", .modeID = "mode" };
    spec.knobsPerRow = 4;
    spec.width = ee::ui::knobRowWidth (3);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Chorus. */
class ChorusSnapshotProcessor : public SnapshotProcessor
{
public:
    ChorusSnapshotProcessor() : SnapshotProcessor (createChorusLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createChorusLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
        const auto percentAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

        auto rateRange = juce::NormalisableRange<float> (0.05f, 8.0f);
        rateRange.setSkewForCentre (0.8f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "rate", 1 }, "Rate", rateRange, 0.6f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth", percent,
                                                                 45.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "phase", 1 }, "Phase", juce::NormalisableRange<float> (0.0f, 180.0f, 1.0f), 110.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int)
                { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix", percent, 50.0f,
                                                                 percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeChorusSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Chorus";
    spec.tagline = "Wide stereo chorus";
    spec.version = "v0.10.0";
    spec.knobs = { { "rate", "Rate" }, { "depth", "Depth" }, { "phase", "Phase" }, { "mix", "Mix" } };
    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Grain. */
class GrainSnapshotProcessor : public SnapshotProcessor
{
public:
    GrainSnapshotProcessor() : SnapshotProcessor (createGrainLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createGrainLayout()
    {
        namespace cfg = ee::dsp::config;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
        const auto percentAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);
        const auto unit = juce::NormalisableRange<float> (0.0f, 1.0f);

        // Size and Density mirror PeakGrainProcessor: one normalised knob each,
        // reinterpreted by a Sync switch. The snapshot renders the free reading.
        const auto durationMap = [] (float lo, float hi, float centre)
        {
            juce::NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return ee::dsp::GrainSyncMap { r, true };
        };
        const auto rateMap = [] (float lo, float hi, float centre)
        {
            juce::NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return ee::dsp::GrainSyncMap { r, false };
        };

        const auto sizeText = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [durationMap] (float v, int)
            { return durationMap (cfg::kMinGrainMs, cfg::kMaxGrainMs, cfg::kGrainSkewMs).toText (v, false, 120.0); });
        const auto densityText = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [rateMap] (float v, int)
            { return rateMap (cfg::kMinDensityHz, cfg::kMaxDensityHz, cfg::kDensitySkewHz).toText (v, false, 120.0); });
        const auto delayTimeText = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [durationMap] (float v, int)
            { return durationMap (cfg::kMinTimeMs, cfg::kMaxTimeMs, cfg::kTimeSkewMs).toText (v, false, 120.0); });

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "size", 1 }, "Size", unit,
                                                                 cfg::kDefaultSize01, sizeText));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "density", 1 }, "Density", unit,
                                                                 cfg::kDefaultDensity01, densityText));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "ssync", 1 }, "Size Sync",
                                                                cfg::kDefaultSizeSync));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "dsync", 1 }, "Density Sync",
                                                                cfg::kDefaultDensitySync));

        // The granular delay half is off the face but the parameters remain.
        auto timeRange = juce::NormalisableRange<float> (cfg::kMinTimeMs, cfg::kMaxTimeMs);
        timeRange.setSkewForCentre (cfg::kTimeSkewMs);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "time", 1 }, "Time", timeRange,
                                                                 cfg::kDefaultTimeMs));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "feedback", 1 }, "Feedback",
                                                                 percent, cfg::kDefaultFeedbackPct, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "stretch", 1 }, "Stretch", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
            cfg::kDefaultStretchPct));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 1 }, "Freeze", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "shape", 1 }, "Shape", percent,
                                                                 cfg::kDefaultShapePct, percentAttributes));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "scatter", 1 }, "Scatter",
                                                                 percent, cfg::kDefaultScatterPct, percentAttributes));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reverse", 1 }, "Reverse",
                                                                 percent, cfg::kDefaultReversePct, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "stereo", 1 }, "Stereo", percent,
                                                                 cfg::kDefaultStereoPct, percentAttributes));

        auto detuneRange = juce::NormalisableRange<float> (cfg::kMinDetuneCents, cfg::kMaxDetuneCents);
        detuneRange.setSkewForCentre (cfg::kDetuneSkewCents);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "detune", 1 }, "Detune", detuneRange, cfg::kDefaultDetuneCents,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "plow", 1 }, "Pitch Low", percent,
                                                                 cfg::kDefaultPitchLowPct, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "puni", 1 }, "Pitch Unison",
                                                                 percent, cfg::kDefaultPitchUnisonPct,
                                                                 percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "phigh", 1 }, "Pitch High",
                                                                 percent, cfg::kDefaultPitchHighPct,
                                                                 percentAttributes));

        // Post delay.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "dtime", 1 }, "Delay Time", unit,
                                                                 cfg::kDefaultDelayTime01, delayTimeText));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "dtsync", 1 }, "Delay Sync",
                                                                cfg::kDefaultDelaySync));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "dfb", 1 }, "Delay Feedback",
                                                                 percent, cfg::kDefaultDelayFeedbackPct,
                                                                 percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "dmix", 1 }, "Delay Mix", percent,
                                                                 cfg::kDefaultDelayMixPct, percentAttributes));

        // Reverb: decay in seconds, its own mix.
        auto decayRange = juce::NormalisableRange<float> (ee::dsp::FdnReverb::kMinDecay, ee::dsp::FdnReverb::kMaxDecay);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "decay", 1 }, "Decay", decayRange, cfg::kDefaultReverbDecaySeconds,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, 2) + " s"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "rmix", 1 }, "Reverb Mix", percent,
                                                                 cfg::kDefaultReverbMixPct, percentAttributes));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix", percent,
                                                                 cfg::kDefaultGrainMixPct, percentAttributes));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeGrainSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Grain";
    spec.tagline = "Granular delay into delay into plate";
    spec.version = "v0.11.0";
    spec.knobs = {
        { "size", "Size" }, { "density", "Destiny" }, { "shape", "Shape" }, { "mix", "Mix" },

        { "plow", "Low" }, { "puni", "Unison" }, { "phigh", "High" }, { "detune", "Detune" },

        { "reverse", "Reverse" }, { "scatter", "Scatter" }, { "stereo", "Stereo" },

        { "dtime", "Time" }, { "dfb", "Feedback" }, { "dmix", "Mix" },

        { "decay", "Decay" }, { "rmix", "Mix" },
    };
    spec.knobGroups = {
        { "Grain", 4 },
        { "Pitch", 4 },
        { "Random", 3 },
        { "Delay", 3 },
        { "Reverb", 2 },
    };
    spec.toggles = {
        { .parameterID = "ssync", .caption = "Sync", .afterKnobIndex = 0, .centeredBelow = true, .belowGap = 10,
          .icon = drawMsIcon, .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = "dsync", .caption = "Sync", .afterKnobIndex = 1, .centeredBelow = true, .belowGap = 10,
          .icon = drawMsIcon, .controlStyle = ee::ui::ControlStyle::digital },
        { .parameterID = "dtsync", .caption = "Sync", .afterKnobIndex = 11, .centeredBelow = true, .belowGap = 10,
          .icon = drawMsIcon, .controlStyle = ee::ui::ControlStyle::digital },
    };
    spec.slideToggle = ee::ui::SlideToggleSpec { .parameterID = "freeze", .labelOff = "Live", .labelOn = "Freeze" };
    spec.titleBesideLogo = true;
    spec.knobsPerRow = 4;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);

    // Five ranks of knobs plus the switch strip, and a Sync button hanging under
    // three of them: smaller caps, a wide row gap so those buttons clear the
    // captioned box below them, and a tall face.
    spec.knobDiameter = 82;
    spec.knobRowGap = 64;
    spec.height = 1060;
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Phase. */
class PhaseSnapshotProcessor : public SnapshotProcessor
{
public:
    PhaseSnapshotProcessor() : SnapshotProcessor (createPhaseLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createPhaseLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
        const auto percentAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

        auto rateRange = juce::NormalisableRange<float> (0.03f, 8.0f);
        rateRange.setSkewForCentre (0.7f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "rate", 1 }, "Rate", rateRange, 0.35f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth", percent,
                                                                 75.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makePhaseSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Phase";
    spec.tagline = "Analog-style stereo phaser";
    spec.version = "v0.10.0";
    spec.knobs = { { "rate", "Rate" }, { "depth", "Depth" } };
    spec.knobsPerRow = 1;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Spring. */
class SpringSnapshotProcessor : public SnapshotProcessor
{
public:
    SpringSnapshotProcessor() : SnapshotProcessor (createSpringLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createSpringLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto decayRange = juce::NormalisableRange<float> (0.4f, 8.0f);
        decayRange.setSkewForCentre (2.2f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "decay", 1 }, "Decay", decayRange, 1.8f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, 2) + " s"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "stereo", 1 }, "Stereo", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeSpringSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Spring";
    spec.tagline = "Dispersive spring tank";
    spec.version = "v0.10.0";
    spec.knobs = { { .parameterID = "mix", .caption = "Mix", .captionUntilTouched = true },
                   { .parameterID = "decay", .caption = "Decay", .captionUntilTouched = true } };
    spec.toggles = { { .parameterID = "stereo",
                       .caption = "Stereo",
                       .afterKnobIndex = 0,
                       .centeredBelow = true,
                       .belowGap = 10,
                       .asSwitch = ee::ui::SlideToggleSpec { .labelOff = "Mono", .labelOn = "Stereo" } } };
    spec.knobsPerRow = 1;
    spec.knobRowGap = 56;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Overdrive. */
class OverdriveSnapshotProcessor : public SnapshotProcessor
{
public:
    OverdriveSnapshotProcessor() : SnapshotProcessor (createOverdriveLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createOverdriveLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
        const auto percentAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

        auto levelRange = juce::NormalisableRange<float> (-30.0f, 6.0f, 0.1f);
        levelRange.setSkewForCentre (-6.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "level", 1 }, "Level", levelRange, 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, 1) + " dB"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "tone", 1 }, "Tone", percent,
                                                                 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "drive", 1 }, "Drive", percent,
                                                                 35.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeOverdriveSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Overdrive";
    spec.tagline = "Soft-clipping overdrive";
    spec.version = "v0.10.0";
    spec.knobs = { { "level", "Level" }, { "drive", "Drive" }, { "tone", "Tone" } };
    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Wah. */
class WahSnapshotProcessor : public SnapshotProcessor
{
public:
    WahSnapshotProcessor() : SnapshotProcessor (createWahLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createWahLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
        const auto percentAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

        for (const auto* id : { "range", "freq", "q", "mix", "decay", "shape" })
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, id, percent, 45.0f,
                                                                     percentAttributes));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "time", 1 }, "Time", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "ftype", 1 }, "Type", percent,
                                                                 50.0f, percentAttributes));
        for (const auto* id : { "stereo", "sync" })
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, id, false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeWahSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Wah";
    spec.tagline = "LFO-driven modulated filter";
    spec.knobsPerRow = 4;
    spec.knobDividerAfterColumn = 2;
    spec.knobBlockRise = 10;
    spec.displayBandRise = 8;
    spec.knobRowGap = 4;
    spec.width = 566;
    spec.knobDiameter = 104;

    constexpr int plain = 88; // the six knobs that carry only a number

    auto shapeIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        r = r.reduced (r.getWidth() * 0.10f, r.getHeight() * 0.28f);
        juce::Path p;
        for (int i = 0; i <= 48; ++i)
        {
            const float t = (float)i / 48.0f;
            // Mirrored, so the glyph opens upward - see drawLfoShapeIcon in
            // plugins/peak-wah.
            const float y = r.getCentreY() + ee::dsp::lfoValue (t, 0.45f) * r.getHeight() * 0.5f;
            i == 0 ? p.startNewSubPath (r.getX() + t * r.getWidth(), y) : p.lineTo (r.getX() + t * r.getWidth(), y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    auto typeIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        r = r.reduced (r.getWidth() * 0.12f, r.getHeight() * 0.26f);
        juce::Path p;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = (float)i / 40.0f;
            const float v = std::exp (-0.5f * std::pow ((t - 0.5f) / 0.16f, 2.0f)); // band-pass
            const float y = r.getBottom() - juce::jlimit (0.0f, 1.15f, v) * r.getHeight();
            i == 0 ? p.startNewSubPath (r.getX() + t * r.getWidth(), y) : p.lineTo (r.getX() + t * r.getWidth(), y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    // Two clusters of four, split by a rule: the filter on the left, its
    // modulation on the right. Shape and Type carry their glyph on the cap and
    // keep the row's full size; the other six are a size down.
    spec.knobs = {
        { .parameterID = "mix", .caption = "Mix" }, // full size: the headline control
        { .parameterID = "freq", .caption = "Freq", .diameter = plain },
        { .parameterID = "decay",
          .caption = "Decay",
          .diameter = plain,
          .endMarker = juce::Colour { 0xff2f6b46 },
          .endMarkerLabel = juce::String::fromUTF8 ("\u221e"),
          .captionUntilTouched = true },
        { .parameterID = "shape", .caption = "Shape", .captionUntilTouched = true, .capIcon = shapeIcon },
        { .parameterID = "q", .caption = "Q", .diameter = plain },
        { .parameterID = "range", .caption = "Range", .diameter = plain },
        { .parameterID = "time", .caption = "Time", .diameter = plain, .captionUntilTouched = true },
        { .parameterID = "ftype", .caption = "Filter Type", .captionUntilTouched = true, .capIcon = typeIcon },
    };

    spec.toggles = { { .parameterID = "sync",
                       .caption = "Sync",
                       .afterKnobIndex = 6,
                       .centeredBelow = true,
                       .asSwitch =
                           ee::ui::SlideToggleSpec { .labelOff = "ms", .labelOn = "Sync", .invertPosition = true } } };

    spec.titleBesideLogo = true;
    spec.titleRowAlignRight = true;
    spec.titleRowRightInset = 34;

    spec.slideToggle = ee::ui::SlideToggleSpec {
        .parameterID = "stereo", .labelOff = "Mono", .labelOn = "Stereo", .labelFlushLeft = true
    };
    spec.slideToggleBottom = true;

    spec.filterScope = ee::ui::FilterScopeSpec { .baseFreqHz = [] { return 520.0f; },
                                                 .resonance01 = [] { return 0.55f; },
                                                 .modL = [] { return 0.55f; },
                                                 .modR = [] { return -0.35f; },
                                                 .sweepDepth01 = [] { return 0.45f; },
                                                 .baseColour = juce::Colour { 0xffc2562f },
                                                 .sweepColour = juce::Colour { 0xff9aa0aa },
                                                 .sweepRatioMax = 5.0f,
                                                 .height = 66 };
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Peak Tape. */
class TapeSnapshotProcessor : public SnapshotProcessor
{
public:
    TapeSnapshotProcessor() : SnapshotProcessor (createTapeLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createTapeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto percent = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);
        const auto percentAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText);

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sat", 1 }, "Saturation", percent,
                                                                 35.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "wear", 1 }, "Wear", percent,
                                                                 30.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "flutter", 1 }, "Flutter", percent,
                                                                 25.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "tone", 1 }, "Tone", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float, int)
                                                                               { return juce::String ("0 %"); })));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "stereo", 1 }, "Stereo", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "noise", 1 }, "Noise", percent,
                                                                 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeTapeSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Peak Tape";
    spec.tagline = "Analogue warmth, wobble and wear";
    spec.version = "v0.10.0";
    spec.knobs = { { "sat", "Saturation" }, { "flutter", "Flutter" }, { "wear", "Wear" }, { "noise", "Noise" } };

    // Tone gets Peak Reverb's RESO treatment: a small vector cap between the
    // rows with its caption alone under it, plus the bipolar arc and detent.
    spec.centreKnob = ee::ui::KnobSpec { .parameterID = "tone",
                                         .caption = "Tone",
                                         .compact = true,
                                         .compactCaption = true,
                                         .bipolarArc = true,
                                         .centreDetent = true };

    spec.slideToggle = ee::ui::SlideToggleSpec { .parameterID = "stereo", .labelOff = "Mono", .labelOn = "Stereo" };
    spec.slideToggleCentred = true;
    spec.slideToggleRise = 3;

    spec.knobRowGap = 12;
    spec.knobBlockRise = 12;

    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);

    spec.titleBesideLogo = true;

    spec.titleImage = juce::ImageCache::getFromMemory (TapeAssets::tape_png, TapeAssets::tape_pngSize);
    spec.titleImageHeight = 60;
    spec.titleImageTint = juce::Colours::white;
    return spec;
}

void writePng (juce::Component& editor, const juce::File& outputFile)
{
    const int w = editor.getWidth();
    const int h = editor.getHeight();

    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (image);
        editor.paintEntireComponent (g, true);
    }

    juce::PNGImageFormat png;
    outputFile.deleteFile();
    if (auto stream = outputFile.createOutputStream())
        png.writeImageToStream (image, *stream);

    std::printf ("wrote %s (%d x %d)\n", outputFile.getFullPathName().toRawUTF8(), w, h);
}

void render (const juce::File& outputFile)
{
    SnapshotProcessor processor;
    // Mirror PeakReverbProcessor::createEditor: blue palette, silver-bezel caps,
    // sky background, black lettering.
    auto theme = ee::ui::PedalTheme::blue();
    theme.controlStyle = ee::ui::ControlStyle::analogSilver;
    theme.backgroundImage =
        juce::ImageCache::getFromMemory (BinaryData::reverbbg_jpeg, BinaryData::reverbbg_jpegSize);
    theme.textPrimary = juce::Colours::black;
    theme.textSecondary = juce::Colour (0xff3a3a3a);
    theme.title = juce::Colours::black;
    theme.logoTint = juce::Colours::black;

    // Swap the value arc and its background track.
    const auto arcLine = theme.knobTrack;
    theme.knobTrack = theme.accent;
    theme.accent = arcLine;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeSpec(), theme);

#if EE_SHIMMER_TUNER
    editor.setSidePanel (
        std::make_unique<ShimmerTunerPanel> (ee::dsp::ShimmerTuning {}, [] (const ee::dsp::ShimmerTuning&) {}),
        ShimmerTunerPanel::preferredWidth);
#endif

    writePng (editor, outputFile);
}

void renderDelay (const juce::File& outputFile)
{
    DelaySnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeDelaySpec(), ee::ui::PedalTheme::moss());

#if EE_TAPE_TUNER
    editor.setSidePanel (std::make_unique<TapeTunerPanel> (ee::dsp::TapeTuning {}, [] (const ee::dsp::TapeTuning&) {}),
                         TapeTunerPanel::preferredWidth);
#endif

    writePng (editor, outputFile);
}

void renderEq (const juce::File& outputFile)
{
    EqSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeEqSpec(), ee::ui::PedalTheme::silver());
    writePng (editor, outputFile);
}

void renderTremPan (const juce::File& outputFile)
{
    TremPanSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeTremPanSpec(), ee::ui::PedalTheme::teal());
    writePng (editor, outputFile);
}

void renderChorus (const juce::File& outputFile)
{
    ChorusSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeChorusSpec(), ee::ui::PedalTheme::sky());
    writePng (editor, outputFile);
}

void renderOverdrive (const juce::File& outputFile)
{
    OverdriveSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeOverdriveSpec(), ee::ui::PedalTheme::yellow());
    writePng (editor, outputFile);
}

void renderPhase (const juce::File& outputFile)
{
    PhaseSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makePhaseSpec(), ee::ui::PedalTheme::orange());
    writePng (editor, outputFile);
}

void renderSpring (const juce::File& outputFile)
{
    SpringSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeSpringSpec(), ee::ui::PedalTheme::charcoal());
    writePng (editor, outputFile);
}

void renderWah (const juce::File& outputFile)
{
    WahSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeWahSpec(), ee::ui::PedalTheme::white());
    writePng (editor, outputFile);
}

void renderGrain (const juce::File& outputFile)
{
    GrainSnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeGrainSpec(), ee::ui::PedalTheme::onyx());
    writePng (editor, outputFile);
}

void renderTape (const juce::File& outputFile)
{
    TapeSnapshotProcessor processor;
    // Mirror PeakTapeProcessor::createEditor: green palette, silver-bezel caps
    // for contrast against the dark face.
    auto theme = ee::ui::PedalTheme::green();
    theme.controlStyle = ee::ui::ControlStyle::analogSilver;
    theme.bezel = juce::Colour (0xff5c8b24);      // the outer frame
    theme.knobTrack = juce::Colour (0xff8fae6f);  // pale resting ring, not a black halo
    ee::ui::PedalEditor editor (processor, processor.apvts, makeTapeSpec(), theme);
    writePng (editor, outputFile);
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File dir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();

    // Optional second arg: render just one face (substring match on the name
    // below), so an iteration loop does not redraw all eleven.
    const juce::String only = argc > 2 ? juce::String (argv[2]).toLowerCase() : juce::String();
    const auto want = [&only] (juce::StringRef name)
    { return only.isEmpty() || juce::String (name).containsIgnoreCase (only); };

    if (want ("reverb pedal")) render (dir.getChildFile ("pedal.png"));
    if (want ("delay")) renderDelay (dir.getChildFile ("delay.png"));
    if (want ("eq")) renderEq (dir.getChildFile ("eq.png"));
    if (want ("trempan")) renderTremPan (dir.getChildFile ("trempan.png"));
    if (want ("chorus")) renderChorus (dir.getChildFile ("chorus.png"));
    if (want ("overdrive")) renderOverdrive (dir.getChildFile ("overdrive.png"));
    if (want ("phase")) renderPhase (dir.getChildFile ("phase.png"));
    if (want ("spring")) renderSpring (dir.getChildFile ("spring.png"));
    if (want ("wah")) renderWah (dir.getChildFile ("wah.png"));
    if (want ("tape")) renderTape (dir.getChildFile ("tape.png"));
    if (want ("grain")) renderGrain (dir.getChildFile ("grain.png"));

    return 0;
}
