#include "ee/ui/DigitalKnob.h"

#include <cmath>

namespace ee::ui
{
namespace
{
    /** Everything the two sizes disagree about. All of it is a fraction of the
        cell radius `R`, so a cap is the same drawing at any diameter. */
    struct Metrics
    {
        float capRadius;     // white face, out to the middle of the ring
        float ringWidth;     // the charcoal ring around it
        float tickInner;     // the scale, outside the ring
        float tickOuter;
        float tickWidth;
        int   tickCount;
        float pointerReach;  // centre of the pointer, as a fraction of capRadius
        float pointerLength; // ... and its size, likewise
        float pointerWidth;
    };

    constexpr Metrics kLarge { 0.800f, 0.040f, 0.905f, 1.000f, 0.012f, 37, 0.705f, 0.200f, 0.120f };
    constexpr Metrics kSmall { 0.775f, 0.055f, 0.890f, 1.000f, 0.015f, 25, 0.685f, 0.230f, 0.150f };

    const Metrics& metricsFor (DigitalKnob::Size size)
    {
        return size == DigitalKnob::Size::large ? kLarge : kSmall;
    }

    juce::Point<float> polar (juce::Point<float> centre, float radius, float angle)
    {
        return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
    }
}

juce::Rectangle<float> DigitalKnob::faceArea (juce::Rectangle<float> bounds, Size size)
{
    const float R = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto& m = metricsFor (size);

    // Well inside where the pointer sweeps. A glyph on the cap is a hint at
    // what the knob does, not a diagram - kept small, it reads as part of the
    // cap rather than competing with the pointer for the eye.
    const float clear = R * m.capRadius * (m.pointerReach - m.pointerLength * 0.5f) * 0.66f;

    return juce::Rectangle<float> (clear * 2.0f, clear * 2.0f).withCentre (bounds.getCentre());
}

void DigitalKnob::draw (juce::Graphics& g,
                        juce::Rectangle<float> bounds,
                        float sliderPos,
                        float startAngle,
                        float endAngle,
                        Size size,
                        const PedalTheme& theme,
                        bool enabled,
                        const EndMarker& endMarker)
{
    const float R = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    if (R <= 2.0f)
        return;

    const auto& m = metricsFor (size);
    const auto centre = bounds.getCentre();
    const float capR = R * m.capRadius;
    const float ringW = juce::jmax (1.6f, R * m.ringWidth);
    const float angle = startAngle + juce::jlimit (0.0f, 1.0f, sliderPos) * (endAngle - startAngle);

    auto disc = [centre] (float radius)
    { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre); };

    const float dim = enabled ? 1.0f : 0.45f;

    //== the scale ==========================================================
    // One tick per step across the slider's own travel, darkening up to the
    // value. This is the only readout on the cap - there is no arc.
    {
        const float lit = juce::jlimit (0.0f, 1.0f, sliderPos);

        // The reached ticks take the face's one accent - the same colour the
        // display's trace is drawn in - so the two things that say what the
        // pedal is doing say it in the same voice.
        const auto litColour = theme.glow.withMultipliedAlpha (dim);
        const auto dullColour = theme.knobTrack.withMultipliedAlpha (dim);
        const float tickW = juce::jmax (1.0f, R * m.tickWidth);

        for (int i = 0; i < m.tickCount; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (m.tickCount - 1);
            const float a = startAngle + t * (endAngle - startAngle);

            // The last tick belongs to the marker, which draws it itself.
            if (endMarker.present && i == m.tickCount - 1)
                continue;

            g.setColour (t <= lit + 1.0e-4f ? litColour : dullColour);
            g.drawLine (juce::Line<float> (polar (centre, R * m.tickInner, a),
                                           polar (centre, R * m.tickOuter, a)),
                        tickW);
        }

        if (endMarker.present)
        {
            // Heavier and longer than its neighbours, so the end of the travel
            // reads as a place rather than as the last of a series.
            g.setColour (endMarker.colour.withMultipliedAlpha (dim));
            g.drawLine (juce::Line<float> (polar (centre, R * (m.tickInner - 0.035f), endAngle),
                                           polar (centre, R * m.tickOuter, endAngle)),
                        tickW * 2.6f);

            if (endMarker.label.isNotEmpty())
            {
                const auto at = polar (centre, R * 1.17f, endAngle);
                const float h = juce::jmax (10.0f, R * 0.36f);

                g.setFont (theme.bodyFont (h).boldened());
                g.drawText (endMarker.label,
                            juce::Rectangle<float> (h * 2.0f, h * 1.4f).withCentre (at),
                            juce::Justification::centred, false);
            }
        }
    }

    //== the cap ============================================================
    // A drop shadow down and to the right is what lifts the cap off the face;
    // there is no outline doing that job.
    {
        juce::Path capPath;
        capPath.addEllipse (disc (capR));

        // Two passes: a tight one that darkens the ground right under the rim,
        // and a wide one that carries well below the cap. One pass deep enough
        // to read from a distance smears the rim; this keeps the edge crisp and
        // still sits the knob on the face.
        juce::DropShadow (theme.softShadow,
                          juce::roundToInt (juce::jmax (3.0f, capR * 0.24f)),
                          { 0, juce::roundToInt (juce::jmax (2.0f, capR * 0.10f)) })
            .drawForPath (g, capPath);

        juce::DropShadow (theme.softShadow.withMultipliedAlpha (0.8f),
                          juce::roundToInt (juce::jmax (5.0f, capR * 0.55f)),
                          { 0, juce::roundToInt (juce::jmax (3.0f, capR * 0.26f)) })
            .drawForPath (g, capPath);
    }

    {
        const auto face = disc (capR - ringW * 0.5f);

        juce::ColourGradient fill (theme.softHighlight, face.getCentreX(), face.getY(),
                                   theme.knobFill.darker (0.045f), face.getCentreX(), face.getBottom(),
                                   false);
        g.setGradientFill (fill);
        g.fillEllipse (face);

        // The ring straddles `capR`, so the face and the ring share an edge and
        // no face colour leaks past it.
        g.setColour (theme.knobBody.withMultipliedAlpha (dim));
        g.drawEllipse (disc (capR - ringW * 0.5f), ringW);

        // A hairline of the face's own light just inside the ring, so the cap
        // reads as a dome rather than a flat disc in a hoop.
        g.setColour (theme.softHighlight.withAlpha (0.55f));
        g.drawEllipse (disc (capR - ringW * 1.35f), juce::jmax (0.8f, R * 0.008f));
    }

    //== the pointer ========================================================
    // A short rounded bar lying along the radius. Everything else on the cap is
    // still, so this alone carries the position.
    {
        const float length = capR * m.pointerLength;
        const float width = capR * m.pointerWidth;

        juce::Path pointer;
        pointer.addRoundedRectangle (-width * 0.5f, -length * 0.5f, width, length, width * 0.5f);

        const auto at = polar (centre, capR * m.pointerReach, angle);
        const auto place = juce::AffineTransform::rotation (angle).translated (at);

        g.setColour (theme.knobPointer.withMultipliedAlpha (dim));
        g.fillPath (pointer, place);
    }
}

} // namespace ee::ui
