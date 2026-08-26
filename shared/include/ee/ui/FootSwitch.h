#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Stomp switch with an LED above it, bound to a boolean parameter. */
class FootSwitch : public juce::Button
{
public:
    FootSwitch (juce::AudioProcessorValueTreeState& state,
                const juce::String& parameterID,
                const PedalTheme& theme);

    ~FootSwitch() override;

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    const PedalTheme& pedalTheme;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FootSwitch)
};

} // namespace ee::ui
