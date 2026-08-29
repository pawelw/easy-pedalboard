#include "ee/ui/PedalEditor.h"

#include "BinaryData.h"

#include <algorithm>
#include <cmath>

namespace ee::ui
{
namespace
{
    constexpr float kBorderThickness = 5.0f; // frame hugging the outer edge
    constexpr float kFaceInset = kBorderThickness;
    constexpr int kShadowDepth = 12;         // how far the face is sunk below the frame

    // Everything on the face is spaced from the inside edge of the frame.
    constexpr int kContentPad = 16;
    constexpr int kMargin = static_cast<int> (kFaceInset) + kContentPad;

    constexpr int kKnobGap = 12;

    // Faders sit in one row below the knobs (or fill the whole control area on
    // a pedal that has no knobs). Fixed cap width so a fader is the same size
    // however many share the row.
    constexpr int kFaderWidth = 48;   // strip width; the cap drawn inside is narrower
    constexpr int kFaderRowGap = 12;

    // Strip carved off the top of the fader band for the reset button and any
    // corner knobs.
    constexpr int kResetStripHeight = 24;
    constexpr int kResetButtonWidth = 54;
    constexpr int kResetButtonHeight = 20;

    // Small knobs pinned top-right. The component is wider than the cap so the
    // "4.5 kHz" style readout fits underneath.
    constexpr int kCornerKnobDiameter = 44;
    constexpr int kCornerKnobWidth = 60;
    constexpr int kCornerKnobGap = 6;

    // Sits above the middle of the rotaries, where the caps have curved away
    // and there is more room either side of it.
    constexpr int kToggleRise = 18;

    // Fixed rather than a fraction of the column, so a knob is the same size on
    // every pedal however many of them a row carries.
    constexpr int kKnobDiameter = 114;

    constexpr int kTitleHeight = 64;
    constexpr int kLogoHeight = 54;

    juce::Image brandLogo()
    {
        static const juce::Image logo = juce::ImageCache::getFromMemory (BinaryData::rocketlogo_png,
                                                                        BinaryData::rocketlogo_pngSize);
        return logo;
    }

