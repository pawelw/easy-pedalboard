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

/** Declarative description of a pedal face. Add an effect by writing one of these. */
struct PedalSpec
{
    juce::String name;
    juce::String tagline;
    juce::String version;
    std::vector<KnobSpec> knobs;
    juce::String bypassParameterID;

    int knobsPerRow = 3;

    int width = 340;
    int height = 440;
};

} // namespace ee::ui
