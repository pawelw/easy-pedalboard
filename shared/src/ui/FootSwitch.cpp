#include "ee/ui/FootSwitch.h"

namespace ee::ui
{
namespace
{
    constexpr float kLedDiameter = 13.0f;
    constexpr float kLedGap = 14.0f;
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
    auto area = getLocalBounds().toFloat();
    const bool engaged = getToggleState();

    const auto ledArea = juce::Rectangle<float> (kLedDiameter, kLedDiameter)
                             .withCentre ({ area.getCentreX(), area.getY() + kLedDiameter * 0.5f });

    if (engaged)
    {
        g.setColour (pedalTheme.ledOn.withAlpha (0.22f));
        g.fillEllipse (ledArea.expanded (7.0f));
        g.setColour (pedalTheme.ledOn.withAlpha (0.40f));
        g.fillEllipse (ledArea.expanded (3.0f));
    }

    g.setColour (engaged ? pedalTheme.ledOn : pedalTheme.ledOff);
    g.fillEllipse (ledArea);
    g.setColour (pedalTheme.outline);
    g.drawEllipse (ledArea, 1.0f);

    area.removeFromTop (kLedDiameter + kLedGap);

    const float diameter = juce::jmin (area.getWidth(), area.getHeight());
    auto stomp = juce::Rectangle<float> (diameter, diameter)
                     .withCentre ({ area.getCentreX(), area.getCentreY() });

    if (isDown)
        stomp = stomp.reduced (2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (stomp.translated (0.0f, 3.0f));

    juce::ColourGradient gradient (pedalTheme.switchHighlight, stomp.getCentreX(), stomp.getY(),
                                   pedalTheme.switchBody.darker (0.5f), stomp.getCentreX(), stomp.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillEllipse (stomp);

    g.setColour (pedalTheme.outline);
    g.drawEllipse (stomp, 1.5f);

    const auto cap = stomp.reduced (diameter * 0.26f);
    g.setColour (pedalTheme.switchBody.brighter (isHighlighted ? 0.35f : 0.12f));
    g.fillEllipse (cap);
    g.setColour (pedalTheme.outline.darker (0.3f));
    g.drawEllipse (cap, 1.0f);
}

} // namespace ee::ui
