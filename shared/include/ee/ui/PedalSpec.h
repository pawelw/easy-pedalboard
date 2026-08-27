#pragma once

#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <vector>

namespace ee::ui
{

struct KnobSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Override the theme's cap colours, for a control that is not really part
        of the same effect as the rest of the row. */
    std::optional<juce::Colour> capFill;
    std::optional<juce::Colour> capBorder;
};

/** A small latching button tucked into the gap between two knobs of a row. */
struct ToggleSpec
{
    juce::String parameterID;
    juce::String caption;

    /** Index into `knobs`; the button sits to the right of that knob. Ignored
        if the next knob starts a new row. */
    int afterKnobIndex = 0;
};

/** Declarative description of a pedal face. Add an effect by writing one of these. */
struct PedalSpec
{
    juce::String name;
    juce::String tagline;
    juce::String version;
    std::vector<KnobSpec> knobs;
    std::vector<ToggleSpec> toggles;

    int knobsPerRow = 3;

    /** Height is shared across pedals so they line up side by side on a rack;
        only the width follows the number of knobs in a row. */
    int width = 340;
    int height = 478;
};

} // namespace ee::ui
