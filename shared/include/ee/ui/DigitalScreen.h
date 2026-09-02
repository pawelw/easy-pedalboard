#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalTheme.h"

#include <initializer_list>

namespace ee::ui

{

/** The soft-UI display: a pale recessed card carrying a dB grid, decade rules
    and their captions.

    Chrome only. `paintPanel` hands back the plot rect and the caller draws its
    own trace into it, so one screen serves a filter response, a spectrum or an
    LFO preview without this knowing which. The grid helpers take the same plot
    rect and the same axis limits the trace is mapped with, which is what keeps
    a curve on its gridline.
*/
class DigitalScreen
{
public:
    /** Paints the recessed card over `bounds` and returns the rect inside it
        that the trace and the grid share. */
    static juce::Rectangle<float> paintPanel (juce::Graphics& g,
                                              juce::Rectangle<float> bounds,
                                              const PedalTheme& theme);

    /** Horizontal rules at the given levels, dashed away from 0 dB and solid
        at it, each captioned in the gutter to the left of the plot.
        `dbFloor` / `dbCeil` are the same limits the caller maps its trace
        with. */
    static void paintLevelGrid (juce::Graphics& g,
                                juce::Rectangle<float> plot,
                                const PedalTheme& theme,
                                float dbFloor,
                                float dbCeil,
                                std::initializer_list<float> levelsDb);

    /** Vertical rules on a log-frequency axis running `fMinHz` to `fMaxHz`,
        each captioned in the gutter below the plot ("500Hz", "10kHz"). */
    static void paintFrequencyGrid (juce::Graphics& g,
                                    juce::Rectangle<float> plot,
                                    const PedalTheme& theme,
                                    float fMinHz,
                                    float fMaxHz,
                                    std::initializer_list<float> marksHz);

    /** Room the captions need outside the plot. Both grid helpers draw their
        labels beyond the plot rect - to its left and below it - so the caller
        trims these off the panel before it maps a trace, and nothing it draws
        can land on a label. */
    static constexpr float kLabelGutterLeft = 26.0f;
    static constexpr float kLabelGutterBottom = 13.0f;

    DigitalScreen() = delete;
};

} // namespace ee::ui
