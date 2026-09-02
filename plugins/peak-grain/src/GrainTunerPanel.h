#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "ee/dsp/GrainerTuning.h"

/** Development-only panel for dialling Peak Grain's voicing while it plays.

    The seven knobs on the face are the effect; these are the things underneath
    it that are normally fixed - how many grains run backwards, where they land
    across the image, which intervals the pitched ones snap to.

    Built only when EE_GRAIN_TUNER is on. The readout at the bottom is the set
    of source lines for whatever the sliders currently say, so a setting that
    works can go straight back into GrainerTuning.h.
*/
class GrainTunerPanel : public juce::Component
{
public:
    using ApplyFn = std::function<void (const ee::dsp::GrainerTuning&)>;

    static constexpr int preferredWidth = 360;

    GrainTunerPanel (const ee::dsp::GrainerTuning& initial, ApplyFn applyFn);
    ~GrainTunerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void pushToProcessor();
    void refreshReadout();

    ee::dsp::GrainerTuning tuning;
    ee::dsp::GrainerTuning defaults;
    ApplyFn apply;

    juce::Viewport viewport;
    juce::Component rows;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::Label>> names;

    juce::TextEditor readout;
    juce::TextButton copyButton { "Copy" };
    juce::TextButton resetButton { "Reset" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainTunerPanel)
};
