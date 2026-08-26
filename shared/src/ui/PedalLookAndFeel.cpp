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

    if (theme.knobFilmstrip.isValid() && theme.knobFilmstripFrames > 1)
    {
        const int frameHeight = theme.knobFilmstrip.getHeight() / theme.knobFilmstripFrames;
        const int frame = juce::jlimit (0, theme.knobFilmstripFrames - 1,
                                        static_cast<int> (sliderPos * static_cast<float> (theme.knobFilmstripFrames - 1) + 0.5f));

        g.drawImage (theme.knobFilmstrip,
                     static_cast<int> (square.getX()), static_cast<int> (square.getY()),
                     static_cast<int> (square.getWidth()), static_cast<int> (square.getHeight()),
                     0, frame * frameHeight,
                     theme.knobFilmstrip.getWidth(), frameHeight);
        return;
    }

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
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, angle, true);
        g.setColour (slider.isEnabled() ? theme.accent : theme.accentDim);
        g.strokePath (valueArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyRadius = arcRadius - track * 1.6f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (theme.knobBody);
    g.fillEllipse (body);
    g.setColour (theme.knobOutline);
    g.drawEllipse (body, 1.4f);

    juce::Path pointer;
    const float pointerThickness = juce::jmax (2.0f, bodyRadius * 0.10f);
    const float pointerLength = bodyRadius * 0.72f;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -bodyRadius * 0.86f,
                                 pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (theme.knobPointer);
    g.fillPath (pointer);
}

} // namespace ee::ui
