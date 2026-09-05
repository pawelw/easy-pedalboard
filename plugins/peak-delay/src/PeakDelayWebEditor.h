#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if EE_TAPE_TUNER
#include "TapeTunerPanel.h"
#endif

class PeakDelayProcessor;

/** Peak Delay's face, ported from the same juce::WebBrowserComponent + React
    approach as Peak Wah's (see PeakWahWebEditor) instead of ee::ui::PedalEditor.
    The Tape knob shares the same knob look as everything else here rather
    than the old face's distinct photographic cap - see jsui/README.md. */
class PeakDelayWebEditor : public juce::AudioProcessorEditor
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
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PeakDelayProcessor& processorRef;

    // kUseDevServer true points the browser at the Vite dev server for
    // hot-reload while iterating on jsui/src; false serves jsui/dist off disk
    // (built with `npm run build`) through the resource provider below.
    static constexpr bool kUseDevServer = true;
    static const juce::String devServerAddress;

    juce::WebSliderRelay leftTimeRelay { "ltime" };
    juce::WebSliderRelay rightTimeRelay { "rtime" };
    juce::WebSliderRelay feedbackRelay { "fb" };
    juce::WebSliderRelay mixRelay { "mix" };
    juce::WebSliderRelay modRelay { "mod" };
    juce::WebSliderRelay tapeRelay { "tape" };

    juce::WebToggleButtonRelay syncRelay { "sync" };
    juce::WebToggleButtonRelay timeUnitRelay { "timeunit" };
    juce::WebToggleButtonRelay onRelay { "on" };

    juce::WebControlParameterIndexReceiver controlParameterIndexReceiver;

    struct SinglePageBrowser : juce::WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;
        bool pageAboutToLoad (const juce::String& newURL) override;
    };

    SinglePageBrowser webView;

    juce::WebSliderParameterAttachment leftTimeAttachment;
    juce::WebSliderParameterAttachment rightTimeAttachment;
    juce::WebSliderParameterAttachment feedbackAttachment;
    juce::WebSliderParameterAttachment mixAttachment;
    juce::WebSliderParameterAttachment modAttachment;
    juce::WebSliderParameterAttachment tapeAttachment;

    juce::WebToggleButtonParameterAttachment syncAttachment;
    juce::WebToggleButtonParameterAttachment timeUnitAttachment;
    juce::WebToggleButtonParameterAttachment onAttachment;

#if EE_TAPE_TUNER
    // Flip showTuner in the .cpp to bring this back without reconfiguring
    // CMake - same switch the old ee::ui editor had.
    std::unique_ptr<TapeTunerPanel> tunerPanel;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakDelayWebEditor)
};
