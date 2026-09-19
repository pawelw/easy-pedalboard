#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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

/** The version a tree was written with; 0 if it carries none. */
inline int stateVersionOf (const juce::ValueTree& state)
{
    return static_cast<int> (state.getProperty (kStateVersionProp, 0));
}

} // namespace ee::plugin
