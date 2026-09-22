#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

#include "ee/plugin/SafeParse.h"

namespace ee::plugin
{

/** The version of the saved-state format, stamped onto the root of the APVTS
    tree every time a pedal writes one out - a DAW project (getStateInformation)
    and a preset file (PresetStore) alike.

    Nothing reads it yet. It is here so that the release which has to migrate
    state - a renamed parameter, a knob whose range means something different -
    can tell the files 1.0 wrote from the ones it writes itself. A tree with no
    stamp at all is version 0: written before the stamp existed, or by hand.

    Bump it only when a change to the saved state needs a migration on load, and
    write that migration where the state is installed (`installState`). Do not
    bump it for a new parameter: APVTS already fills a missing one from its
    default. */
inline constexpr int kStateVersion = 1;
inline constexpr const char* kStateVersionProp = "stateVersion";

/** `apvts.copyState()` with the version stamped on. Every write of a pedal's
    state goes through this rather than calling copyState() directly. */
inline juce::ValueTree copyVersionedState (juce::AudioProcessorValueTreeState& apvts)
{
    auto state = apvts.copyState();
    state.setProperty (kStateVersionProp, kStateVersion, nullptr);
    return state;
}

/** `AudioProcessor::getXmlFromBinary`, for a session a host hands back. Scans
    the raw bytes, framing included, before JUCE's parser is let near them. */
inline std::unique_ptr<juce::XmlElement> xmlFromBinary (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0 || (size_t) sizeInBytes > kMaxStateBytes
        || ! xmlNestingWithin (static_cast<const char*> (data), (size_t) sizeInBytes))
        return nullptr;

    return juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes);
}

/** A tree that came from outside, made safe to hand to `replaceState`.

    APVTS writes whatever a parameter's `value` property parses to straight into
    the parameter, and juce::String parses "nan" to NaN. A NaN in a parameter
    reaches the DSP as a NaN delay time or cutoff, which is a silent plugin or a
    stuck one until the instance is reloaded - from a text file a user was
    editing, or a session written by something that had a NaN of its own. So a
    parameter whose value is not a finite number is put back to its default here,
    and one that is merely out of range is left for APVTS, which clamps it.

    Only touches what `replaceState` would read: children with an `id` that names
    a real parameter. Everything else is left alone, unknown ids included -
    APVTS ignores them. */
inline juce::ValueTree sanitisedState (juce::ValueTree tree, juce::AudioProcessorValueTreeState& apvts)
{
    for (auto child : tree)
    {
        const auto id = child.getProperty ("id").toString();
        auto* parameter = id.isNotEmpty() ? apvts.getParameter (id) : nullptr;

        if (parameter == nullptr || ! child.hasProperty ("value"))
            continue;

        if (! std::isfinite (static_cast<double> (child.getProperty ("value"))))
            child.setProperty ("value", parameter->convertFrom0to1 (parameter->getDefaultValue()), nullptr);
    }

    return tree;
}

/** The version a tree was written with; 0 if it carries none. */
inline int stateVersionOf (const juce::ValueTree& state)
{
    return static_cast<int> (state.getProperty (kStateVersionProp, 0));
}

} // namespace ee::plugin
