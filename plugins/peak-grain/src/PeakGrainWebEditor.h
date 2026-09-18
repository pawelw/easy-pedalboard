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

#if EE_GRAIN_TUNER
#include "GrainTunerPanel.h"
#endif

class PeakGrainProcessor;

/** Peak Grain's face: the same juce::WebBrowserComponent + React arrangement
    Peak Reverb uses (ee::plugin::RelaySet, which walks the processor's own
    parameter list and builds the right relay for each - there is no relay
    list in this file, so adding a parameter to the layout binds it with no
    editor change).

    Two things ride along on top of RelaySet, both lifted from Peak Delay's
    own web editor: formatKnobValue special-cases the four tempo-synced knobs
    (Size, Density, the two delay times), whose printed text depends on the
    Sync switch and the host tempo and so isn't the parameter's own
    stringFromValue; and a low-rate timer re-emits the host tempo so those
    readouts refresh even when the host's tempo changes with nothing on the
    face touched. */
class PeakGrainWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PeakGrainWebEditor (PeakGrainProcessor&);
    ~PeakGrainWebEditor() override;

    void resized() override;

    int getControlParameterIndex (Component&) override { return relays.getControlParameterIndex(); }

private:
    /** Re-emits the host tempo as a "grainTempo" event at a rate that's
        plenty for a number nobody is watching move in real time - Grain's
        scope is still and knob-tracking only, unlike Delay's, so there's no
        45 Hz meter feed to ride along with here. Also re-emits the Mod LFO's
        phase ("lfoPhase") for the Mod tab's live playhead marker, and, only
        when either has actually changed, the breakpoint shape
        ("lfoBreakpoints") and the drag-and-drop routing ("lfoRouting") - see
        lastLfoGeneration. */
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PeakGrainProcessor& processorRef;

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

    /** The last lfoStateGeneration() this editor saw - a preset load (or a
        host session restore) bumps the processor's own counter once for both
        the breakpoints and the routing, and the timer resends
        "lfoBreakpoints"/"lfoRouting" only when it moves, rather than every
        tick regardless. -1 so the very first tick always sends once. */
    int lastLfoGeneration = -1;

#if EE_GRAIN_TUNER
    // Flip showTuner in the .cpp to bring this back without reconfiguring
    // CMake - same switch Peak Delay's web editor has.
    std::unique_ptr<GrainTunerPanel> tunerPanel;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakGrainWebEditor)
};
