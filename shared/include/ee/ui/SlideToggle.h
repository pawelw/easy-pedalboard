#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"
#include "ee/ui/SwitchControl.h"

namespace ee::ui
{

/** A large two-way sliding switch: a label either side of a dark rounded track
    with a light knob that rests left (parameter false) or right (true). The
    whole thing toggles on click. Latching, bound to a bool parameter. */
class SlideToggle : public juce::Button,
                    public SwitchControl
{
public:
    SlideToggle (juce::AudioProcessorValueTreeState& state,
                 const SlideToggleSpec& spec,
                 const PedalTheme& theme);

    ~SlideToggle() override;

    static constexpr int preferredWidth = 164;
    static constexpr int preferredHeight = 26;

    /** Blank space between the component's left edge and the first letter of the
        off label - what a caller has to shift the switch left by to line that
        letter up with something below it. 0 unless `labelFlushLeft` is set. */
    int labelInset() const;

    int switchWidth() const override       { return preferredWidth; }
    int switchHeight() const override      { return preferredHeight; }
    int switchLabelInset() const override  { return labelInset(); }

protected:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const PedalTheme& pedalTheme;
    juce::String labelOff;
    juce::String labelOn;
    juce::Colour accent;
    juce::Colour labelColour;
    bool flushLeft = false;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlideToggle)
};

} // namespace ee::ui
