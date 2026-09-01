#include "ee/ui/PedalLookAndFeel.h"

#include "ee/ui/DigitalKnob.h"
#include "ee/ui/FaderStrip.h"

#include "BinaryData.h"

namespace ee::ui
{

namespace
{
    /** The white-on-transparent spoon icon, loaded once. */
    juce::Image spoonImage()
    {
        static const juce::Image img = juce::ImageCache::getFromMemory (BinaryData::spoon_png,
                                                                       BinaryData::spoon_pngSize);
        return img;
    }

    /** The knob artwork: upright, square, its own pointer baked in at 12
        o'clock. The knob body's outer radius is `kKnobImageRadiusFrac` of the
        image width. Loaded once. */
    juce::Image defaultKnobImage()
    {
        static const juce::Image img = juce::ImageCache::getFromMemory (BinaryData::knob_png,
                                                                        BinaryData::knob_pngSize);
        return img;
    }
    constexpr float kKnobImageRadiusFrac = 0.948f;

    /** The static centre plate, laid over the (spinning) knob body and never
        rotated. Loaded once. */
    juce::Image knobPlateImage()
    {
        static const juce::Image img = juce::ImageCache::getFromMemory (BinaryData::plate_png,
                                                                        BinaryData::plate_pngSize);
        return img;
    }
    /** Plate radius as a fraction of the knob radius `R` - sized to cover the
        knob artwork's own metal centre. */
    constexpr float kPlateRadiusFrac = 0.44f;

    /** Plate spin relative to the knob body: negative = opposite direction,
        magnitude < 1 = slower, 0 = fully static. */
    constexpr float kPlateSpinFactor = 0.0f;

    /** A soft specular glint on the plate, orbiting as the knob turns. The
        factor is its angular rate relative to the knob, the alpha its strength. */
    constexpr float kPlateGlintSpinFactor = 0.6f;
    constexpr float kPlateGlintAlpha      = 0.32f;

    /** Knob artwork radius as a fraction of the value-arc radius. Below 1 leaves
        the progress ring visible around the knob. */
    constexpr float kKnobSizeFrac = 0.87f;

    /** Draws the knob artwork rotated to the value, scaled so the body's outer
        edge lands at `R`. The baked-in pointer turns with it. */
    void drawImageKnob (juce::Graphics& g, juce::Point<float> centre, float R,
                        float angle, const juce::Image& img, juce::Colour backing)
    {
        const float imgKnobR = kKnobImageRadiusFrac * static_cast<float> (img.getWidth()) * 0.5f;
        const float scale    = R / imgKnobR;

        // An opaque disc behind the artwork. The cap is a heavy minification of
        // an 848 px source - about 0.13 at the full size and half that for a
        // small one - and the sampler carries some of its transparent surround
        // inward at that reduction, which let the face colour through the body
        // and tinted a small cap against a large one. Backing it means whatever
        // the sampler does at the rim, the body is always the same colour.
        // Slightly inside R so it cannot peek out past the artwork's own edge.
        g.setColour (backing);
        g.fillEllipse (juce::Rectangle<float> (R * 1.94f, R * 1.94f).withCentre (centre));

        const auto t = juce::AffineTransform::translation (img.getWidth() * -0.5f,
                                                           img.getHeight() * -0.5f)
                           .scaled (scale)
                           .rotated (angle)
                           .translated (centre.x, centre.y);

        // The artwork is 848 px square and every cap is a heavy downscale of it
        // - about 0.13 at the full size, half that for a small one. The default
        // sampler takes too few source pixels for a reduction that big, which
        // averages the black grooves away into mid-grey and leaves a small cap
        // looking washed out and tinted next to a large one. Filtering properly
        // costs nothing here (the caps only redraw on a value change) and keeps
        // every knob the same colour whatever its size or angle.
        juce::Graphics::ScopedSaveState imageState (g);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageTransformed (img, t, false);

        // Centre plate - laid over the spinning body and turned only a fraction
        // of the way (and the other direction), so its brushed grain drifts
        // slowly against the knob rather than tracking it.
        if (const auto plate = knobPlateImage(); plate.isValid())
        {
            const float plateR = R * kPlateRadiusFrac;
            const float ps = (plateR * 2.0f) / static_cast<float> (plate.getWidth());
            const auto pt = juce::AffineTransform::translation (plate.getWidth() * -0.5f,
                                                                plate.getHeight() * -0.5f)
                                .scaled (ps)
                                .rotated (angle * kPlateSpinFactor)
                                .translated (centre.x, centre.y);
            juce::Graphics::ScopedSaveState s (g);
            juce::Path pc;
            pc.addEllipse (juce::Rectangle<float> (plateR * 2.0f, plateR * 2.0f).withCentre (centre));
            g.reduceClipRegion (pc);
            g.drawImageTransformed (plate, pt, false);

            // Specular glint that orbits the plate as the knob turns.
            const float ga = angle * kPlateGlintSpinFactor;
            const juce::Point<float> gp (centre.x + plateR * 0.5f * std::sin (ga),
                                         centre.y - plateR * 0.5f * std::cos (ga));
            const auto glow = juce::Rectangle<float> (plateR * 1.8f, plateR * 1.8f).withCentre (gp);
            juce::ColourGradient gg (juce::Colours::white.withAlpha (kPlateGlintAlpha), gp.x, gp.y,
                                     juce::Colours::transparentWhite,
                                     gp.x, gp.y + plateR * 0.9f, true);
            gg.addColour (0.35, juce::Colours::white.withAlpha (kPlateGlintAlpha * 0.4f));
            g.setGradientFill (gg);
            g.fillEllipse (glow);
        }

        // A whisper of top-down light, screen-space so it stays at 12 o'clock
        // however far the knob is turned.
        {
            const auto lit = juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (centre)
                                 .reduced (R * 0.06f);
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addEllipse (lit);
            g.reduceClipRegion (clip);

            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.16f),
                                        lit.getCentreX(), lit.getY(),
                                        juce::Colours::transparentWhite,
                                        lit.getCentreX(), lit.getCentreY() + lit.getHeight() * 0.18f,
                                        false);
            g.setGradientFill (sheen);
            g.fillEllipse (lit);

