// Renders the pedal UI offscreen to a PNG so the layout can be inspected
// without launching a host.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalEditor.h"

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
    if (value >= 20000.0f)
        return "off";
    if (value >= 1000.0f)
        return juce::String (value / 1000.0f, 1) + " k";
    return juce::String (juce::roundToInt (value)) + " Hz";
}

/** Minimal host-free processor carrying the same parameters as Easy Reverb. */
class SnapshotProcessor : public juce::AudioProcessor
{
public:
    SnapshotProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("In", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", createLayout())
    {
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto decayRange = juce::NormalisableRange<float> (0.3f, 8.0f);
        decayRange.setSkewForCentre (2.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "decay", 1 }, "Decay Time", decayRange, 2.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (secondsToText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentToText)));
        auto highCutRange = juce::NormalisableRange<float> (800.0f, 20000.0f);
        highCutRange.setSkewForCentre (4000.0f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "hicut", 1 }, "High Cut", highCutRange, 8000.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hertzToText)));
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
    spec.version = "v0.2.0";
    spec.bypassParameterID = "on";
    spec.knobs = { { "decay", "Decay" }, { "mix", "Mix" }, { "hicut", "High Cut" } };
    return spec;
}

void render (const juce::File& outputFile, bool engaged)
{
    SnapshotProcessor processor;
    processor.apvts.getParameter ("on")->setValueNotifyingHost (engaged ? 1.0f : 0.0f);

    ee::ui::PedalEditor editor (processor, processor.apvts, makeSpec());

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
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File dir = argc > 1 ? juce::File (juce::String (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory();

    render (dir.getChildFile ("pedal-on.png"), true);
    render (dir.getChildFile ("pedal-off.png"), false);

    return 0;
}
