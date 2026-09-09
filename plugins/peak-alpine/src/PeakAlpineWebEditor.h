#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "ee/plugin/RelaySet.h"

// Off by default (see EE_JSUI_DEV_SERVER in cmake/AddPeakPlugin.cmake): the
// face is served out of jsui/dist through the resource provider below, so an
// installed plugin renders in a DAW with nothing else running. ON points the
// browser at the Vite dev server instead, for hot reload while iterating on
// jsui/src - a dev-only build, never one you hand to anyone.
#ifndef EE_JSUI_DEV_SERVER
#define EE_JSUI_DEV_SERVER 0
#endif

class PeakAlpineProcessor;

/** Peak Alpine's face: the same juce::WebBrowserComponent + React arrangement
    Peak Wah and Peak Delay use.
 *
 * The difference is the parameter count. Peak Delay declares its seventeen
 * relays and seventeen attachments by hand; this plugin has forty-six, so they
 * come from `ee::plugin::RelaySet`, which builds them off the processor's own
 * parameter list. There is no list of parameters in this file at all - add one
 * to the layout and the face can bind it without touching the editor.
 */
class PeakAlpineWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PeakAlpineWebEditor (PeakAlpineProcessor&);
    ~PeakAlpineWebEditor() override;

    void resized() override;

    int getControlParameterIndex (Component&) override { return relays.getControlParameterIndex(); }

private:
    /** Pushes two per-frame feeds the pages listen for, neither a parameter so
        neither has a relay:

        - "delayMeter": the input level, note-onset count and host tempo, for the
          Delay module's scope and the chrome's tempo readout.
        - "filterMod": the Artifact module's Filter engine live modL / modR, for
          its response scope.

        Both keep the names their own pedals' editors emit - the faces inside
        the modules are those pedals' faces, listening for exactly those: one
        feed per editor, not one per module. */
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PeakAlpineProcessor& processorRef;

    // Where the page comes from - see EE_JSUI_DEV_SERVER above.
    static constexpr bool kUseDevServer = EE_JSUI_DEV_SERVER != 0;
    static const juce::String devServerAddress;

    /** Declared before `webView`: its relays have to exist before they can be
        handed to the browser's Options. See RelaySet's own note. */
    ee::plugin::RelaySet relays;

    struct SinglePageBrowser : juce::WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;
        bool pageAboutToLoad (const juce::String& newURL) override;
        bool pageLoadHadNetworkError (const juce::String& errorInfo) override;

        /** One shot. Only a dev-server build can reach the error path at all,
            and the page it falls back to is served locally so it cannot fail
            the same way - but goToURL() from inside that callback is
            documented as loopable, so this makes a loop impossible. */
        bool triedFallback = false;
    };

    SinglePageBrowser webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakAlpineWebEditor)
};
