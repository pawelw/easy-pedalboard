#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"
#include "ee/ui/SwitchControl.h"

namespace ee::ui
{

/** Small latching button, sized to drop into the gap between two knobs.

    Drawn as a lit bezel around a black face: the frame and the legend carry the
    colour while it is on, and lose it when it is off.
*/
class MiniToggle : public juce::Button,
                   public SwitchControl
{
public:
    MiniToggle (juce::AudioProcessorValueTreeState& state,
                const ToggleSpec& spec,
                const PedalTheme& theme);

    ~MiniToggle() override;

    static constexpr int preferredWidth = 46;
    static constexpr int preferredHeight = 24;

    int switchWidth() const override  { return preferredWidth; }
    int switchHeight() const override { return preferredHeight; }

protected:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const PedalTheme& pedalTheme;
    juce::String captionText;
    juce::Colour litColour;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniToggle)
};

} // namespace ee::ui
