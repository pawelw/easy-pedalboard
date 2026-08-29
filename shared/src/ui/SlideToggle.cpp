#include "ee/ui/SlideToggle.h"

namespace ee::ui
{
namespace
{
    // Track is about two knob-circles wide.
    constexpr float kTrackHeight = 24.0f;
    constexpr float kKnobInset = 3.0f;
    constexpr float kTrackWidth = 2.0f * (kTrackHeight - 2.0f * kKnobInset) + 6.0f;
    constexpr float kLabelGap = 7.0f;
}

SlideToggle::SlideToggle (juce::AudioProcessorValueTreeState& state,
                          const SlideToggleSpec& spec,
                          const PedalTheme& theme)
    : juce::Button (spec.parameterID),
      pedalTheme (theme),
      labelOff (spec.labelOff),
      labelOn (spec.labelOn),
      accent (spec.accent.value_or (theme.title))
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, spec.parameterID, *this);
}

SlideToggle::~SlideToggle() = default;

void SlideToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const bool on = getToggleState();

    const float trackH = juce::jmin (kTrackHeight, bounds.getHeight());
    const auto track = juce::Rectangle<float> (kTrackWidth, trackH).withCentre (bounds.getCentre());

    const float sideW = (bounds.getWidth() - kTrackWidth) * 0.5f - kLabelGap;
    const auto leftLabel  = juce::Rectangle<float> (bounds.getX(), bounds.getY(), sideW, bounds.getHeight());
    const auto rightLabel = juce::Rectangle<float> (track.getRight() + kLabelGap, bounds.getY(), sideW, bounds.getHeight());

    g.setFont (pedalTheme.bodyFont (11.5f).boldened().withExtraKerningFactor (0.04f));

    g.setColour (accent.withAlpha (on ? 0.34f : 1.0f));
    g.drawText (labelOff.toUpperCase(), leftLabel, juce::Justification::centredRight, false);

    g.setColour (accent.withAlpha (on ? 1.0f : 0.34f));
    g.drawText (labelOn.toUpperCase(), rightLabel, juce::Justification::centredLeft, false);

    // Recessed track.
    g.setColour (pedalTheme.knobBody);
    g.fillRoundedRectangle (track, trackH * 0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (track.reduced (0.6f), trackH * 0.5f, 1.0f);

    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : 0.05f));
        g.fillRoundedRectangle (track, trackH * 0.5f);
    }

    // Knob, resting against one end.
    const float kd = trackH - 2.0f * kKnobInset;
    const float kx = on ? track.getRight() - kKnobInset - kd
                        : track.getX() + kKnobInset;
    const auto knob = juce::Rectangle<float> (kx, track.getY() + kKnobInset, kd, kd);

    g.setColour (accent);
    g.fillEllipse (knob);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillEllipse (knob.reduced (kd * 0.30f).translated (0.0f, -kd * 0.10f));
    g.setColour (juce::Colours::black.withAlpha (0.20f));
    g.drawEllipse (knob.reduced (0.5f), 1.0f);
}

} // namespace ee::ui
