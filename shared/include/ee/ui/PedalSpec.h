#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace ee::ui
{

struct KnobSpec
{
    juce::String parameterID;
    juce::String caption;
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

    int width = 340;
    int height = 440;
};

} // namespace ee::ui
