#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "ee/plugin/PresetStore.h"

namespace ee::plugin
{

/** The five native functions a WebView face's preset bar talks to, added to a
    juce::WebBrowserComponent::Options in one call.

    A web face's preset bar is the same control on every pedal, so its bridge
    should be too: a pedal's editor writes

        webView (ee::plugin::presetBridge (juce::WebBrowserComponent::Options {}
                                               .withNativeIntegrationEnabled()
                                               ... ,
                                           processorRef.presets,
                                           EE_PRESET_SOURCE_DIR))

    and gets browsing, loading, saving and deleting. Nothing else per pedal.

    Relays would have been the obvious mechanism and are the wrong one: a relay
    carries one parameter's value, and none of this is a parameter. Loading a
    preset replaces the whole APVTS tree, which every attachment already hears
    about on its own, so the knobs redraw without this having to say anything
    about them.

    **Every call answers with the whole list**, current selection included,
    rather than with a bare ok/failed. A save changes the list, a load changes
    the selection and a delete changes both, so a page that had to ask again
    after each one would be showing a stale bar for a round trip - and would
    have to remember to ask at all. One shape of answer, always current.

    Called on the message thread by the WebView; PresetStore is message-thread
    only, which is the same thread. */
namespace presetbridge
{

inline juce::var listPayload (PresetStore& presets, bool canAuthor)
{
    auto toArray = [] (const juce::StringArray& names)
    {
        juce::Array<juce::var> out;
        for (const auto& n : names)
            out.add (n);
        return juce::var (out);
    };

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("factory", toArray (presets.factoryNames()));
    payload->setProperty ("user", toArray (presets.userNames()));
    payload->setProperty ("currentKind", presets.currentKind() == PresetStore::Kind::user ? "user" : "factory");
    payload->setProperty ("currentName", presets.currentName());

    // Drives whether the save dialog offers its third button at all. A page
    // cannot be trusted to decide this for itself - it is the build that knows,
    // and a release build answers false, so the dialog it draws has two buttons.
    payload->setProperty ("canAuthor", canAuthor);

    return juce::var (payload);
}

inline juce::var resultPayload (PresetStore& presets, bool canAuthor, const juce::Result& result)
{
    auto payload = listPayload (presets, canAuthor);
    payload.getDynamicObject()->setProperty ("ok", result.wasOk());
    payload.getDynamicObject()->setProperty ("error", result.getErrorMessage());
    return payload;
}

inline PresetStore::Kind kindFrom (const juce::var& v)
{
    return v.toString() == "user" ? PresetStore::Kind::user : PresetStore::Kind::factory;
}

} // namespace presetbridge

/** `presetSourceDirectory` is the pedal's own `presets/` folder in the source
    tree - EE_PRESET_SOURCE_DIR, which cmake defines for every pedal. It is only
    ever read by the author-only save, and only in a build configured with
    EE_PRESET_AUTHOR=ON. */
inline juce::WebBrowserComponent::Options presetBridge (juce::WebBrowserComponent::Options options,
                                                        PresetStore& presets,
                                                        const juce::String& presetSourceDirectory = {})
{
#if EE_PRESET_AUTHOR
    constexpr bool canAuthor = true;
#else
    constexpr bool canAuthor = false;
#endif

    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;
    using namespace presetbridge;

    return options
        // The page asks for this once on mount, and gets the same shape back
        // from every call below - so nothing else ever has to poll.
        .withNativeFunction ("presetList",
                             [&presets] (const juce::Array<juce::var>&, Completion complete)
                             {
                                 // Re-read the folder rather than trusting the
                                 // cached list: a second instance of the same
                                 // plugin may have saved something since this
                                 // editor last looked, and reopening a face is
                                 // exactly when someone expects to see it.
                                 presets.rescan();
                                 complete (listPayload (presets, canAuthor));
                             })

        .withNativeFunction ("presetLoad",
                             [&presets] (const juce::Array<juce::var>& args, Completion complete)
                             {
                                 const auto ok = args.size() >= 2
                                                 && presets.load (kindFrom (args[0]), args[1].toString());

                                 complete (resultPayload (presets, canAuthor,
                                                          ok ? juce::Result::ok()
                                                             : juce::Result::fail ("That preset is no longer there.")));
                             })

        // One entry point for both save buttons: `kind` picks which bank, the
        // same way it does for a load. "factory" is refused outright unless the
        // build allows authoring, so a page that asked for it anyway - a stale
        // dist, someone poking at the bridge - cannot write into the source
        // tree of a release build.
        .withNativeFunction ("presetSave",
                             [&presets, presetSourceDirectory] (const juce::Array<juce::var>& args,
                                                                Completion complete)
                             {
                                 if (args.size() < 2)
                                     return complete (
                                         resultPayload (presets, canAuthor, juce::Result::fail ("No preset name.")));

                                 const auto name = args[1].toString();

                                 const auto result =
                                     kindFrom (args[0]) == PresetStore::Kind::factory
                                         ? (canAuthor ? presets.saveFactorySource (name,
                                                                                   juce::File (presetSourceDirectory))
                                                      : juce::Result::fail ("This build cannot write factory presets."))
                                         : presets.saveUser (name);

                                 complete (resultPayload (presets, canAuthor, result));
                             })

        // The prev / next arrows. A fifth function rather than the page
        // walking its own two lists, because "what comes after the last
        // factory preset" is an ordering decision and the store already
        // makes it - see PresetStore::step. Two copies of that would drift.
        .withNativeFunction ("presetStep",
                             [&presets] (const juce::Array<juce::var>& args, Completion complete)
                             {
                                 presets.step (args.size() >= 1 ? (int) args[0] : 1);
                                 complete (listPayload (presets, canAuthor));
                             })

        // User presets only. A factory preset is in the binary; there is
        // nothing on disk to remove and the next launch would bring it back.
        .withNativeFunction ("presetDelete",
                             [&presets] (const juce::Array<juce::var>& args, Completion complete)
                             {
                                 const auto ok = args.size() >= 1 && presets.deleteUser (args[0].toString());

                                 complete (resultPayload (presets, canAuthor,
                                                          ok ? juce::Result::ok()
                                                             : juce::Result::fail ("Could not delete that preset.")));
                             });
}

} // namespace ee::plugin
