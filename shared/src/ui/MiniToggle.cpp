#include "ee/ui/MiniToggle.h"

namespace ee::ui
{
namespace
{
    constexpr float kCornerRadius = 5.0f;
    constexpr float kBezelThickness = 3.0f;
}

MiniToggle::MiniToggle (juce::AudioProcessorValueTreeState& state,
                        const ToggleSpec& spec,
                        const PedalTheme& theme)
    : juce::Button (spec.caption),
      pedalTheme (theme),
      captionText (spec.caption),
      litColour (spec.litColour.value_or (theme.glow))
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, spec.parameterID, *this);
}

MiniToggle::~MiniToggle() = default;

void MiniToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.5f);
    const bool on = getToggleState();

    // Off, the frame and legend fall back to a grey that barely lifts off the
    // black face, so only the lit state carries any colour.
    const auto accent = on ? litColour : pedalTheme.knobBody.brighter (0.35f);

    g.setColour (pedalTheme.knobBody);
    g.fillRoundedRectangle (bounds, kCornerRadius + kBezelThickness * 0.5f);

    g.setColour (accent);
    g.drawRoundedRectangle (bounds.reduced (kBezelThickness * 0.5f), kCornerRadius, kBezelThickness);

    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : 0.07f));
        g.fillRoundedRectangle (bounds, kCornerRadius + kBezelThickness * 0.5f);
    }

    g.setColour (accent);
    g.setFont (pedalTheme.bodyFont (9.0f).boldened().withExtraKerningFactor (0.14f));
    g.drawText (captionText.toUpperCase(), getLocalBounds(), juce::Justification::centred, false);
}

} // namespace ee::ui