            juce::ColourGradient shade (juce::Colours::transparentBlack,
                                        lit.getCentreX(), lit.getCentreY() + lit.getHeight() * 0.2f,
                                        juce::Colours::black.withAlpha (0.13f),
                                        lit.getCentreX(), lit.getBottom(),
                                        false);
            g.setGradientFill (shade);
            g.fillEllipse (lit);
        }
    }

    /** Draws the spoon as the position pointer: the handle tip sits on the knob
        centre and the scoop points radially outward along `angle`, reaching
        `length` from the centre. Replaces the dot entirely. */
    void drawSpoonPointer (juce::Graphics& g, juce::Point<float> centre,
                           float angle, float length)
    {
        const auto img = spoonImage();
        if (! img.isValid())
            return;

        const float scale = length / static_cast<float> (img.getHeight());

        // Anchor the midpoint of the spoon halfway out, so its inner end lands on
        // the knob centre.
        const float distance = length * 0.5f;
        const juce::Point<float> at (centre.x + distance * std::sin (angle),
                                     centre.y - distance * std::cos (angle));

        const auto place =
            juce::AffineTransform::translation (img.getWidth() * -0.5f, img.getHeight() * -0.5f)
                .scaled (scale, scale)
                .rotated (angle)
                .translated (at.x, at.y);

        g.drawImageTransformed (img, place, false);
    }

    /** Flat graphic knob: a black disc, a black cog ring with a thin light
        outline, and a big brushed-metal centre. Drawn upright; only the white
        marker turns with `angle`. `R` is the outer radius. */
    void drawVectorKnob (juce::Graphics& g, juce::Point<float> centre, float R,
                         float angle, const PedalTheme& theme, bool enabled)
    {
        using juce::Colour;
        namespace Colours = juce::Colours;
        const float pi  = juce::MathConstants<float>::pi;
        const float tau = juce::MathConstants<float>::twoPi;

        // ---- proportions --------------------------------------------
        const float cogTip  = R * 0.85f;
        const float cogRoot = R * 0.70f;
        const float metalR  = R * 0.60f;
        const int   teeth   = 8;
        const float markIn  = R * 0.64f;
        const float markOut = R * 0.955f;
        const float markW   = R * 0.155f;

        auto disc = [centre] (float rad)
        { return juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (centre); };
        auto pol = [centre] (float rad, float th)
        { return juce::Point<float> (centre.x + rad * std::sin (th), centre.y - rad * std::cos (th)); };

        // gear outline radius as a function of angle: rounded teeth
        auto cogPath = [&] (float scaleTip, float scaleRoot)
        {
            juce::Path p;
            const int steps = 480;
            for (int i = 0; i <= steps; ++i)
            {
                const float th = tau * i / (float) steps;
                float f = 0.5f + 0.5f * std::cos (teeth * th);          // 1 tooth, 0 notch
                f = juce::jlimit (0.0f, 1.0f, (f - 0.16f) / 0.26f);      // wide flats, small notch
                f = f * f * (3.0f - 2.0f * f);                           // smoothstep -> rounded
                f = f * f * (3.0f - 2.0f * f);
                const float rr = cogRoot * scaleRoot + (cogTip * scaleTip - cogRoot * scaleRoot) * f;
                const auto pt = pol (rr, th);
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            p.closeSubPath();
            return p;
        };

        // ---- outer black disc -------------------------------------
        {
            juce::ColourGradient gd (Colour (0xff272727), centre.x, centre.y - R,
                                     Colour (0xff0c0c0c), centre.x, centre.y + R, false);
            gd.addColour (0.55, Colour (0xff161616));
            g.setGradientFill (gd);
            g.fillEllipse (disc (R));
            g.setColour (Colours::black.withAlpha (0.9f));
            g.drawEllipse (disc (R).reduced (0.75f), 1.5f);
        }

        // ---- cog ring -------------------------------------------
        const auto cog = cogPath (1.0f, 1.0f);
        g.setColour (Colour (0xff141414));
        g.fillPath (cog);

        // gentle top-to-bottom shading on the cog face
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (cog);
            juce::ColourGradient sh (Colours::white.withAlpha (0.05f), centre.x, centre.y - cogTip,
                                     Colours::transparentWhite, centre.x, centre.y, false);
            g.setGradientFill (sh);
            g.fillEllipse (disc (cogTip));
            juce::ColourGradient dk (Colours::transparentBlack, centre.x, centre.y,
                                     Colours::black.withAlpha (0.28f), centre.x, centre.y + cogTip, false);
            g.setGradientFill (dk);
            g.fillEllipse (disc (cogTip));
        }

        // thin light thread outline round the cog
        g.setColour (Colour (0xff9a9a9a).withAlpha (0.6f));
        g.strokePath (cog, juce::PathStrokeType (juce::jmax (1.2f, R * 0.016f)));

        // ---- recess the metal sits in --------------------------
        g.setColour (Colour (0xff0a0a0a));
        g.fillEllipse (disc (metalR + R * 0.035f));
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawEllipse (disc (metalR + R * 0.02f), juce::jmax (1.0f, R * 0.02f));

        // ---- brushed-metal centre -----------------------------
        {
            const auto mB = disc (metalR);

            juce::ColourGradient base (Colour (0xfff0f0f0), centre.x, centre.y,
                                       Colour (0xffa4a4a4), centre.x, centre.y + metalR, true);
            base.addColour (0.8, Colour (0xffc0c0c0));
            g.setGradientFill (base);
            g.fillEllipse (mB);

            // spun sheen: two bright sweeps, two dark, via thin wedges
            {
                juce::Graphics::ScopedSaveState s (g);
                juce::Path clip; clip.addEllipse (mB);
                g.reduceClipRegion (clip);
                const int wedges = 360;
                for (int i = 0; i < wedges; ++i)
                {
                    const float a0 = tau * i / (float) wedges;
                    const float a1 = tau * (i + 2.0f) / (float) wedges;   // overlap = smooth
                    float b = 0.64f + 0.26f * std::cos (2.0f * (a0 + 0.55f))
                                    + 0.07f * std::cos (a0 - 0.3f);
                    b = juce::jlimit (0.40f, 0.98f, b);
                    juce::Path w;
                    w.startNewSubPath (centre);
                    w.lineTo (pol (metalR * 1.05f, a0));
                    w.lineTo (pol (metalR * 1.05f, a1));
                    w.closeSubPath();
                    g.setColour (Colour::greyLevel (b).withAlpha (0.5f));
                    g.fillPath (w);
                }
                // fine turned rings
                juce::Random rng (0x9e3d);
                for (int i = 1; i <= 46; ++i)
                {
                    const float rr = metalR * (i / 47.0f);
                    g.setColour ((i % 2 ? Colours::white : Colours::black)
                                     .withAlpha (0.03f + 0.02f * rng.nextFloat()));
                    g.drawEllipse (disc (rr), 1.0f);
                }
                // slight darkening at the rim
                juce::ColourGradient vg (Colours::transparentBlack, centre.x, centre.y,
                                         Colours::black.withAlpha (0.18f), centre.x + metalR, centre.y, true);
                vg.addColour (0.8, Colours::transparentBlack);
                g.setGradientFill (vg);
                g.fillEllipse (mB);
            }

            g.setColour (Colour (0xff7c7c7c));
            g.drawEllipse (mB.reduced (1.0f), juce::jmax (1.0f, R * 0.008f));
            g.setColour (Colour (0xff0d0d0d));
            g.drawEllipse (mB.expanded (juce::jmax (1.0f, R * 0.006f)), juce::jmax (1.0f, R * 0.014f));
        }

        // ---- white marker -----------------------------------
        {
            juce::Path mk;
            mk.addRoundedRectangle (-markW * 0.5f, -(markOut - markIn),
                                    markW, markOut - markIn, markW * 0.28f);
            const auto t = juce::AffineTransform::rotation (angle)
                               .translated (pol ((markIn + markOut) * 0.5f, angle));
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillPath (mk, t.translated (0.0f, 1.6f));
            g.setColour (enabled ? Colours::white : Colours::white.withAlpha (0.5f));
            g.fillPath (mk, t);
            g.setColour (Colours::black.withAlpha (0.3f));
            g.strokePath (mk, juce::PathStrokeType (1.2f), t);
        }
    }
}

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

    // The digital cap owns its whole cell: the tick ring around it is the scale,
    // so none of the arc drawing below runs. `compactKnob` - the flag the analog
    // style uses to mean "utility cap" - is what picks the smaller of its two
    // sizes.
    if (theme.controlStyle == ControlStyle::digital)
    {
        const bool compactKnob = static_cast<bool> (slider.getProperties().getWithDefault ("compactKnob", false));

        DigitalKnob::EndMarker marker;
        if (const auto& argb = slider.getProperties()["endMarker"]; ! argb.isVoid())
        {
            marker.present = true;
            marker.colour = juce::Colour (static_cast<juce::uint32> (static_cast<int> (argb)));
            marker.label = slider.getProperties().getWithDefault ("endMarkerLabel", juce::String()).toString();
        }

        DigitalKnob::draw (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle,
                           compactKnob ? DigitalKnob::Size::small
                                       : DigitalKnob::sizeForDiameter (juce::roundToInt (diameter)),
                           theme, slider.isEnabled(), marker);
        return;
    }

    const bool inverted = static_cast<bool> (slider.getProperties().getWithDefault ("invertedArc", false));

    // A centre-detent control (one knob carrying a low cut one way and a high
    // cut the other) grows its arc out of 12 o'clock in whichever direction it
    // is turned, rather than filling from the minimum.
    const bool bipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolarArc", false));
    const float centreAngle = (rotaryStartAngle + rotaryEndAngle) * 0.5f;

    // The wet/dry knob swaps its position dot for a small spoon pointer.
    const bool spoonPointer = static_cast<bool> (slider.getProperties().getWithDefault ("spoonPointer", false));

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
    else if (bipolar)
    {
        // A tick at 12 o'clock, so the detent reads even with the arc empty.
        const float tickInner = arcRadius - track * 0.5f - 2.0f;
        const float tickOuter = arcRadius + track * 0.5f + 2.0f;
        const auto tickDirection = juce::Point<float> (std::sin (centreAngle), -std::cos (centreAngle));

        g.setColour (theme.textSecondary.withAlpha (0.7f));
        g.drawLine (juce::Line<float> (centre + tickDirection * tickInner,
                                       centre + tickDirection * tickOuter), 1.6f);

        if (std::abs (angle - centreAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (centreAngle, angle), juce::jmax (centreAngle, angle), true);

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

    // Full-size knobs get the vector knob, drawn upright and lit from the top so
    // only the position line turns. Compact utility knobs keep the vector cap
    // below. The value arc is already drawn underneath, so its ring still shows.
    const bool compactKnob = static_cast<bool> (slider.getProperties().getWithDefault ("compactKnob", false));

    if (! compactKnob)
    {
        if (const auto img = defaultKnobImage(); img.isValid())
            drawImageKnob (g, centre, arcRadius * kKnobSizeFrac, angle, img, theme.knobBody);
        else
            drawVectorKnob (g, centre, arcRadius * kKnobSizeFrac, angle, theme, slider.isEnabled());
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

    if (spoonPointer)
    {
        // Spans from the knob centre out to most of the ring radius, so it clears
        // the rim by a comfortable margin.
        drawSpoonPointer (g, centre, angle, ringRadius * 0.82f);
        return;
    }

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
