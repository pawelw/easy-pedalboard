#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** A live filter-response scope: magnitude versus log frequency.

    Draws a static "base" resonant bump at the Freq setting plus two moving
    bumps whose peak frequency slides with a per-channel modulator (the LFO in
    Peak Wah), all fed from the closures in a `FilterScopeSpec`. Peak height and
    width track resonance, and a dot marks the base bump's apex. Repaints on its
    own 45 fps clock so the moving curves animate without a parameter listener.
*/
class FilterScope : public juce::Component,
                    private juce::Timer
{
public:
    FilterScope (const FilterScopeSpec& spec, const PedalTheme& theme);
    ~FilterScope() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void drawBump (juce::Graphics&, juce::Rectangle<float> plot,
                   float fcHz, float peakDb, float bw,
                   juce::Colour colour, float thickness) const;

    FilterScopeSpec spec;
    PedalTheme theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterScope)
};

} // namespace ee::ui
