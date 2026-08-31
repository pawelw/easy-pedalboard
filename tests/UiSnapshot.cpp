// Renders the pedal UI offscreen to a PNG so the layout can be inspected
// without launching a host.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/dsp/TempoDivision.h"
#include "ee/ui/PedalEditor.h"

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

/** Minimal host-free processor carrying the same parameters as Easy Reverb. */
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
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "on", 1 }, "On", true));

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
    spec.name = "Easy Reverb";
    spec.tagline = "Decay drives room size and predelay";
    spec.version = "v0.10.0";
    spec.knobs = { { "decay", "Decay" }, { "mix", "Mix" },
                   { "shimmer", "Shimmer" }, { "locut", "Low Cut" } };
    spec.centreKnob = ee::ui::KnobSpec {
        .parameterID = "res", .caption = "reso", .compact = true, .compactCaption = true };
    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Easy Delay. */
class DelaySnapshotProcessor : public SnapshotProcessor
{
public:
    DelaySnapshotProcessor() : SnapshotProcessor (createDelayLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createDelayLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto divisions = ee::dsp::tempoDivisionLabels();

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "ltime", 1 }, "Left Time", divisions, 5));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "rtime", 1 }, "Right Time", divisions, 5));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "sync", 1 }, "Sync L/R", true));

        for (const auto* id : { "fb", "mix", "mod", "tape" })
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, id,
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeDelaySpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Easy Delay";
    spec.tagline = "Tempo-synced stereo delay";
    spec.version = "v0.10.0";
    const juce::Colour tapeCap { 0xff375916 };
    const juce::Colour tapeBorder { 0xff17280b };
    spec.knobs = { { "ltime", "Left Time" }, { "rtime", "Right Time" }, { "fb", "Feedback" },
                   { "mix", "Mix" }, { "mod", "Mod" },
                   { "tape", "Tape", tapeCap, tapeBorder, tapeCap } };
    spec.toggles = { { "sync", "Sync", 0, juce::Colour (0xffffaa33) } };
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

/** Minimal host-free processor carrying the same parameters as Easy EQ. */
class EqSnapshotProcessor : public SnapshotProcessor
{
public:
    EqSnapshotProcessor() : SnapshotProcessor (createEqLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createEqLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto dbRange = juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f);
        const auto dbAttributes =
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (decibelsToText);

