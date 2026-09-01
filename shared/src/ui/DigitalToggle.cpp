#include "ee/ui/DigitalToggle.h"

namespace ee::ui
{
namespace
{
    /** Corner radius and stroke of the bezel, as fractions of its height. */
    constexpr float kCornerFraction = 0.26f;
    constexpr float kStrokeFraction = 0.055f;

    /** How much of the bezel the glyph fills. */
    constexpr float kIconFraction = 0.54f;
}

DigitalToggle::DigitalToggle (juce::AudioProcessorValueTreeState& state,
                              const ToggleSpec& spec,
                              const PedalTheme& theme)
    : juce::Button (spec.caption),
      pedalTheme (theme),
      captionText (spec.caption),
      onColour (spec.litColour.value_or (theme.textPrimary)),
      icon (spec.icon)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, spec.parameterID, *this);
}

DigitalToggle::~DigitalToggle() = default;

int DigitalToggle::switchWidth() const
{
    return icon ? iconSize : captionWidth;
}

int DigitalToggle::switchHeight() const
{
    return icon ? iconSize : captionHeight;
}

void DigitalToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    if (bounds.isEmpty())
        return;

    const bool on = getToggleState();
    const float stroke = juce::jmax (1.4f, bounds.getHeight() * kStrokeFraction);
    const float radius = bounds.getHeight() * kCornerFraction;

    // Off is the same pale grey the unreached ticks are drawn in, so a button
    // doing nothing sits at the same weight as a scale not reached.
    const auto ink = (on ? onColour : pedalTheme.knobTrack)
                         .withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f);

    // The face shows through the bezel: it is an outline, not a chip.
    if (highlighted || down)
    {
        g.setColour (pedalTheme.knobBody.withAlpha (down ? 0.10f : 0.05f));
        g.fillRoundedRectangle (bounds, radius);
    }

    g.setColour (ink);
    g.drawRoundedRectangle (bounds.reduced (stroke * 0.5f), radius, stroke);

    if (icon)
    {
        const float side = bounds.getHeight() * kIconFraction;
        icon (g, juce::Rectangle<float> (side, side).withCentre (bounds.getCentre()), ink);
        return;
    }

    g.setFont (pedalTheme.bodyFont (9.5f).boldened().withExtraKerningFactor (0.12f));
    g.drawText (captionText.toUpperCase(), bounds, juce::Justification::centred, false);
}

} // namespace ee::ui
