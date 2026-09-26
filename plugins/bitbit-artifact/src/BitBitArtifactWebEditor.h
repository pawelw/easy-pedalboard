#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "ee/plugin/CompMeterFeed.h"
#include "ee/plugin/CornerResizer.h"
#include "ee/plugin/RelaySet.h"

// Off by default (see EE_JSUI_DEV_SERVER in cmake/AddBitBitPlugin.cmake): the
// face is served out of jsui/dist through the resource provider below, so an
// installed plugin renders in a DAW with nothing else running. ON points the
// browser at the Vite dev server instead, for hot reload while iterating on
// jsui/src - a dev-only build, never one you hand to anyone.
#ifndef EE_JSUI_DEV_SERVER
#define EE_JSUI_DEV_SERVER 0
#endif

class BitBitArtifactProcessor;

/** BitBit Artifact's face: the same juce::WebBrowserComponent + React arrangement
    BitBit Wah and BitBit Delay use.

    The parameters come from `ee::plugin::RelaySet`, which walks the processor's
    own list and builds the right relay for each - there is no parameter list in
    this file, so adding one to the layout binds it with no editor change.

    One live feed, off a Timer: "compMeter", the Comp engine's gain-reduction
    display (ee::plugin::CompMeterFeed) - sent only while Comp is selected. */
class BitBitArtifactWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BitBitArtifactWebEditor (BitBitArtifactProcessor&);
    ~BitBitArtifactWebEditor() override;

    void resized() override;

    int getControlParameterIndex (Component&) override { return relays.getControlParameterIndex(); }

private:
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    ee::plugin::CompMeterFeed compMeter;

    BitBitArtifactProcessor& processorRef;

    // Same range every resizable release pedal's window drags across - see
    // BitBitAlpineWebEditor's identical constants, the first of them.
    static constexpr float kMinZoom = 0.6f;
    static constexpr float kMaxZoom = 2.0f;

    // The size the page reported the one time it is measured - see
    // installResizableFace. 0 until that first report arrives.
    int baseWidth = 0;
    int baseHeight = 0;

    // The bottom-left resize grip's native half - the page draws the grip
    // and drives this. Declared before the WebView, whose options it adds to;
    // see CornerResizer.
    ee::plugin::CornerResizer resizeGrip { *this };

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBitArtifactWebEditor)
};
