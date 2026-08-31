#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** A live LFO waveform preview.

    Reads four parameters named in a `WaveDisplaySpec` - amount, rate, shape and
    a mode flag - and draws the resulting LFO shape across the component. In
    single-trace mode it is one curve; in paired mode it draws the curve and its
    mirror image, the way an auto-pan shows its left and right motion.

    The trace is generated with `ee::dsp::lfoValue`, the same function the DSP
    uses, so what is drawn is what is heard.
*/
class WaveDisplay : public juce::Component,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::AsyncUpdater,
                    private juce::Timer
{
public:
    WaveDisplay (juce::AudioProcessorValueTreeState& state,
                 const WaveDisplaySpec& spec,
                 const PedalTheme& theme);

    ~WaveDisplay() override;

    void paint (juce::Graphics&) override;

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void paintLive (juce::Graphics&, juce::Rectangle<float> bounds);

    float normalised (const juce::String& paramID) const;

    juce::AudioProcessorValueTreeState& apvts;
    WaveDisplaySpec spec;
    PedalTheme theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveDisplay)
};

} // namespace ee::ui
