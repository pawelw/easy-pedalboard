// Renders the pedal UI offscreen to a PNG so the layout can be inspected
// without launching a host.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/dsp/TempoDivision.h"
#include "ee/ui/PedalEditor.h"

#if EE_TAPE_TUNER
 #include "TapeTunerPanel.h"
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
                   { "res", "Resonance" }, { "locut", "Low Cut" } };
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
    const juce::Colour tapeCap { 0xffffaa33 };
    const juce::Colour tapeBorder { 0xffa35f00 };
    spec.knobs = { { "ltime", "Left Time" }, { "rtime", "Right Time" }, { "fb", "Feedback" },
                   { "mix", "Mix" }, { "mod", "Mod" },
                   { "tape", "Tape", tapeCap, tapeBorder, tapeCap } };
    spec.toggles = { { "sync", "Sync", 0, tapeCap } };
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
    spec.width = 340;
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
    writePng (editor, outputFile);
}

void renderDelay (const juce::File& outputFile)
{
    DelaySnapshotProcessor processor;
    ee::ui::PedalEditor editor (processor, processor.apvts, makeDelaySpec(), ee::ui::PedalTheme::silver());

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
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File dir = argc > 1 ? juce::File (juce::String (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory();

    render (dir.getChildFile ("pedal.png"));
    renderDelay (dir.getChildFile ("delay.png"));
    renderEq (dir.getChildFile ("eq.png"));

    return 0;
}
