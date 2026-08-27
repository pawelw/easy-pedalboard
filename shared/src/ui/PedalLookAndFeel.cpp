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
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const float track = theme.knobThickness;
    const float arcRadius = diameter * 0.5f - track * 0.5f - 1.0f;
    const auto stroke = juce::PathStrokeType (track, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                       rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (theme.knobTrack);
    g.strokePath (arc, stroke);

    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, angle, true);

        if (slider.isEnabled())
        {
            g.setColour (theme.glow.withAlpha (0.22f));
            g.strokePath (value, juce::PathStrokeType (track * 4.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            g.setColour (theme.glow.withAlpha (0.38f));
            g.strokePath (value, juce::PathStrokeType (track * 2.3f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        g.setColour (slider.isEnabled() ? theme.accent : theme.accentDim);
        g.strokePath (value, stroke);
    }

    const float bodyRadius = arcRadius - track * 1.6f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    // Artwork draws over the arc, so a transparent PNG keeps its ring.
    if (theme.knobFilmstrip.isValid() && theme.knobFilmstripFrames > 1)
    {
        const int frameHeight = theme.knobFilmstrip.getHeight() / theme.knobFilmstripFrames;
        const int frame = juce::jlimit (0, theme.knobFilmstripFrames - 1,
                                        juce::roundToInt (sliderPos * static_cast<float> (theme.knobFilmstripFrames - 1)));

        g.drawImage (theme.knobFilmstrip,
                     body.getX(), body.getY(), body.getWidth(), body.getHeight(),
                     0, frame * frameHeight, theme.knobFilmstrip.getWidth(), frameHeight);
        return;
    }

    if (theme.knobImage.isValid())
    {
        const auto src = theme.knobImage.getBounds().toFloat();
        const float scale = juce::jmin (body.getWidth() / src.getWidth(),
                                        body.getHeight() / src.getHeight());

        auto t = juce::AffineTransform::translation (-src.getWidth() * 0.5f, -src.getHeight() * 0.5f)
                     .scaled (scale);
        if (theme.knobImageRotates)
            t = t.rotated (angle);

        g.drawImageTransformed (theme.knobImage, t.translated (centre), false);
        return;
    }

    // Ring with a dot for position, over a filled cap.
    const float ringRadius = arcRadius - track * 2.4f;
    const float ringThickness = juce::jmax (3.0f, diameter * 0.055f);
    const auto ring = juce::Rectangle<float> (ringRadius * 2.0f, ringRadius * 2.0f).withCentre (centre);

    g.setColour (theme.knobFill);
    g.fillEllipse (ring.expanded (ringThickness * 0.5f));

    g.setColour (theme.knobBody);
    g.drawEllipse (ring, ringThickness);

    const float dotRadius = juce::jmax (2.0f, diameter * 0.038f);
    const float dotDistance = ringRadius * 0.62f;
    const auto dot = juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f)
                         .withCentre (centre.translated (dotDistance * std::sin (angle),
                                                         -dotDistance * std::cos (angle)));

    g.setColour (theme.knobPointer);
    g.fillEllipse (dot);
}

} // namespace ee::ui
