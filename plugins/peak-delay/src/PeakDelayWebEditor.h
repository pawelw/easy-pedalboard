#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

// Off by default (see EE_JSUI_DEV_SERVER in cmake/AddPeakPlugin.cmake): the
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

class PeakDelayProcessor;

/** Peak Delay's face, ported from the same juce::WebBrowserComponent + React
    approach as Peak Wah's (see PeakWahWebEditor) instead of ee::ui::PedalEditor.
    The Tape knob shares the same knob look as everything else here rather
    than the old face's distinct photographic cap - see jsui/README.md. */
class PeakDelayWebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PeakDelayWebEditor (PeakDelayProcessor&);
    ~PeakDelayWebEditor() override;

    void resized() override;

    int getControlParameterIndex (Component&) override
    {
        return controlParameterIndexReceiver.getControlParameterIndex();
    }

private:
    /** Pushes the processor's input level, onset count and the host tempo to
        the page as the one "delayMeter" event - none of them is a parameter,
        so there is no relay to carry them. The TapScope animates off the first
        two and nothing else: no signal, no movement. Same shape as Peak Wah's
        "filterMod" feed.

        Tempo rides along because a synced Time knob means a different number
        of milliseconds at every tempo, and the host can change tempo with
        nothing on the face moving at all - so the readouts beside the knobs
        had no way to hear about it and sat on a stale figure until something
        was touched. This feed is already running at 45 Hz for the scope; the
        page only re-reads the readouts when the number actually changes. */
    void timerCallback() override;

    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PeakDelayProcessor& processorRef;

    // Where the page comes from - see EE_JSUI_DEV_SERVER above.
    static constexpr bool kUseDevServer = EE_JSUI_DEV_SERVER != 0;
    static const juce::String devServerAddress;

    juce::WebSliderRelay leftTimeRelay { "ltime" };
    juce::WebSliderRelay rightTimeRelay { "rtime" };
    juce::WebSliderRelay feedbackRelay { "fb" };
    juce::WebSliderRelay mixRelay { "mix" };
    // The footer's two stage sections: Tape (Wear + Flutter), with a router
    // saying which side of the delay it sits on, and Mod (Drift + Phaser).
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
    // its own labels for them (App.jsx), the same way the Tape router does.
    juce::WebComboBoxRelay typeRelay { "dtype" };

    juce::WebToggleButtonRelay syncRelay { "sync" };
    juce::WebToggleButtonRelay timeUnitRelay { "timeunit" };
    juce::WebToggleButtonRelay tapePreRelay { "tapepre" };
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
    juce::WebToggleButtonParameterAttachment tapePreAttachment;
    juce::WebToggleButtonParameterAttachment onAttachment;

#if EE_TAPE_TUNER
    // Flip showTuner in the .cpp to bring this back without reconfiguring
    // CMake - same switch the old ee::ui editor had.
    std::unique_ptr<TapeTunerPanel> tunerPanel;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakDelayWebEditor)
};
