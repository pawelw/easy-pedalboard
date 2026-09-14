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

class PeakModulationProcessor;

/** Peak Modulation's face: the same juce::WebBrowserComponent + React
    arrangement Peak Artifact uses.

    The parameters come from `ee::plugin::RelaySet`, which walks the processor's
    own list and builds the right relay for each - there is no parameter list in
    this file, so adding one to the layout binds it with no editor change. */
class PeakModulationWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PeakModulationWebEditor (PeakModulationProcessor&);
    ~PeakModulationWebEditor() override;

    void resized() override;

    int getControlParameterIndex (Component&) override { return relays.getControlParameterIndex(); }

private:
    /** Pushes the Filter engine's live modL / modR as one "filterMod" event, for
        its response scope - not a parameter, so it has no relay. */
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PeakModulationProcessor& processorRef;

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

        bool triedFallback = false;
    };

    SinglePageBrowser webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakModulationWebEditor)
};