        for (const auto* id : { "level", "b100", "b200", "b400", "b800", "b1k6", "b3k2", "b6k4" })
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, id, dbRange, 0.0f, dbAttributes));

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

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeEqSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Easy EQ";
    spec.tagline = "Seven-band graphic EQ";
    spec.version = "v0.10.0";
    spec.sliders = {
        { .parameterID = "level", .caption = "LEVEL",
          .fill = juce::Colour (0xffd6d6d6), .joinCurve = false },
        { .parameterID = "b100", .caption = "100",  .axisHz = 100.0f },
        { .parameterID = "b200", .caption = "200",  .axisHz = 200.0f },
        { .parameterID = "b400", .caption = "400",  .axisHz = 400.0f },
        { .parameterID = "b800", .caption = "800",  .axisHz = 800.0f },
        { .parameterID = "b1k6", .caption = "1.6k", .axisHz = 1600.0f },
        { .parameterID = "b3k2", .caption = "3.2k", .axisHz = 3200.0f },
        { .parameterID = "b6k4", .caption = "6.4k", .axisHz = 6400.0f },
    };
    spec.cornerKnobs = {
        { .parameterID = "locut", .compact = true, .cutSide = ee::ui::CutSide::low },
        { .parameterID = "hicut", .compact = true, .cutSide = ee::ui::CutSide::high,
          .invertedArc = true },
    };
    spec.groupTrims = {
        { .caption = "LOW", .sliderIndices = { 1, 2 } },
        { .caption = "MID", .sliderIndices = { 3, 4, 5 } },
        { .caption = "HI",  .sliderIndices = { 6, 7 } },
    };
    spec.width = 442;
    spec.compactKnobDiameter = 62;
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Easy Trem & Pan. */
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

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "amount", 1 }, "Amount", percent, 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "rate", 1 }, "Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float, int) { return juce::String ("1/8"); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "shape", 1 }, "Shape", percent, 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "bias", 1 }, "Tube", percent, 0.0f, percentAttributes));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "mode", 1 }, "Panning", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "sync", 1 }, "Tempo Sync", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeTremPanSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Easy Trem & Pan";
    spec.version = "v0.10.0";
    spec.knobs = { { "amount", "Amount" }, { "rate", "Rate" }, { "shape", "Shape" },
                   { "bias", "Tube" } };

    const juce::Colour cream { 0xfffee1b8 };
    spec.slideToggle = ee::ui::SlideToggleSpec {
        .parameterID = "mode", .labelOff = "Tremolo", .labelOn = "Panning", .accent = cream };
    spec.toggles = {
        { .parameterID = "sync", .caption = "Sync", .afterKnobIndex = 1,
          .litColour = juce::Colour (0xffffaa33), .centeredAbove = true },
    };
    spec.waveDisplay = ee::ui::WaveDisplaySpec {
        .amountID = "amount", .rateID = "rate", .shapeID = "shape", .modeID = "mode" };
    spec.knobsPerRow = 4;
    spec.width = ee::ui::knobRowWidth (3);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Easy Chorus. */
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
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "depth", 1 }, "Depth", percent, 45.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "phase", 1 }, "Phase",
            juce::NormalisableRange<float> (0.0f, 180.0f, 1.0f), 110.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (juce::roundToInt (v))
                                           + juce::String::fromUTF8 ("\xc2\xb0"); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mix", 1 }, "Mix", percent, 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeChorusSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Easy Chorus";
    spec.tagline = "Wide stereo chorus";
    spec.version = "v0.10.0";
    spec.knobs = { { "rate", "Rate" }, { "depth", "Depth" },
                   { "phase", "Phase" }, { "mix", "Mix" } };
    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
    return spec;
}

/** Minimal host-free processor carrying the same parameters as Easy Overdrive. */
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
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "tone", 1 }, "Tone", percent, 50.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "drive", 1 }, "Drive", percent, 35.0f, percentAttributes));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "on", 1 }, "On", true));

        return layout;
    }
};

ee::ui::PedalSpec makeOverdriveSpec()
{
    ee::ui::PedalSpec spec;
    spec.name = "Easy Overdrive";
    spec.tagline = "Soft-clipping overdrive";
    spec.version = "v0.10.0";
    spec.knobs = { { "level", "Level" }, { "drive", "Drive" }, { "tone", "Tone" } };
    spec.knobsPerRow = 2;
    spec.width = ee::ui::knobRowWidth (spec.knobsPerRow);
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
    ee::ui::PedalEditor editor (processor, processor.apvts, makeSpec(), ee::ui::PedalTheme::blue());

#if EE_SHIMMER_TUNER
    editor.setSidePanel (std::make_unique<ShimmerTunerPanel> (ee::dsp::ShimmerTuning{},
                                                             [] (const ee::dsp::ShimmerTuning&) {}),
                         ShimmerTunerPanel::preferredWidth);
#endif

    writePng (editor, outputFile);
}

void renderDelay (const juce::File& outputFile)
{
    DelaySnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeDelaySpec(), ee::ui::PedalTheme::gold());

#if EE_TAPE_TUNER
    editor.setSidePanel (std::make_unique<TapeTunerPanel> (ee::dsp::TapeTuning{},
                                                           [] (const ee::dsp::TapeTuning&) {}),
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
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File dir = argc > 1 ? juce::File (juce::String (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory();

    render (dir.getChildFile ("pedal.png"));
    renderDelay (dir.getChildFile ("delay.png"));
    renderEq (dir.getChildFile ("eq.png"));
    renderTremPan (dir.getChildFile ("trempan.png"));
    renderChorus (dir.getChildFile ("chorus.png"));
    renderOverdrive (dir.getChildFile ("overdrive.png"));

    return 0;
}
