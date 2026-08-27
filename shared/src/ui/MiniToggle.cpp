#include "ee/ui/MiniToggle.h"

namespace ee::ui
{

MiniToggle::MiniToggle (juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterID,
                        const juce::String& caption,
                        const PedalTheme& theme)
    : juce::Button (caption), pedalTheme (theme), captionText (caption)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, parameterID, *this);
}

MiniToggle::~MiniToggle() = default;

void MiniToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.5f);
    const float radius = bounds.getHeight() * 0.5f;
    const bool on = getToggleState();

    if (on)
    {
        g.setColour (pedalTheme.glow.withAlpha (0.28f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), radius + 1.5f, 3.0f);
    }

    g.setColour (on ? pedalTheme.knobBody : pedalTheme.knobTrack);
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (pedalTheme.knobBody);
    g.drawRoundedRectangle (bounds, radius, 1.6f);

    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.16f : 0.08f));
        g.fillRoundedRectangle (bounds, radius);
    }

    g.setColour (on ? pedalTheme.knobPointer : pedalTheme.knobBody);
    g.setFont (pedalTheme.bodyFont (9.5f).boldened().withExtraKerningFactor (0.12f));
    g.drawText (captionText.toUpperCase(), getLocalBounds(), juce::Justification::centred, false);
}

} // namespace ee::ui
