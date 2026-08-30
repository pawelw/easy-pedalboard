#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "ee/dsp/ShimmerTuning.h"

/** Development-only panel for dialling Easy Reverb's shimmer voicing while it
    plays.

    Built only when EE_SHIMMER_TUNER is on. The readout at the bottom is the set
    of source lines for whatever the sliders currently say, so a setting that
    works can go straight back into ShimmerTuning.h.
*/
class ShimmerTunerPanel : public juce::Component
{
public:
    using ApplyFn = std::function<void (const ee::dsp::ShimmerTuning&)>;

    static constexpr int preferredWidth = 360;

    ShimmerTunerPanel (const ee::dsp::ShimmerTuning& initial, ApplyFn applyFn);
    ~ShimmerTunerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void pushToProcessor();
    void refreshReadout();

    ee::dsp::ShimmerTuning tuning;
    ee::dsp::ShimmerTuning defaults;
    ApplyFn apply;

    juce::Viewport viewport;
    juce::Component rows;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::Label>> names;

    juce::TextEditor readout;
    juce::TextButton copyButton { "Copy" };
    juce::TextButton resetButton { "Reset" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShimmerTunerPanel)
};
