#pragma once

#include "ee/plugin/SafeParse.h"

#include <juce_core/juce_core.h>

#include <vector>

#include "ee/plugin/ModRouter.h"

namespace ee::plugin
{

/** The wire format between the Mod tab's JS drag-and-drop routing and
    ee::plugin::ModRouter: a plain array, `[{"paramId": "...", "depth": ...}, ...]`.
    No version wrapper - unlike the breakpoint shape (LfoBreakpointJson.h)
    there is no evaluation model to keep in step, just a list of pairs, so
    there is nothing a version number would ever need to describe. */

inline juce::String modRoutingToJson (const std::vector<ModAssignment>& assignments)
{
    juce::Array<juce::var> array;
    for (const auto& a : assignments)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("paramId", a.paramID);
        obj->setProperty ("depth", a.depth);
        array.add (juce::var (obj));
    }
    return juce::JSON::toString (juce::var (array), true);
}

inline std::vector<ModAssignment> modRoutingFromJson (const juce::String& json)
{
    std::vector<ModAssignment> assignments;
    if (json.isEmpty())
        return assignments;

    const auto parsed = parseJson (json);
    if (auto* array = parsed.getArray())
    {
        for (const auto& item : *array)
        {
            if (auto* obj = item.getDynamicObject())
            {
                ModAssignment a;
                a.paramID = obj->getProperty ("paramId").toString();
                a.depth = static_cast<float> (obj->getProperty ("depth"));
                if (a.paramID.isNotEmpty())
                    assignments.push_back (a);
            }
        }
    }

    return assignments;
}

} // namespace ee::plugin
