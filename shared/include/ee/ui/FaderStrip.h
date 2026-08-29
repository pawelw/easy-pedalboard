#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Radius of the round node that sits at the top of each fader stem. */
constexpr float kFaderNodeRadius = 9.0f;

/** Colour of the response curve joining the fader nodes, and of any value
    readout that is not sitting at 0. A deep Spotify-ish green. */
inline const juce::Colour kFaderCurveColour { 0xff1aa34a };

/** Vertical span (min y .. max y) the node travels through inside a fader's
    slider bounds. Shared by the look and feel and the editor so the drawn node
    and the curve that joins the nodes always line up. */
juce::Range<float> faderTrackRange (juce::Rectangle<float> sliderBounds) noexcept;

/** Vertical style fader plus the value readout and caption underneath it.

    Drawn as a graph node: a stem dropping to the baseline with a round handle
    at the value. The fader counterpart to Knob - same label block, same
    attachment wiring - so a pedal face can mix the two without special cases.
*/
class FaderStrip : public juce::Component
{
public:
    FaderStrip (juce::AudioProcessorValueTreeState& state,
                const SliderSpec& spec,
                const PedalTheme& theme);

    ~FaderStrip() override;

    /** Height of the value and caption rows below the fader. Matches Knob so a
        row of either lines up. */
    static constexpr int labelHeight = 32;

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

    /** Centre of the node handle, in the coordinate space of this component's
        parent - the point the joining curve passes through. */
    juce::Point<float> nodeCentreInParent();

private:
    void refreshValueText();

    /** Slider with a magnetic detent: a user drag that lands near 0 is pulled
        onto it. Programmatic moves (the attachment, automation, the reset
        button) are left alone. */
    struct DetentSlider : juce::Slider
    {
        double snapValue (double attemptedValue, DragMode dragMode) override;
    };

    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramID;
    juce::String captionText;
    const PedalTheme& pedalTheme;

    DetentSlider slider;
    juce::String valueText;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderStrip)
};

} // namespace ee::ui
