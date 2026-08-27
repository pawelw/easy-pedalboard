#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Small latching pill button, sized to drop into the gap between two knobs. */
class MiniToggle : public juce::Button
{
public:
    MiniToggle (juce::AudioProcessorValueTreeState& state,
                const juce::String& parameterID,
                const juce::String& caption,
                const PedalTheme& theme);

    ~MiniToggle() override;

    static constexpr int preferredWidth = 42;
    static constexpr int preferredHeight = 22;

protected:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const PedalTheme& pedalTheme;
    juce::String captionText;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniToggle)
};

} // namespace ee::ui
