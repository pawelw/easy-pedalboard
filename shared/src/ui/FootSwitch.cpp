#include "ee/ui/FootSwitch.h"

namespace ee::ui
{
namespace
{
    constexpr float kLedDiameter = 9.0f;
    constexpr float kGlowRadius = 8.0f;  // halo drawn outside the LED
    constexpr float kLedGap = 12.0f;     // between the halo and the stomp
    constexpr float kShadowDrop = 3.0f;
    constexpr float kMaxStompDiameter = 58.0f;
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

    // The halo is drawn outside the LED, so it needs its own room at the top or
    // the component edge slices it off.
    const auto ledArea = juce::Rectangle<float> (kLedDiameter, kLedDiameter)
                             .withCentre ({ area.getCentreX(),
                                            area.getY() + kGlowRadius + kLedDiameter * 0.5f });

    if (engaged)
    {
        g.setColour (pedalTheme.ledOn.withAlpha (0.18f));
        g.fillEllipse (ledArea.expanded (kGlowRadius));
        g.setColour (pedalTheme.ledOn.withAlpha (0.38f));
        g.fillEllipse (ledArea.expanded (kGlowRadius * 0.45f));
    }

    g.setColour (engaged ? pedalTheme.ledOn : pedalTheme.ledOff);
    g.fillEllipse (ledArea);
    g.setColour (pedalTheme.outline);
    g.drawEllipse (ledArea, 1.0f);

    const float stompTop = ledArea.getBottom() + kGlowRadius + kLedGap;
    const float available = area.getBottom() - stompTop - kShadowDrop;
    const float diameter = juce::jmin (area.getWidth(), available, kMaxStompDiameter);

    if (diameter <= 0.0f)
        return;

    auto stomp = juce::Rectangle<float> (diameter, diameter)
                     .withCentre ({ area.getCentreX(), stompTop + available * 0.5f });

    if (isDown)
        stomp = stomp.reduced (2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (stomp.translated (0.0f, kShadowDrop));

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
