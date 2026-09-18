#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ee/dsp/BreakpointLfo.h"

namespace ee::plugin
{

/** The wire format between the Mod tab's JS breakpoint editor and
    ee::dsp::BreakpointLfo: `{ "version": 1, "points": [{x,y,curve,hold}, ...] }`.
    Kept out of BreakpointLfo.h itself so that header stays juce::var-free,
    the same way ee::dsp::Tremolo/RateMap never touch JUCE's variant type -
    this is bridge/persistence glue, not DSP. */

inline juce::String lfoBreakpointsToJson (const std::vector<ee::dsp::LfoBreakpoint>& points)
{
    juce::Array<juce::var> array;
    for (const auto& p : points)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("x", p.x);
        obj->setProperty ("y", p.y);
        obj->setProperty ("curve", p.curve);
        obj->setProperty ("hold", p.hold);
        array.add (juce::var (obj));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("points", array);
    return juce::JSON::toString (juce::var (root), true);
}

inline std::vector<ee::dsp::LfoBreakpoint> lfoBreakpointsFromJson (const juce::String& json)
{
    std::vector<ee::dsp::LfoBreakpoint> points;
    if (json.isEmpty())
        return points;

    const auto parsed = juce::JSON::parse (json);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return points;

    if (auto* array = root->getProperty ("points").getArray())
    {
        for (const auto& item : *array)
        {
            if (auto* obj = item.getDynamicObject())
            {
                ee::dsp::LfoBreakpoint p;
                p.x = static_cast<float> (obj->getProperty ("x"));
                p.y = static_cast<float> (obj->getProperty ("y"));
                p.curve = static_cast<float> (obj->getProperty ("curve"));
                p.hold = static_cast<bool> (obj->getProperty ("hold"));
                points.push_back (p);
            }
        }
    }

    return points;
}

} // namespace ee::plugin
