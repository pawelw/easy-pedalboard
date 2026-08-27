#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Rotary control plus the value readout and caption underneath it. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state,
          const juce::String& parameterID,
          const juce::String& caption,
          const PedalTheme& theme);

    ~Knob() override;

    /** Height of the value and caption rows below the rotary. */
    static constexpr int labelHeight = 32;

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

private:
    void refreshValueText();

    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramID;
    juce::String captionText;
    const PedalTheme& pedalTheme;

    juce::Slider slider;
    juce::String valueText;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace ee::ui
