#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "ee/plugin/CornerResizer.h"

// Off by default (see EE_JSUI_DEV_SERVER in cmake/AddBitBitPlugin.cmake): the
// face is served out of jsui/dist through the resource provider below, so an
// installed plugin renders in a DAW with nothing else running. ON points the
// browser at the Vite dev server instead, for hot reload while iterating on
// jsui/src - a dev-only build, never one you hand to anyone.
#ifndef EE_JSUI_DEV_SERVER
#define EE_JSUI_DEV_SERVER 0
#endif

#if EE_TAPE_TUNER
#include "TapeTunerPanel.h"
#endif

class BitBitDelayProcessor;

/** BitBit Delay's face, ported from the same juce::WebBrowserComponent + React
    approach as BitBit Wah's (see BitBitWahWebEditor) instead of ee::ui::PedalEditor.
    The Tape knob shares the same knob look as everything else here rather
    than the old face's distinct photographic cap - see jsui/README.md. */
class BitBitDelayWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BitBitDelayWebEditor (BitBitDelayProcessor&);
    ~BitBitDelayWebEditor() override;

    void resized() override;

    int getControlParameterIndex (Component&) override
    {
        return controlParameterIndexReceiver.getControlParameterIndex();
    }

private:
    /** Pushes the processor's input level, onset count and the host tempo to
        the page as the one "delayMeter" event - none of them is a parameter,
        so there is no relay to carry them. The TapScope animates off the first
        two and nothing else: no signal, no movement. Same shape as BitBit Wah's
        "filterMod" feed.

        Tempo rides along because a synced Time knob means a different number
        of milliseconds at every tempo, and the host can change tempo with
        nothing on the face moving at all - so the readouts beside the knobs
        had no way to hear about it and sat on a stale figure until something
        was touched. This feed is already running at 45 Hz for the scope; the
        page only re-reads the readouts when the number actually changes. */
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    BitBitDelayProcessor& processorRef;

    // Same range every resizable release pedal's window drags across - see
    // BitBitAlpineWebEditor's identical constants, the first of them.
    static constexpr float kMinZoom = 0.6f;
    static constexpr float kMaxZoom = 2.0f;

    // The size the page reported the one time it is measured - see
    // installResizableFace. 0 until that first report arrives.
    int baseWidth = 0;
    int baseHeight = 0;

    // Bottom-left grip, added once the first report above sizes the window -
    // see CornerResizer's own note on why bottom-left.
    std::unique_ptr<ee::plugin::CornerResizer> resizeGrip;

    // Where the page comes from - see EE_JSUI_DEV_SERVER above.
    static constexpr bool kUseDevServer = EE_JSUI_DEV_SERVER != 0;
    static const juce::String devServerAddress;

    juce::WebSliderRelay leftTimeRelay { "ltime" };
    juce::WebSliderRelay rightTimeRelay { "rtime" };
    juce::WebSliderRelay feedbackRelay { "fb" };
    juce::WebSliderRelay mixRelay { "mix" };
    // The footer's two stage sections: Tape (Wear + Flutter, on the repeats) and
    // Mod (Drift + Phaser).
    // "mod" and "tape" are the ids Drift and Wear kept from the single-knob
    // face - see PluginProcessor.cpp.
    juce::WebSliderRelay wearRelay { "tape" };
    juce::WebSliderRelay flutterRelay { "flutter" };
    juce::WebSliderRelay driftRelay { "mod" };
    juce::WebSliderRelay phaserRelay { "phaser" };

    // The footer's Filter section: the two cuts on the repeats.
    juce::WebSliderRelay loCutRelay { "locut" };
    juce::WebSliderRelay hiCutRelay { "hicut" };

    // The header's two faders, either end of the pedal.
    juce::WebSliderRelay inGainRelay { "ingain" };
    juce::WebSliderRelay outGainRelay { "outgain" };

    // The Delay Type button next to Sync. A combo relay rather than a toggle
    // one, because there are three positions rather than two; the page draws
    // its own labels for them (App.jsx).
    juce::WebComboBoxRelay typeRelay { "dtype" };

    juce::WebToggleButtonRelay syncRelay { "sync" };
    juce::WebToggleButtonRelay timeUnitRelay { "timeunit" };
    juce::WebToggleButtonRelay onRelay { "on" };

    juce::WebControlParameterIndexReceiver controlParameterIndexReceiver;

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

    juce::WebSliderParameterAttachment leftTimeAttachment;
    juce::WebSliderParameterAttachment rightTimeAttachment;
    juce::WebSliderParameterAttachment feedbackAttachment;
    juce::WebSliderParameterAttachment mixAttachment;
    juce::WebSliderParameterAttachment wearAttachment;
    juce::WebSliderParameterAttachment flutterAttachment;
    juce::WebSliderParameterAttachment driftAttachment;
    juce::WebSliderParameterAttachment phaserAttachment;
    juce::WebSliderParameterAttachment loCutAttachment;
    juce::WebSliderParameterAttachment hiCutAttachment;
    juce::WebSliderParameterAttachment inGainAttachment;
    juce::WebSliderParameterAttachment outGainAttachment;

    juce::WebComboBoxParameterAttachment typeAttachment;

    juce::WebToggleButtonParameterAttachment syncAttachment;
    juce::WebToggleButtonParameterAttachment timeUnitAttachment;
    juce::WebToggleButtonParameterAttachment onAttachment;

#if EE_TAPE_TUNER
    // Flip showTuner in the .cpp to bring this back without reconfiguring
    // CMake - same switch the old ee::ui editor had.
    std::unique_ptr<TapeTunerPanel> tunerPanel;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBitDelayWebEditor)
};
