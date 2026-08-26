#include "ee/ui/FootSwitch.h"

namespace ee::ui
{
namespace
{
    constexpr float kLedDiameter = 13.0f;
    constexpr float kShadowDrop = 4.0f;
    constexpr float kMaxStompDiameter = 76.0f;
    constexpr int kKnurlTeeth = 64;

    constexpr float kStompCentreX = 0.44f;
    constexpr float kLedCentreX = 0.74f;

    /** Milled teeth around the rim of the barrel. */
    void drawKnurl (juce::Graphics& g, juce::Point<float> centre, float outer, float inner,
                    juce::Colour light, juce::Colour dark)
    {
        for (int i = 0; i < kKnurlTeeth; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * static_cast<float> (i)
                            / static_cast<float> (kKnurlTeeth);

            // Teeth catch the light from above, so shade them by their angle.
            const float lit = 0.5f + 0.5f * std::cos (a - juce::MathConstants<float>::pi * 0.35f);

            juce::Path tooth;
            const float half = juce::MathConstants<float>::twoPi / static_cast<float> (kKnurlTeeth) * 0.34f;
            tooth.startNewSubPath (centre.x + outer * std::sin (a - half), centre.y - outer * std::cos (a - half));
            tooth.lineTo (centre.x + outer * std::sin (a + half), centre.y - outer * std::cos (a + half));
            tooth.lineTo (centre.x + inner * std::sin (a + half), centre.y - inner * std::cos (a + half));
            tooth.lineTo (centre.x + inner * std::sin (a - half), centre.y - inner * std::cos (a - half));
            tooth.closeSubPath();

            g.setColour (dark.interpolatedWith (light, lit));
            g.fillPath (tooth);
        }
    }
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

    const float diameter = juce::jmin (area.getWidth() * 0.5f,
                                       area.getHeight() - kShadowDrop * 2.0f,
                                       kMaxStompDiameter);
    if (diameter <= 0.0f)
        return;

    const float centreY = area.getCentreY();
    auto centre = juce::Point<float> (area.getX() + area.getWidth() * kStompCentreX, centreY);

    if (isDown)
        centre.y += 1.5f;

    const float r = diameter * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillEllipse (juce::Rectangle<float> (diameter, diameter)
                       .withCentre (centre.translated (0.0f, kShadowDrop)));

    const juce::Colour steelLight (0xfff2f1ee);
    const juce::Colour steelMid (0xffb9b7b1);
    const juce::Colour steelDark (0xff6e6c67);

    // Hex collar the barrel is bolted through.
    {
        juce::Path nut;
        for (int i = 0; i < 6; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 6.0f
                            + juce::MathConstants<float>::pi / 6.0f;
            const auto p = centre.translated (r * 1.04f * std::sin (a), -r * 1.04f * std::cos (a));
            if (i == 0) nut.startNewSubPath (p); else nut.lineTo (p);
        }
        nut.closeSubPath();

        g.setColour (steelMid.darker (0.35f));
        g.fillPath (nut);
        g.setColour (steelDark.withAlpha (0.7f));
        g.strokePath (nut, juce::PathStrokeType (1.0f));
    }

    drawKnurl (g, centre, r, r * 0.88f, steelLight, steelDark);

    const auto barrel = juce::Rectangle<float> (r * 1.78f, r * 1.78f).withCentre (centre);
    juce::ColourGradient barrelFill (steelLight, barrel.getX() + barrel.getWidth() * 0.3f, barrel.getY(),
                                     steelDark, barrel.getRight(), barrel.getBottom(), true);
    barrelFill.addColour (0.45, steelMid);
    g.setGradientFill (barrelFill);
    g.fillEllipse (barrel);

    g.setColour (steelDark.withAlpha (0.55f));
    g.drawEllipse (barrel, 1.0f);

    // Turned face: a few concentric passes read as machining marks.
    for (int i = 1; i <= 3; ++i)
    {
        const float k = 1.0f - static_cast<float> (i) * 0.16f;
        g.setColour ((i % 2 == 0 ? steelLight : steelMid).withAlpha (0.35f));
        g.drawEllipse (barrel.withSizeKeepingCentre (barrel.getWidth() * k, barrel.getHeight() * k), 0.9f);
    }

    const auto cap = barrel.withSizeKeepingCentre (barrel.getWidth() * 0.46f, barrel.getHeight() * 0.46f);
    juce::ColourGradient capFill (steelMid.brighter (isHighlighted ? 0.30f : 0.14f),
                                  cap.getCentreX(), cap.getY(),
                                  steelDark, cap.getCentreX(), cap.getBottom(), false);
    g.setGradientFill (capFill);
    g.fillEllipse (cap);
    g.setColour (steelDark.withAlpha (0.6f));
    g.drawEllipse (cap, 0.9f);

    g.setColour (steelLight.withAlpha (0.38f));
    g.fillEllipse (cap.withSizeKeepingCentre (cap.getWidth() * 0.30f, cap.getHeight() * 0.26f)
                       .translated (-cap.getWidth() * 0.05f, -cap.getHeight() * 0.16f));

    // Dark slot across the top of the barrel, as on the reference switch.
    g.setColour (juce::Colour (0xff2c2a28));
    g.fillRoundedRectangle (juce::Rectangle<float> (r * 0.34f, r * 0.15f)
                                .withCentre (centre.translated (0.0f, -r * 0.62f)), 1.5f);

    const auto led = juce::Rectangle<float> (kLedDiameter, kLedDiameter)
                         .withCentre ({ area.getX() + area.getWidth() * kLedCentreX, centreY });

    // Bezel the lamp is seated in.
    g.setColour (pedalTheme.ledRing);
    g.fillEllipse (led.expanded (5.5f));
    g.setColour (pedalTheme.ledRing.brighter (0.25f));
    g.drawEllipse (led.expanded (5.5f), 1.0f);

    if (engaged)
    {
        g.setColour (pedalTheme.ledOn.withAlpha (0.16f));
        g.fillEllipse (led.expanded (14.0f));
        g.setColour (pedalTheme.ledOn.withAlpha (0.30f));
        g.fillEllipse (led.expanded (7.0f));
    }

    g.setColour (engaged ? pedalTheme.ledOn : pedalTheme.ledOff.darker (0.5f));
    g.fillEllipse (led);

    if (engaged)
    {
        g.setColour (pedalTheme.ledOn.brighter (0.9f).withAlpha (0.85f));
        g.fillEllipse (led.reduced (kLedDiameter * 0.30f).translated (-0.5f, -1.0f));
    }
}

} // namespace ee::ui
