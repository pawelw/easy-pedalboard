#include "GrainTunerPanel.h"

namespace
{
constexpr int kRowHeight = 22;
constexpr int kNameWidth = 124;
constexpr int kPad = 8;
constexpr int kReadoutHeight = 150;
constexpr int kButtonHeight = 24;

/** A float literal you can paste straight back into the header. At least one
    decimal place, because the entries that display as whole numbers would
    otherwise print "7f", which does not compile. */
juce::String literal (float value, int decimals)
{
    return juce::String (value, juce::jmax (1, decimals)) + "f";
}
} // namespace

GrainTunerPanel::GrainTunerPanel (const ee::dsp::GrainerTuning& initial, ApplyFn applyFn)
    : tuning (initial), defaults (initial), apply (std::move (applyFn))
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);

    for (const auto& entry : ee::dsp::kGrainerTuningEntries)
    {
        auto name = std::make_unique<juce::Label> (juce::String(), entry.name);
        name->setFont (juce::FontOptions (11.0f));
        name->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        rows.addAndMakeVisible (*name);
        names.push_back (std::move (name));

        auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        slider->setRange (entry.minimum, entry.maximum);
        slider->setNumDecimalPlacesToDisplay (entry.decimals);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 18);
        slider->setValue (tuning.*entry.member, juce::dontSendNotification);

        float ee::dsp::GrainerTuning::*member = entry.member;
        slider->onValueChange = [this, member, raw = slider.get()]
        {
            tuning.*member = static_cast<float> (raw->getValue());
            pushToProcessor();
            refreshReadout();
        };

        rows.addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));
    }

    readout.setMultiLine (true);
    readout.setReadOnly (true);
    readout.setScrollbarsShown (true);
    readout.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    readout.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101216));
    readout.setColour (juce::TextEditor::textColourId, juce::Colour (0xffb9e08a));
    addAndMakeVisible (readout);

    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (readout.getText()); };
    addAndMakeVisible (copyButton);

    resetButton.onClick = [this]
    {
        tuning = defaults;

        for (size_t i = 0; i < sliders.size(); ++i)
            sliders[i]->setValue (tuning.*ee::dsp::kGrainerTuningEntries[i].member, juce::dontSendNotification);

        pushToProcessor();
        refreshReadout();
    };
    addAndMakeVisible (resetButton);

    refreshReadout();
}

GrainTunerPanel::~GrainTunerPanel() = default;

void GrainTunerPanel::pushToProcessor()
{
    if (apply)
        apply (tuning);
}

void GrainTunerPanel::refreshReadout()
{
    juce::String text = "// shared/include/ee/dsp/GrainerTuning.h\n";

    for (const auto& entry : ee::dsp::kGrainerTuningEntries)
        text << "float " << entry.name << " = " << literal (tuning.*entry.member, entry.decimals) << ";\n";

    readout.setText (text, false);
}

void GrainTunerPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    g.drawText ("GRAIN TUNER  (dev build)", getLocalBounds().reduced (kPad).removeFromTop (18),
                juce::Justification::centredLeft, false);
}

void GrainTunerPanel::resized()
{
    auto area = getLocalBounds().reduced (kPad);
    area.removeFromTop (22);

    auto footer = area.removeFromBottom (kButtonHeight);
    copyButton.setBounds (footer.removeFromLeft (footer.getWidth() / 2 - 4));
    resetButton.setBounds (footer.removeFromRight (footer.getWidth() - 8));

    area.removeFromBottom (6);
    readout.setBounds (area.removeFromBottom (kReadoutHeight));
    area.removeFromBottom (6);

    viewport.setBounds (area);

    const int contentHeight = static_cast<int> (sliders.size()) * kRowHeight;
    rows.setSize (area.getWidth() - (contentHeight > area.getHeight() ? 10 : 0), contentHeight);

    auto content = rows.getLocalBounds();

    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto row = content.removeFromTop (kRowHeight);
        names[i]->setBounds (row.removeFromLeft (kNameWidth));
        sliders[i]->setBounds (row);
    }
}
