#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Rotary control plus the value readout and caption underneath it. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state,
          const KnobSpec& spec,
          const PedalTheme& theme);

    ~Knob() override;

    /** Height of the value and caption rows below the rotary. */
    static constexpr int labelHeight = 32;

    /** Same, compact: value readout only, no caption. */
    static constexpr int compactLabelHeight = 16;

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

    /** Re-reads the value text (from `liveValueText` if the spec set one, else
        the parameter) and repaints if it changed. Call this when something the
        `liveValueText` closure depends on has moved. */
    void refreshValueText();

    int getLabelHeight() const noexcept { return compact ? compactLabelHeight : labelHeight; }

    /** Called after the value changes, once the readout has refreshed. Lets a
        parent react (e.g. repaint artwork that depends on the value). */
    std::function<void()> onValueChanged;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramID;
    juce::String captionText;
    const PedalTheme& pedalTheme;
    bool compact = false;
    bool compactCaption = false;

    std::function<juce::String()> liveValueText;

    juce::Slider slider;
    juce::String valueText;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace ee::ui
