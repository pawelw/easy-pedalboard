#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Development-only side panel for the output-safety watchdog (EE_ALPINE_WATCHDOG).

    Shows the black-box record of the last time the safety net had to step in,
    with a Copy button so it can go straight into a bug report, and an Open log
    button for the running file every incident is also appended to. Modelled on
    Peak Delay's TapeTunerPanel. */
class AlpineWatchdogPanel : public juce::Component
{
public:
    static constexpr int preferredWidth = 380;

    explicit AlpineWatchdogPanel (juce::File logFile);
    ~AlpineWatchdogPanel() override;

    /** Replace the readout with the newest incident report. */
    void showReport (const juce::String& text);

    /** Refresh the running tally line. */
    void setCounters (int incidents, int blocksSanitised, int moduleResets, float outputPeakLinear);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::File log;

    juce::Label status;
    juce::TextEditor readout;
    juce::TextButton copyButton { "Copy report" };
    juce::TextButton openButton { "Open log" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlpineWatchdogPanel)
};
