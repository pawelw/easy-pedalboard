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

private:
    void applyColours();

    PedalTheme theme;
};

} // namespace ee::ui
