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
        float capRadius; // white face, out to the middle of the ring
        float ringWidth; // the charcoal ring around it
        float tickInner; // the scale, outside the ring
        float tickOuter;
        float tickWidth;
        int tickCount;
        float pointerReach;  // centre of the pointer, as a fraction of capRadius
        float pointerLength; // ... and its size, likewise
        float pointerWidth;
    };

    constexpr Metrics kLarge { 0.800f, 0.040f, 0.905f, 1.000f, 0.012f, 37, 0.775f, 0.190f, 0.175f };
    constexpr Metrics kSmall { 0.775f, 0.055f, 0.890f, 1.000f, 0.015f, 25, 0.755f, 0.220f, 0.205f };

    const Metrics& metricsFor (DigitalKnob::Size size)
    {
        return size == DigitalKnob::Size::large ? kLarge : kSmall;
    }

    juce::Point<float> polar (juce::Point<float> centre, float radius, float angle)
    {
        return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
    }
} // namespace

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
            g.drawLine (juce::Line<float> (polar (centre, R * m.tickInner, a), polar (centre, R * m.tickOuter, a)),
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
                const float h = juce::jmax (10.0f, R * 0.468f);

                g.setFont (theme.bodyFont (h).boldened());
                g.drawText (endMarker.label, juce::Rectangle<float> (h * 2.0f, h * 1.4f).withCentre (at),
                            juce::Justification::centred, false);
            }
        }
    }

    //== the cap ============================================================
    // A drop shadow down and to the right is what lifts the cap off the face;
    // there is no outline doing that job on a borderless cap, and even a ringed
    // one wants it for the same reason the reference does - shadow, not a hard
    // line, is what separates a raised disc from its face.
    {
        juce::Path capPath;
        capPath.addEllipse (disc (capR));

        // No digital cap has an outline carrying its silhouette any more - a
        // theme's ring, when it asks for one, is a light hairline set well
        // inside the rim. So every cap leans on the shadow the same way: a
        // soft, nearly symmetric pass right under the rim first - a very faint
        // halo all the way round - then the two directional passes below.
        {
            juce::DropShadow (theme.softShadow.withMultipliedAlpha (0.4f),
                              juce::roundToInt (juce::jmax (1.5f, capR * 0.05f)),
                              { 0, juce::roundToInt (juce::jmax (1.0f, capR * 0.015f)) })
                .drawForPath (g, capPath);
        }

        // Two passes: a tight one that darkens the ground right under the rim,
        // and a wide one that carries well below the cap. One pass deep enough
        // to read from a distance smears the rim; this keeps the edge crisp and
        // still sits the knob on the face.
        juce::DropShadow (theme.softShadow, juce::roundToInt (juce::jmax (3.0f, capR * 0.24f)),
                          { 0, juce::roundToInt (juce::jmax (2.0f, capR * 0.10f)) })
            .drawForPath (g, capPath);

        juce::DropShadow (theme.softShadow.withMultipliedAlpha (0.8f),
                          juce::roundToInt (juce::jmax (5.0f, capR * 0.55f)),
                          { 0, juce::roundToInt (juce::jmax (3.0f, capR * 0.26f)) })
            .drawForPath (g, capPath);
    }

    {
        const auto face = disc (capR - ringW * 0.5f);

        // Flat enough to read as one plastic surface, not a dome shading all
        // the way down - the rim light below is what says which way is up. Only
        // a whisper of gradient, top to bottom.
        juce::ColourGradient fill (theme.knobFill.interpolatedWith (theme.softHighlight, 0.35f), face.getCentreX(),
                                   face.getY(), theme.knobFill.darker (0.03f), face.getCentreX(), face.getBottom(),
                                   false);
        g.setGradientFill (fill);
        g.fillEllipse (face);

        if (theme.knobCapBorder)
        {
            // Not an edge ring: two fine concentric hairlines set well inside
            // the rim, in a mid grey rather than the charcoal of the body. The
            // cap reads as one white disc with a machined groove near its edge,
            // and the rim itself is held only by the shadow.
            const auto ringColour = theme.knobBody.interpolatedWith (theme.knobFill, 0.46f).withMultipliedAlpha (dim);
            const float hair = juce::jmax (1.0f, R * 0.011f);

            g.setColour (ringColour);
            g.drawEllipse (disc (capR * 0.90f), hair);
        }

        // The rim light: a thin highlight right at the top edge of the cap,
        // screen-space - not tied to `angle`, so it stays fixed at 12 o'clock
        // exactly however far the knob is turned, the way the pointer does not.
        // The same top-down-light technique the photographic cap uses
        // (drawImageKnob's "whisper of light"), pulled in tight: it fades out
        // within a sliver of the cap rather than a third of it, so it reads as
        // a bright edge - closer to a border than a lit dome.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addEllipse (face);
            g.reduceClipRegion (clipPath);

            const auto litColour = theme.softHighlight.interpolatedWith (juce::Colours::white, 0.35f);

            // A theme may pin the fade to a fixed number of pixels instead of a
            // fraction of the cap. A dark cap wants the light gone within a few
            // pixels of the top edge; a pale one carries a longer fade.
            const float sheenHeight =
                theme.capRimLightHeight > 0.0f ? theme.capRimLightHeight : face.getHeight() * 0.12f;

            juce::ColourGradient sheen (litColour.withAlpha (0.6f * dim), face.getCentreX(), face.getY(),
                                        litColour.withAlpha (0.0f), face.getCentreX(), face.getY() + sheenHeight,
                                        false);
            g.setGradientFill (sheen);
            g.fillEllipse (face);
        }
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
