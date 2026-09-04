#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

class PeakWahProcessor;

/** Spike: Peak Wah's face built as a React app hosted in a
    juce::WebBrowserComponent, instead of ee::ui::PedalEditor. See
    jsui/README.md for the dev loop and what this does and doesn't prove. */
class PeakWahWebEditor : public juce::AudioProcessorEditor
{
public:
    explicit PeakWahWebEditor (PeakWahProcessor&);

    void resized() override;

    int getControlParameterIndex (Component&) override
    {
        return controlParameterIndexReceiver.getControlParameterIndex();
    }

private:
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    PeakWahProcessor& processorRef;

    // kUseDevServer true points the browser at the Vite dev server for
    // hot-reload while iterating on jsui/src; false serves jsui/dist off disk
    // (built with `npm run build`) through the resource provider below.
    static constexpr bool kUseDevServer = true;
    static const juce::String devServerAddress;

    juce::WebSliderRelay rangeRelay { "range" };
    juce::WebSliderRelay freqRelay { "freq" };
    juce::WebSliderRelay qRelay { "q" };
    juce::WebSliderRelay mixRelay { "mix" };
    juce::WebSliderRelay decayRelay { "decay" };
    juce::WebSliderRelay shapeRelay { "shape" };
    juce::WebSliderRelay timeRelay { "time" };
    juce::WebSliderRelay typeRelay { "ftype" };

    juce::WebToggleButtonRelay stereoRelay { "stereo" };
    juce::WebToggleButtonRelay syncRelay { "sync" };
    juce::WebToggleButtonRelay onRelay { "on" };

    juce::WebControlParameterIndexReceiver controlParameterIndexReceiver;

    struct SinglePageBrowser : juce::WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;
        bool pageAboutToLoad (const juce::String& newURL) override;
    };

    SinglePageBrowser webView;

    juce::WebSliderParameterAttachment rangeAttachment;
    juce::WebSliderParameterAttachment freqAttachment;
    juce::WebSliderParameterAttachment qAttachment;
    juce::WebSliderParameterAttachment mixAttachment;
    juce::WebSliderParameterAttachment decayAttachment;
    juce::WebSliderParameterAttachment shapeAttachment;
    juce::WebSliderParameterAttachment timeAttachment;
    juce::WebSliderParameterAttachment typeAttachment;

    juce::WebToggleButtonParameterAttachment stereoAttachment;
    juce::WebToggleButtonParameterAttachment syncAttachment;
    juce::WebToggleButtonParameterAttachment onAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakWahWebEditor)
};
