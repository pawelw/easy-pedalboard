#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

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

#ifndef EE_ALPINE_WATCHDOG
#define EE_ALPINE_WATCHDOG 0
#endif

#if EE_ALPINE_WATCHDOG
#include "AlpineWatchdog.h"
#include "AlpineWatchdogPanel.h"
#endif

class BitBitAlpineProcessor;

/** BitBit Alpine's face: the same juce::WebBrowserComponent + React arrangement
    BitBit Wah and BitBit Delay use.
 *
 * The difference is the parameter count. BitBit Delay declares its seventeen
 * relays and seventeen attachments by hand; this plugin has forty-six, so they
 * come from `ee::plugin::RelaySet`, which builds them off the processor's own
 * parameter list. There is no list of parameters in this file at all - add one
 * to the layout and the face can bind it without touching the editor.
 */
class BitBitAlpineWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BitBitAlpineWebEditor (BitBitAlpineProcessor&);
    ~BitBitAlpineWebEditor() override;

    void resized() override;

    int getControlParameterIndex (Component&) override { return relays.getControlParameterIndex(); }

private:
    /** Pushes two per-frame feeds the pages listen for, neither a parameter so
        neither has a relay:

        - "delayMeter": the input level, note-onset count and host tempo, for the
          Delay module's scope and the chrome's tempo readout.
        - "filterMod": the Modulation module's Filter engine live modL / modR,
          for its response scope.

        Both keep the names their own pedals' editors emit (BitBit Delay's and
        BitBit Wah's): one feed per editor, not one per module. */
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    BitBitAlpineProcessor& processorRef;

    // How far the window can be dragged from the size the page first reported -
    // the same range PedalEditor's native faces use, so every pedal's window
    // resizes by the same feel. Unlike those, this one opens at exactly the
    // reported size (scale 1) rather than kDefaultZoom - reportContentSize's
    // very first call is what used to size the window outright, and still does.
    static constexpr float kMinZoom = 0.6f;
    static constexpr float kMaxZoom = 2.0f;

    // The size the page reported the one time it is measured - see
    // installResizableFace. 0 until that first report arrives.
    int baseWidth = 0;
    int baseHeight = 0;

    // Bottom-left rather than juce::ResizableCornerComponent's fixed
    // bottom-right - see CornerResizer's own note; every resizable release
    // pedal shares this one grip, this being the first of them.
    std::unique_ptr<ee::plugin::CornerResizer> resizeGrip;

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

#if EE_ALPINE_WATCHDOG
    /** Drains the processor's watchdog on the timer: any newly frozen incident
        is formatted, appended to the log file, and shown on the panel. */
    void pollWatchdog();
    juce::String formatIncident (const AlpineWatchdog::Incident&) const;

    std::unique_ptr<AlpineWatchdogPanel> watchdogPanel;
    juce::StringArray watchdogParamIds; // getParameters() order, matches the snapshot
    juce::File watchdogLog;
    juce::uint32 lastWatchdogSeq = 0;
    int watchdogIncidents = 0;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBitAlpineWebEditor)
};
