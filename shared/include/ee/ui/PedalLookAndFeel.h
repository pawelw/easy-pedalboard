#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Draws knobs from a PedalTheme, preferring a filmstrip image when one is set. */
class PedalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit PedalLookAndFeel (PedalTheme themeToUse);

    void setTheme (PedalTheme newTheme);
    const PedalTheme& getTheme() const noexcept { return theme; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    /** Small pill button: solid fill from TextButton::buttonColourId, label from
        the text colour ids, fully rounded. */
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    /** Two faint 45-degree strokes in the bottom-right corner, just inside where
        the pedal's drop shadow fades - the resize grip. */
    void drawCornerResizer (juce::Graphics&, int w, int h,
                            bool isMouseOver, bool isMouseDragging) override;

private:
    void applyColours();

    PedalTheme theme;
};

} // namespace ee::ui
