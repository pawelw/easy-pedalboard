#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"
#include "ee/ui/SwitchControl.h"

namespace ee::ui
{

/** The soft-UI two-way switch: a pill track with a white knob resting against
    one end, a caption either side of it.

    The track carries the state - pale and recessed when the knob is left, solid
    charcoal when it is right - and the two captions dim on whichever side the
    knob is not. Latching, bound to a bool parameter.

    The digital counterpart of `SlideToggle`; both satisfy `SwitchControl`, so
    the editor lays either out without knowing which it built.
*/
class DigitalSwitch : public juce::Button,
                      public SwitchControl
{
public:
    /** How big the pill is drawn. `compact` is for a switch tucked under a
        knob, where the full-size one would be wider than the column. */
    enum class Size
    {
        full,
        compact
    };

    DigitalSwitch (juce::AudioProcessorValueTreeState& state,
                   const SlideToggleSpec& spec,
                   const PedalTheme& theme,
                   Size size = Size::full);

    ~DigitalSwitch() override;

    int switchWidth() const override;
    int switchHeight() const override;
    int switchLabelInset() const override;
    int switchTrackOffset() const override;

protected:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Font labelFont() const;
    float trackWidth() const;
    float labelWidth (const juce::String&) const;

    /** Where the knob rests: normally the parameter itself, flipped when the
        spec asked for the "on" label on the left. */
    bool knobIsRight() const;

    const PedalTheme& pedalTheme;
    juce::String labelLeft;
    juce::String labelRight;
    juce::Colour accent;
    juce::Colour labelColour;
    Size size;
    bool flushLeft = false;
    bool inverted = false;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitalSwitch)
};

} // namespace ee::ui
