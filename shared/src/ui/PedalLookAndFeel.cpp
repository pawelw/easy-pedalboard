#include "ee/ui/PedalLookAndFeel.h"

namespace ee::ui
{

PedalLookAndFeel::PedalLookAndFeel (PedalTheme themeToUse)
    : theme (std::move (themeToUse))
{
    applyColours();
}

void PedalLookAndFeel::setTheme (PedalTheme newTheme)
{
    theme = std::move (newTheme);
    applyColours();
}

void PedalLookAndFeel::applyColours()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme.background);
    setColour (juce::Label::textColourId, theme.textPrimary);
    setColour (juce::TooltipWindow::backgroundColourId, theme.panel);
    setColour (juce::TooltipWindow::textColourId, theme.textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, theme.outline);
}

void PedalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto square = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre());
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const float radius = diameter * 0.5f;
    const float track = theme.knobThickness;
    const auto centre = square.getCentre();
    const float arcRadius = radius - track * 0.5f - 1.0f;

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (theme.knobTrack);
    g.strokePath (backgroundArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        const bool live = slider.isMouseOverOrDragging() && slider.isEnabled();
        const auto colour = slider.isEnabled() ? (live ? theme.accentGlow : theme.accent) : theme.accentDim;

        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, angle, true);

        if (live)
        {
            g.setColour (colour.withAlpha (0.20f));
            g.strokePath (valueArc, juce::PathStrokeType (track * 4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour.withAlpha (0.34f));
            g.strokePath (valueArc, juce::PathStrokeType (track * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (colour);
        g.strokePath (valueArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyRadius = arcRadius - track * 1.6f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    // Artwork sits on top of the arc, so a transparent PNG keeps its ring.
    if (theme.knobFilmstrip.isValid() && theme.knobFilmstripFrames > 1)
    {
        const int frameHeight = theme.knobFilmstrip.getHeight() / theme.knobFilmstripFrames;
        const int frame = juce::jlimit (0, theme.knobFilmstripFrames - 1,
                                        static_cast<int> (sliderPos * static_cast<float> (theme.knobFilmstripFrames - 1) + 0.5f));

        g.drawImage (theme.knobFilmstrip,
                     body.getX(), body.getY(), body.getWidth(), body.getHeight(),
                     0, frame * frameHeight,
                     theme.knobFilmstrip.getWidth(), frameHeight);
        return;
    }

    if (theme.knobImage.isValid())
    {
        const auto src = theme.knobImage.getBounds().toFloat();
        const float scale = juce::jmin (body.getWidth() / src.getWidth(),
                                        body.getHeight() / src.getHeight());

        auto transform = juce::AffineTransform::translation (-src.getWidth() * 0.5f, -src.getHeight() * 0.5f)
                             .scaled (scale);

        if (theme.knobImageRotates)
            transform = transform.rotated (angle);

        g.drawImageTransformed (theme.knobImage, transform.translated (centre), false);
        return;
    }

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillEllipse (body.translated (0.0f, 2.0f));

    juce::ColourGradient face (theme.knobBody.brighter (0.28f),
                               centre.x - bodyRadius * 0.35f, centre.y - bodyRadius * 0.55f,
                               theme.knobBody.darker (0.55f),
                               centre.x + bodyRadius * 0.5f, centre.y + bodyRadius * 0.7f, true);
    g.setGradientFill (face);
    g.fillEllipse (body);

    {
        // Spun-metal streaks, clipped to the cap so they read as a finish
        // rather than lines drawn on top of it.
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path disc;
        disc.addEllipse (body);
        g.reduceClipRegion (disc);

        constexpr int spokes = 72;
        for (int i = 0; i < spokes; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (spokes);
            const float lit = 0.5f + 0.5f * std::cos (2.0f * (a - juce::MathConstants<float>::pi * 0.25f));
            g.setColour (juce::Colours::white.withAlpha (0.020f + 0.045f * lit));
            g.drawLine (centre.x, centre.y,
                        centre.x + bodyRadius * std::sin (a),
                        centre.y - bodyRadius * std::cos (a), 1.0f);
        }
    }

    g.setColour (theme.knobOutline.withAlpha (0.8f));
    g.drawEllipse (body, 1.2f);

    juce::Path pointer;
    const float pointerThickness = juce::jmax (2.4f, bodyRadius * 0.11f);
    const float pointerLength = bodyRadius * 0.62f;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -bodyRadius * 0.88f,
                                 pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (theme.knobPointer);
    g.fillPath (pointer);
}

} // namespace ee::ui
