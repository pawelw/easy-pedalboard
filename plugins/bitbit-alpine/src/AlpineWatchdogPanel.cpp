#include "AlpineWatchdogPanel.h"

#include <cmath>

namespace
{
constexpr int kPad = 8;
constexpr int kButtonHeight = 24;
constexpr int kStatusHeight = 52;

juce::String dbString (float linear)
{
    if (linear <= 1.0e-6f)
        return "-inf";

    return juce::String (20.0f * std::log10 (linear), 1) + " dBFS";
}
} // namespace

AlpineWatchdogPanel::AlpineWatchdogPanel (juce::File logFile) : log (std::move (logFile))
{
    status.setJustificationType (juce::Justification::topLeft);
    status.setFont (juce::FontOptions (11.0f));
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    status.setText ("Safety net armed. No incidents yet.", juce::dontSendNotification);
    addAndMakeVisible (status);

    readout.setMultiLine (true);
    readout.setReadOnly (true);
    readout.setScrollbarsShown (true);
    readout.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    readout.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101216));
    readout.setColour (juce::TextEditor::textColourId, juce::Colour (0xffe0b060));
    readout.setText ("The last incident's black box will appear here.\n"
                     "When you hear the blow-up, hit Copy report and paste it into the chat.",
                     false);
    addAndMakeVisible (readout);

    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (readout.getText()); };
    addAndMakeVisible (copyButton);

    openButton.onClick = [this]
    {
        if (log.existsAsFile())
            log.revealToUser();
        else
            log.getParentDirectory().revealToUser();
    };
    addAndMakeVisible (openButton);
}

AlpineWatchdogPanel::~AlpineWatchdogPanel() = default;

void AlpineWatchdogPanel::showReport (const juce::String& text)
{
    readout.setText (text, false);
}

void AlpineWatchdogPanel::setCounters (int incidents, int blocksSanitised, int moduleResets, float outputPeakLinear)
{
    juce::String s;
    s << "incidents logged   " << incidents << "\n"
      << "blocks sanitised   " << blocksSanitised << "     module resets   " << moduleResets << "\n"
      << "output peak now     " << dbString (outputPeakLinear);

    status.setText (s, juce::dontSendNotification);
}

void AlpineWatchdogPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    g.drawText ("ALPINE WATCHDOG  (dev build)", getLocalBounds().reduced (kPad).removeFromTop (18),
                juce::Justification::centredLeft, false);
}

void AlpineWatchdogPanel::resized()
{
    auto area = getLocalBounds().reduced (kPad);
    area.removeFromTop (22);

    status.setBounds (area.removeFromTop (kStatusHeight));
    area.removeFromTop (6);

    auto footer = area.removeFromBottom (kButtonHeight);
    copyButton.setBounds (footer.removeFromLeft (footer.getWidth() / 2 - 4));
    openButton.setBounds (footer.removeFromRight (footer.getWidth() - 8));
    area.removeFromBottom (6);

    readout.setBounds (area);
}
