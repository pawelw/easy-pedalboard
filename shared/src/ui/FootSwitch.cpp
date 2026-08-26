#include "ee/ui/FootSwitch.h"

namespace ee::ui
{
namespace
{
    constexpr float kLedDiameter = 12.0f;
    constexpr float kMaxStompDiameter = 64.0f;
    constexpr float kStompCentreX = 0.5f;
    constexpr float kLedCentreX = 0.82f;
}

FootSwitch::FootSwitch (juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterID,
                        const PedalTheme& theme)
    : juce::Button ("footswitch"), pedalTheme (theme)
{
    setClickingTogglesState (true);
    setTooltip ("Engage / bypass");

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterID, *this);
}

FootSwitch::~FootSwitch() = default;

void FootSwitch::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto area = getLocalBounds().toFloat();
    const bool engaged = getToggleState();

    const float diameter = juce::jmin (area.getWidth() * 0.5f, area.getHeight(), kMaxStompDiameter);
    if (diameter <= 0.0f)
        return;

    auto centre = juce::Point<float> (area.getX() + area.getWidth() * kStompCentreX, area.getCentreY());
    if (isDown)
        centre.y += 1.0f;

    const auto stomp = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

    // Recessed collar, then the button proper standing slightly proud of it.
    g.setColour (pedalTheme.switchBody.darker (0.6f));
    g.fillEllipse (stomp);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (stomp, 1.6f);

    const auto button = stomp.reduced (diameter * 0.14f);
    g.setColour (pedalTheme.switchBody.brighter (isHighlighted ? 0.22f : 0.0f));
    g.fillEllipse (button);
    g.setColour (pedalTheme.switchHighlight.withAlpha (0.75f));
    g.drawEllipse (button, 1.2f);

    g.setColour (pedalTheme.switchHighlight.withAlpha (isDown ? 0.25f : 0.5f));
    g.fillEllipse (button.reduced (diameter * 0.22f));

    const auto led = juce::Rectangle<float> (kLedDiameter, kLedDiameter)
                         .withCentre ({ area.getX() + area.getWidth() * kLedCentreX, area.getCentreY() });

    if (engaged)
    {
        g.setColour (pedalTheme.glow.withAlpha (0.16f));
        g.fillEllipse (led.expanded (20.0f));
        g.setColour (pedalTheme.glow.withAlpha (0.30f));
        g.fillEllipse (led.expanded (12.0f));
        g.setColour (pedalTheme.glow.withAlpha (0.55f));
        g.fillEllipse (led.expanded (5.0f));
    }

    g.setColour (engaged ? pedalTheme.ledOn : pedalTheme.ledOff);
    g.fillEllipse (led);
}

} // namespace ee::ui
