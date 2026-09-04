#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** The granular-plus-delay scope band. See `GrainScopeSpec`.

    The left of the strip is the grain cloud, a scatter of blobs the knob values
    shape; a divider marks "now"; the right of the strip is the delay, the burst
    repeated and fading to the right over a faint reverb wash. With no live hook
    it is a still, deterministic picture that redraws only on a knob move, so it
    works offline in the snapshot renderer. */
class GrainScope : public juce::Component,
                   private juce::AudioProcessorValueTreeState::Listener,
                   private juce::AsyncUpdater,
                   private juce::Timer
{
public:
    GrainScope (juce::AudioProcessorValueTreeState& state, const GrainScopeSpec& spec, const PedalTheme& theme);
    ~GrainScope() override;

    void paint (juce::Graphics&) override;

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    float value (const juce::String& paramID) const;

    juce::AudioProcessorValueTreeState& apvts;
    GrainScopeSpec spec;
    PedalTheme theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainScope)
};

} // namespace ee::ui
