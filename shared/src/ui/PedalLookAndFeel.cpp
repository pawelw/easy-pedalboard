#include "ee/ui/PedalLookAndFeel.h"

#include "ee/ui/FaderStrip.h"

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

    const bool inverted = static_cast<bool> (slider.getProperties().getWithDefault ("invertedArc", false));

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                       rotaryStartAngle, rotaryEndAngle, true);

    if (inverted)
    {
        // A bold white ring is the resting state; a faint dark halo keeps it
        // legible on a light face.
        const auto haloStroke = juce::PathStrokeType (track + 4.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded);
        const auto whiteStroke = juce::PathStrokeType (track + 2.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded);
        g.setColour (theme.knobBody.withAlpha (0.35f));
        g.strokePath (arc, haloStroke);
        g.setColour (juce::Colours::white);
        g.strokePath (arc, whiteStroke);
    }
    else
    {
        g.setColour (theme.knobTrack);
        g.strokePath (arc, stroke);
    }

    const bool arcOverridden = slider.isColourSpecified (juce::Slider::thumbColourId);
    const auto arcColour = arcOverridden ? slider.findColour (juce::Slider::thumbColourId)
                                         : theme.accent;

    if (inverted)
    {
        // Rests full and white at the top of the range; the arc grows back from
        // the maximum end as the knob is turned down.
        if (angle < rotaryEndAngle - 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 angle, rotaryEndAngle, true);

            g.setColour (slider.isEnabled() ? arcColour : theme.accentDim);
            g.strokePath (value, stroke);
        }
    }
    else if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, angle, true);

        g.setColour (slider.isEnabled() ? arcColour : theme.accentDim);
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

    const auto capFill = slider.isColourSpecified (juce::Slider::rotarySliderFillColourId)
                             ? slider.findColour (juce::Slider::rotarySliderFillColourId)
                             : theme.knobFill;
    const auto capBorder = slider.isColourSpecified (juce::Slider::rotarySliderOutlineColourId)
                               ? slider.findColour (juce::Slider::rotarySliderOutlineColourId)
                               : theme.knobBody;

    g.setColour (capFill);
    g.fillEllipse (ring.expanded (ringThickness * 0.5f));

    g.setColour (capBorder);
    g.drawEllipse (ring, ringThickness);

    const float dotRadius = juce::jmax (2.0f, diameter * 0.038f);
    const float dotDistance = ringRadius * 0.62f;
    const auto dot = juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f)
                         .withCentre (centre.translated (dotDistance * std::sin (angle),
                                                         -dotDistance * std::cos (angle)));

    // A pale cap needs a dark pointer, or the position is invisible.
    g.setColour (capFill.getPerceivedBrightness() > 0.6f ? theme.knobBody : theme.knobPointer);
    g.fillEllipse (dot);
}

void PedalLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                          minSliderPos, maxSliderPos, style, slider);
        return;
    }

    // Graph-style band: a stem dropping to the baseline with a round node at
    // the value. The faint grid and the curve joining the nodes are drawn by
    // PedalEditor, underneath.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float centreX = bounds.getCentreX();

    const auto range = faderTrackRange (bounds);
    const float prop = static_cast<float> (slider.valueToProportionOfLength (slider.getValue()));
    const float nodeY = juce::jmap (prop, 0.0f, 1.0f, range.getEnd(), range.getStart());

    const auto stemColour = slider.isEnabled() ? theme.accent : theme.accentDim;

    // A fader can override its node colour (the level fader does, to set itself
    // apart from the band faders).
    const bool nodeOverridden = slider.isColourSpecified (juce::Slider::trackColourId);
    const auto nodeFill   = nodeOverridden ? slider.findColour (juce::Slider::trackColourId)
                                           : stemColour;
    const auto nodeBorder = nodeOverridden ? juce::Colours::black
                                           : stemColour.brighter (0.4f);

    // Stem from the node down to the baseline.
    g.setColour (stemColour.withAlpha (0.85f));
    g.drawLine ({ centreX, nodeY, centreX, range.getEnd() }, 2.4f);

    // Node handle.
    const float r = kFaderNodeRadius;
    const auto node = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ centreX, nodeY });

    g.setColour (nodeFill);
    g.fillEllipse (node);
    g.setColour (nodeBorder);
    g.drawEllipse (node.reduced (0.9f), nodeOverridden ? 2.0f : 1.5f);

    // Soft top highlight so the node reads as a raised bead - skipped on a pale
    // overridden node, where it would just wash the fill out.
    if (! nodeOverridden)
    {
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillEllipse (node.reduced (r * 0.9f).translated (0.0f, -0.6f));
    }
}

void PedalLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour&, bool highlighted, bool down)
{
    const auto r = button.getLocalBounds().toFloat();
    const float radius = r.getHeight() * 0.5f;

    auto fill = button.findColour (juce::TextButton::buttonColourId);
    if (down)             fill = fill.brighter (0.18f);
    else if (highlighted) fill = fill.brighter (0.09f);

    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);

    // Faint top sheen so the pill has a little depth.
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (r.reduced (1.0f).removeFromTop (r.getHeight() * 0.45f), radius);
}

void PedalLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h,
                                          bool isMouseOver, bool isMouseDragging)
{
    const auto fw = static_cast<float> (w);
    const auto fh = static_cast<float> (h);
    const float thickness = juce::jmax (1.2f, fw / 40.0f);

    // Anchor point sits in from the corner so nothing touches the frame or its
    // drop shadow.
    const float inset = fw * 0.30f;
    const float cx = fw - inset;
    const float cy = fh - inset;

    g.setColour (theme.textSecondary.withAlpha (isMouseOver || isMouseDragging ? 0.55f : 0.34f));

    // A short stroke nearer the corner, a longer one just inside it.
    g.drawLine (cx - fw * 0.20f, cy, cx, cy - fw * 0.20f, thickness);

    const float gap = fw * 0.075f;
    g.drawLine (cx - gap - fw * 0.32f, cy - gap, cx - gap, cy - gap - fw * 0.32f, thickness);
}

void PedalLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                       bool, bool)
{
    const auto colourId = button.getToggleState() ? juce::TextButton::textColourOnId
                                                  : juce::TextButton::textColourOffId;

    g.setColour (button.findColour (colourId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (theme.bodyFont (11.0f).boldened().withExtraKerningFactor (0.08f));
    g.drawText (button.getButtonText().toUpperCase(), button.getLocalBounds(),
                juce::Justification::centred, false);
}

} // namespace ee::ui