    /** Keeps the artwork's shape and replaces its colour. */
    juce::Image tinted (const juce::Image& source, juce::Colour colour)
    {
        juce::Image out (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);

        const juce::Image::BitmapData src (source, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < source.getHeight(); ++y)
            for (int x = 0; x < source.getWidth(); ++x)
                dst.setPixelColour (x, y, colour.withAlpha (src.getPixelColour (x, y).getFloatAlpha()));

        return out;
    }
}

PedalEditor::PedalEditor (juce::AudioProcessor& processor,
                          juce::AudioProcessorValueTreeState& state,
                          PedalSpec specToUse,
                          PedalTheme themeToUse)
    : juce::AudioProcessorEditor (processor),
      theme (std::move (themeToUse)),
      spec (std::move (specToUse)),
      lookAndFeel (theme)
{
    setLookAndFeel (&lookAndFeel);

    for (const auto& knobSpec : spec.knobs)
        knobs.push_back (std::make_unique<Knob> (state, knobSpec, theme));

    for (auto& knob : knobs)
        addAndMakeVisible (*knob);

    for (const auto& sliderSpec : spec.sliders)
        faders.push_back (std::make_unique<FaderStrip> (state, sliderSpec, theme));

    for (auto& fader : faders)
        addAndMakeVisible (*fader);

    for (const auto& knobSpec : spec.cornerKnobs)
    {
        auto knob = std::make_unique<Knob> (state, knobSpec, theme);
        knob->onValueChanged = [this] { repaint(); };   // the cut masks track it
        addAndMakeVisible (*knob);
        cornerKnobs.push_back (std::move (knob));
    }

    // A pedal driven by faders gets a reset that flattens them all. It lives in
    // the name row, under the first fader.
    if (! faders.empty())
    {
        faderResetButton = std::make_unique<juce::TextButton> ("RESET");
        faderResetButton->setWantsKeyboardFocus (false);
        // Small dark pill with a light label.
        faderResetButton->setColour (juce::TextButton::buttonColourId, theme.knobBody);
        faderResetButton->setColour (juce::TextButton::textColourOffId, juce::Colour (0xfff2f4f6));
        faderResetButton->onClick = [this] { resetFaders(); };
        addAndMakeVisible (*faderResetButton);
    }

    // Added after the knobs so they sit on top where the two bounds overlap.
    for (const auto& toggleSpec : spec.toggles)
    {
        toggles.push_back (std::make_unique<MiniToggle> (state, toggleSpec, theme));
        addAndMakeVisible (*toggles.back());
    }

    setSize (spec.width, spec.height);

    logoImage = brandLogo();

    if (theme.logoTint.has_value() && logoImage.isValid())
        logoImage = tinted (logoImage, *theme.logoTint);

    if (theme.grain > 0.0f)
    {
        // Baked once: a speckle pass over the whole face costs far too much to
        // redraw on every repaint.
        grain = juce::Image (juce::Image::ARGB, spec.width, spec.height, true);
        juce::Random rng (0x5eed);

        for (int y = 0; y < spec.height; ++y)
            for (int x = 0; x < spec.width; ++x)
            {
                const float n = rng.nextFloat() - 0.5f;
                const auto a = static_cast<juce::uint8> (juce::jlimit (0.0f, 255.0f, std::abs (n) * theme.grain * 26.0f));
                grain.setPixelAt (x, y, (n < 0.0f ? juce::Colours::black : juce::Colours::white).withAlpha (a));
            }
    }
}

PedalEditor::~PedalEditor()
{
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> PedalEditor::faceBounds() const
{
    return getLocalBounds().withWidth (spec.width);
}

void PedalEditor::setSidePanel (std::unique_ptr<juce::Component> panel, int panelWidth)
{
    sidePanel = std::move (panel);

    if (sidePanel != nullptr)
    {
        addAndMakeVisible (*sidePanel);
        setSize (spec.width + panelWidth, spec.height);
    }
}

juce::Rectangle<int> PedalEditor::logoArea() const
{
    return faceBounds().reduced (kMargin).removeFromBottom (kLogoHeight);
}

juce::Rectangle<int> PedalEditor::titleArea() const
{
    auto area = faceBounds().reduced (kMargin);
    area.removeFromBottom (kLogoHeight);
    return area.removeFromBottom (kTitleHeight);
}

juce::Rectangle<int> PedalEditor::knobArea() const
{
    auto area = faceBounds().reduced (kMargin);
    area.removeFromBottom (kLogoHeight + kTitleHeight);
    return area;
}

juce::Rectangle<int> PedalEditor::faderArea() const
{
    if (faders.empty())
        return {};

    auto r = faders.front()->getBounds();
    for (const auto& fader : faders)
        r = r.getUnion (fader->getBounds());

    return r.withTrimmedBottom (FaderStrip::labelHeight);
}

void PedalEditor::paintFaderGraph (juce::Graphics& g) const
{
    if (faders.empty())
        return;

    const auto area = faderArea().toFloat();
    if (area.isEmpty())
        return;

    // Rules line up with the node travel, so the top and bottom rule meet the
    // extremes of the faders.
    const auto travel = faderTrackRange (area);

    constexpr float kGridCorner = 6.0f;

    const juce::Rectangle<float> gridBounds (area.getX(), travel.getStart(),
                                             area.getWidth(), travel.getLength());

    juce::Path gridClip;
    gridClip.addRoundedRectangle (gridBounds, kGridCorner);

    // A hair of a frame around the ruled area, drawn before the clip so its
    // full stroke shows.
    g.setColour (theme.outline.withAlpha (0.3f));
    g.drawRoundedRectangle (gridBounds, kGridCorner, 1.0f);

    // Everything inside the grid is clipped to the rounded frame so no rule,
    // shading or curve tail runs past a corner.
    juce::Graphics::ScopedSaveState gridState (g);
    g.reduceClipRegion (gridClip);

    // Shading for the trimmed ends of the spectrum.
    paintCutMasks (g, gridBounds);

    // Horizontal rules, one per 5 dB step across a +/-15 dB face, the middle
    // one (0 dB) a touch stronger. The top and bottom steps are left to the
    // frame.
    constexpr int kHLines = 7;
    for (int i = 1; i < kHLines - 1; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kHLines - 1);
        const float ly = travel.getStart() + t * travel.getLength();
        const bool centre = (i == kHLines / 2);

        g.setColour (theme.outline.withAlpha (centre ? 0.5f : 0.22f));
        g.fillRect (juce::Rectangle<float> (area.getX(), ly - 0.5f, area.getWidth(), 1.0f));
    }

    // Vertical rule under every fader; gather the band faders (a level fader is
    // skipped) with the frequency and gain each one carries.
    struct Band { float x, hz, db; };
    std::vector<Band> bands;
    std::vector<juce::Point<float>> nodes;   // fallback when no frequencies are given
    bool haveFrequencies = true;

    for (size_t i = 0; i < faders.size(); ++i)
    {
        const auto node = faders[i]->nodeCentreInParent();

        g.setColour (theme.outline.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (node.x - 0.5f, travel.getStart(), 1.0f, travel.getLength()));

        const bool onCurve = (i >= spec.sliders.size() || spec.sliders[i].joinCurve);
        if (! onCurve)
            continue;

        nodes.push_back (node);

        const float hz = (i < spec.sliders.size()) ? spec.sliders[i].axisHz : 0.0f;
        if (hz > 0.0f)
            bands.push_back ({ node.x, hz,
                               static_cast<float> (faders[i]->getSlider().getValue()) });
        else
            haveFrequencies = false;
    }

    const auto unit = [] (juce::Point<float> v)
    {
        const float len = juce::jmax (1.0e-4f, v.getDistanceFromOrigin());
        return juce::Point<float> (v.x / len, v.y / len);
    };

    // Eased polyline: straight between points, corners rounded off.
    const auto easedPath = [&unit] (const std::vector<juce::Point<float>>& p)
    {
        juce::Path curve;
        if (p.size() < 2)
            return curve;

        constexpr float kCornerRadius = 8.0f;
        curve.startNewSubPath (p.front());

        for (size_t i = 1; i + 1 < p.size(); ++i)
        {
            const auto prev = p[i - 1], cur = p[i], next = p[i + 1];
            const float rIn  = juce::jmin (kCornerRadius, (prev - cur).getDistanceFromOrigin() * 0.45f);
            const float rOut = juce::jmin (kCornerRadius, (next - cur).getDistanceFromOrigin() * 0.45f);
            curve.lineTo (cur + unit (prev - cur) * rIn);
            curve.quadraticTo (cur, cur + unit (next - cur) * rOut);
        }

        curve.lineTo (p.back());
        return curve;
    };

    std::vector<juce::Point<float>> points;

    if (haveFrequencies && bands.size() >= 2)
    {
        // Map between x and log2(frequency) along the band anchors, and read the
        // fader gain at any frequency by interpolating between them.
        const auto lg = [] (float hz) { return std::log2 (juce::jmax (hz, 1.0f)); };

        const auto xForHz = [&] (float hz)
        {
            const float L = lg (hz);
            if (L <= lg (bands.front().hz))
            {
                const auto& a = bands[0]; const auto& b = bands[1];
                return a.x + (b.x - a.x) * (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
            }
            for (size_t i = 1; i < bands.size(); ++i)
                if (L <= lg (bands[i].hz))
                {
                    const auto& a = bands[i - 1]; const auto& b = bands[i];
                    return a.x + (b.x - a.x) * (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
                }
            const auto& a = bands[bands.size() - 2]; const auto& b = bands.back();
            return b.x + (b.x - a.x) * (L - lg (b.hz)) / (lg (b.hz) - lg (a.hz));
        };

        const auto gainDbAtHz = [&] (float hz)
        {
            const float L = lg (hz);
            if (L <= lg (bands.front().hz)) return bands.front().db;
            if (L >= lg (bands.back().hz))  return bands.back().db;
            for (size_t i = 1; i < bands.size(); ++i)
                if (L <= lg (bands[i].hz))
                {
                    const auto& a = bands[i - 1]; const auto& b = bands[i];
                    const float t = (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
                    return a.db + t * (b.db - a.db);
                }
            return bands.back().db;
        };

        // Active cut frequencies.
        float loHz = 0.0f, hiHz = 0.0f;
        for (size_t i = 0; i < cornerKnobs.size() && i < spec.cornerKnobs.size(); ++i)
        {
            const auto side = spec.cornerKnobs[i].cutSide;
            if (side == CutSide::none)
                continue;

            auto& s = cornerKnobs[i]->getSlider();
            const auto v = static_cast<float> (s.getValue());
            if (side == CutSide::low  && v > static_cast<float> (s.getMinimum()) + 0.5f) loHz = v;
            if (side == CutSide::high && v < static_cast<float> (s.getMaximum()) - 0.5f) hiHz = v;
        }

        constexpr float kCutSlopeDbPerOct = 14.0f;
        const auto rolloffDb = [&] (float hz)
        {
            float d = 0.0f;
            if (loHz > 0.0f && hz < loHz) d -= kCutSlopeDbPerOct * std::log2 (loHz / juce::jmax (hz, 1.0f));
            if (hiHz > 0.0f && hz > hiHz) d -= kCutSlopeDbPerOct * std::log2 (hz / hiHz);
            return d;
        };

        const float midY = travel.getStart() + travel.getLength() * 0.5f;
        const float pxPerDb = travel.getLength() / 30.0f;   // +/-15 dB across the grid
        const auto yForDb = [&] (float db) { return midY - db * pxPerDb; };
        const auto pointAtHz = [&] (float hz)
        {
            return juce::Point<float> (xForHz (hz), yForDb (gainDbAtHz (hz) + rolloffDb (hz)));
        };

        // Low-cut tail: octave steps down from the corner, until well off-grid.
        if (loHz > 0.0f)
            for (float f = loHz; f >= 15.0f; f *= 0.5f)
            {
                const auto p = pointAtHz (f);
                points.push_back (p);
                if (p.x < area.getX() - 24.0f || p.y > travel.getEnd() + 90.0f)
                    break;
            }

        for (const auto& b : bands)
            points.push_back ({ b.x, yForDb (b.db + rolloffDb (b.hz)) });

        // High-cut tail: octave steps up from the corner.
        if (hiHz > 0.0f)
            for (float f = hiHz; f <= 22000.0f; f *= 2.0f)
            {
                const auto p = pointAtHz (f);
                points.push_back (p);
                if (p.x > area.getRight() + 24.0f || p.y > travel.getEnd() + 90.0f)
                    break;
            }

        std::sort (points.begin(), points.end(),
                   [] (auto& a, auto& b) { return a.x < b.x; });
        points.erase (std::unique (points.begin(), points.end(),
                                   [] (auto& a, auto& b) { return std::abs (a.x - b.x) < 1.0f; }),
                      points.end());
    }
    else
    {
        points = nodes;
    }

    if (points.size() >= 2)
    {
        // The outer clip already confines the tail to the rounded grid, so it
        // visibly runs off the bottom.
        g.setColour (kFaderCurveColour);
        g.strokePath (easedPath (points),
                      juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    }
}

void PedalEditor::paintCutMasks (juce::Graphics& g, juce::Rectangle<float> grid) const
{
    // A shared log-frequency axis across every cut knob's full range, so the
    // low- and high-cut shading land on the same x where their ranges meet.
    float axisMinHz = 0.0f;
    float axisMaxHz = 0.0f;

    for (size_t i = 0; i < cornerKnobs.size() && i < spec.cornerKnobs.size(); ++i)
    {
        if (spec.cornerKnobs[i].cutSide == CutSide::none)
            continue;

        auto& s = cornerKnobs[i]->getSlider();
        const auto lo = static_cast<float> (s.getMinimum());
        const auto hi = static_cast<float> (s.getMaximum());

        axisMinHz = (axisMinHz <= 0.0f) ? lo : juce::jmin (axisMinHz, lo);
        axisMaxHz = juce::jmax (axisMaxHz, hi);
    }

    if (axisMinHz <= 0.0f || axisMaxHz <= axisMinHz)
        return;

    const float logMin = std::log (axisMinHz);
    const float logSpan = std::log (axisMaxHz) - logMin;

    const auto xForHz = [&] (float hz)
    {
        const float t = juce::jlimit (0.0f, 1.0f,
                                      (std::log (juce::jmax (hz, 1.0f)) - logMin) / logSpan);
        return grid.getX() + t * grid.getWidth();
    };

    for (size_t i = 0; i < cornerKnobs.size() && i < spec.cornerKnobs.size(); ++i)
    {
        const auto side = spec.cornerKnobs[i].cutSide;
        if (side == CutSide::none)
            continue;

        const float edgeX = xForHz (static_cast<float> (cornerKnobs[i]->getSlider().getValue()));
        const bool fromLeft = (side == CutSide::low);

        const auto band = fromLeft ? grid.withRight (edgeX) : grid.withLeft (edgeX);
        if (band.getWidth() < 0.5f)
            continue;

        // Dense at the outer edge the shading grew from, fading to nothing at
        // the cutoff so there is no visible line where it stops.
        juce::ColourGradient grad (juce::Colours::black.withAlpha (0.16f),
                                   fromLeft ? band.getX() : band.getRight(), band.getCentreY(),
                                   juce::Colours::transparentBlack,
                                   fromLeft ? band.getRight() : band.getX(), band.getCentreY(),
                                   false);
        g.setGradientFill (grad);
        g.fillRect (band);
    }
}

void PedalEditor::resetFaders()
{
    for (auto& fader : faders)
    {
        auto& s = fader->getSlider();
        s.setValue (s.getDoubleClickReturnValue(), juce::sendNotificationSync);
    }
}

void PedalEditor::layOutFaders (juce::Rectangle<int> area)
{
    const int count = static_cast<int> (faders.size());

    if (count == 0 || area.isEmpty())
        return;

    // Trim a little off the bottom of the fader/grid band, leaving a gap
    // between the value readouts and the pedal name.
    area = area.removeFromTop (juce::roundToInt (area.getHeight() * 0.95f));

    // Even columns across the area; the fader sits centred in its column at a
    // fixed width, spanning the full height so the throw is as long as the face
    // allows.
    const int cellWidth = area.getWidth() / count;
    const int faderWidth = juce::jmin (kFaderWidth, cellWidth);

    for (int i = 0; i < count; ++i)
    {
        auto cell = area.removeFromLeft (i < count - 1 ? cellWidth : area.getWidth());
        faders[static_cast<size_t> (i)]->setBounds (cell.withSizeKeepingCentre (faderWidth, cell.getHeight()));
    }
}

void PedalEditor::paint (juce::Graphics& g)
{
    auto bounds = faceBounds().toFloat();

    if (theme.backgroundImage.isValid())
    {
        g.drawImage (theme.backgroundImage, bounds, juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.fillAll (theme.background);

        const auto face = bounds.reduced (kFaceInset);
        g.setColour (theme.panel);
        g.fillRoundedRectangle (face, theme.cornerRadius);

        if (grain.isValid())
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path rounded;
            rounded.addRoundedRectangle (face, theme.cornerRadius);
            g.reduceClipRegion (rounded);
            g.setOpacity (1.0f);
            g.drawImageAt (grain, 0, 0, true);
        }

        // Shadow cast inwards by the frame, so the face reads as recessed.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path rounded;
            rounded.addRoundedRectangle (face, theme.cornerRadius);
            g.reduceClipRegion (rounded);

            for (int i = 0; i < kShadowDepth; ++i)
            {
                const float fade = 1.0f - static_cast<float> (i) / static_cast<float> (kShadowDepth);
                g.setColour (juce::Colours::black.withAlpha (0.30f * fade * fade));
                g.drawRoundedRectangle (face.reduced (0.5f + static_cast<float> (i)),
                                        theme.cornerRadius, 1.6f);
            }
        }

        g.setColour (theme.bezel);
        g.drawRoundedRectangle (bounds.reduced (kBorderThickness * 0.5f),
                                theme.cornerRadius + kBorderThickness * 0.5f,
                                kBorderThickness);
    }

    // Sits under the fader children, which paint their stems and nodes on top.
    paintFaderGraph (g);

    // Name sits under the knobs, the way it is screened onto a real pedal.
    // That also keeps the top of the face free instead of carrying a header.
    g.setColour (theme.title);
    g.setFont (theme.titleFont (58.0f));
    g.drawText (spec.name, titleArea(), juce::Justification::centred, false);

    if (const auto logo = logoImage; logo.isValid())
    {
        g.setOpacity (0.92f);
        g.drawImage (logo, logoArea().toFloat(), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    if (spec.version.isNotEmpty())
    {
        g.setColour (theme.textSecondary.withAlpha (0.7f));
        g.setFont (theme.bodyFont (10.5f));
        g.drawText (spec.version,
                    faceBounds().reduced (kMargin),
                    juce::Justification::bottomLeft,
                    false);
    }
}

void PedalEditor::resized()
{
    if (sidePanel != nullptr)
        sidePanel->setBounds (getLocalBounds().withTrimmedLeft (spec.width));

    auto area = knobArea();

    const int count = static_cast<int> (knobs.size());
    const int perRow = juce::jlimit (1, juce::jmax (1, count), spec.knobsPerRow);

    // Columns span the full content width; the rotary sits centred in its
    // column at a fixed size.
    const int cellWidth = (area.getWidth() - kKnobGap * (perRow - 1)) / perRow;
    const int knobWidth = juce::jmin (kKnobDiameter, cellWidth);
    const int rowHeight = knobWidth + Knob::labelHeight;

    for (int first = 0; first < count; first += perRow)
    {
        auto knobRow = area.removeFromTop (rowHeight);
        area.removeFromTop (kKnobGap);

        const int inRow = juce::jmin (perRow, count - first);

        for (int i = 0; i < inRow; ++i)
        {
            auto cell = knobRow.removeFromLeft (cellWidth);
            knobs[static_cast<size_t> (first + i)]->setBounds (cell.withSizeKeepingCentre (knobWidth, rowHeight));
            if (i < inRow - 1)
                knobRow.removeFromLeft (kKnobGap);
        }
    }

    // Faders take whatever is left below the knob rows. On a knob-less pedal
    // (a graphic EQ) that is the whole control area.
    if (! faders.empty())
    {
        if (count > 0)
            area.removeFromTop (kFaderRowGap);

        // Reset (hard left) and any corner cut knobs (hard right) share a strip
        // in the gap above the grid.
        int stripH = 0;
        if (faderResetButton != nullptr)
            stripH = juce::jmax (stripH, kResetStripHeight);
        if (! cornerKnobs.empty())
            stripH = juce::jmax (stripH, kCornerKnobDiameter + Knob::compactLabelHeight);

        if (stripH > 0)
        {
            const auto strip = area.removeFromTop (stripH);

            // Line the reset button up with the centre of the corner knob caps,
            // not the centre of the whole strip (the caps sit above their
            // value readouts).
            const int controlsCentreY = cornerKnobs.empty()
                ? strip.getCentreY()
                : strip.getY() + kCornerKnobDiameter / 2;

            if (faderResetButton != nullptr)
                faderResetButton->setBounds (
                    juce::Rectangle<int> (kResetButtonWidth, kResetButtonHeight)
                        .withPosition (strip.getX(),
                                       controlsCentreY - kResetButtonHeight / 2));

            int x = strip.getRight() - kCornerKnobWidth;
            for (auto it = cornerKnobs.rbegin(); it != cornerKnobs.rend(); ++it)
            {
                (*it)->setBounds (x, strip.getY(), kCornerKnobWidth,
                                  kCornerKnobDiameter + Knob::compactLabelHeight);
                x -= kCornerKnobWidth + kCornerKnobGap;
            }
        }

        layOutFaders (area);
    }

    // A toggle straddles the gap between two knobs of the same row, centred on
    // the rotaries rather than on the cell, so it lines up with the caps.
    for (size_t t = 0; t < toggles.size(); ++t)
    {
        const int index = spec.toggles[t].afterKnobIndex;

        if (index < 0 || index + 1 >= count)
        {
            toggles[t]->setVisible (false);
            continue;
        }

        const auto left = knobs[static_cast<size_t> (index)]->getBounds();
        const auto right = knobs[static_cast<size_t> (index + 1)]->getBounds();

        if (right.getY() != left.getY())
        {
            toggles[t]->setVisible (false);
            continue;
        }

        const auto centre = juce::Point<int> ((left.getRight() + right.getX()) / 2,
                                              left.getY() + (left.getHeight() - Knob::labelHeight) / 2
                                                  - kToggleRise);

        toggles[t]->setVisible (true);
        toggles[t]->setBounds (juce::Rectangle<int> (MiniToggle::preferredWidth,
                                                     MiniToggle::preferredHeight)
                                   .withCentre (centre));
    }
}

} // namespace ee::ui
