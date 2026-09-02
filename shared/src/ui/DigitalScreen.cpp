#include "ee/ui/DigitalScreen.h"

#include <cmath>

namespace ee::ui
{
namespace
{
    constexpr float kCornerRadius = 10.0f;
    constexpr float kPad = 8.0f;
    constexpr float kCaptionHeight = 9.5f;

    /** A frequency as it is captioned on the axis: "500Hz", "10kHz". */
    juce::String frequencyCaption (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz < 10000.0f && std::fmod (hz, 1000.0f) != 0.0f ? 1 : 0) + "kHz"
                             : juce::String (juce::roundToInt (hz)) + "Hz";
    }

    void drawDashedHorizontal (juce::Graphics& g, float y, float x0, float x1, float thickness)
    {
        const float dashes[] = { 4.0f, 4.0f };
        juce::Line<float> line (x0, y, x1, y);
        g.drawDashedLine (line, dashes, 2, thickness);
    }
}

juce::Rectangle<float> DigitalScreen::paintPanel (juce::Graphics& g,
                                                  juce::Rectangle<float> bounds,
                                                  const PedalTheme& theme)
{
    const auto panel = bounds.reduced (1.0f);
    if (panel.isEmpty())
        return {};

    // The screen is the one thing on the face that sits below it rather than
    // above, so its shadow falls inwards from the top edge.
    g.setColour (theme.recess);
    g.fillRoundedRectangle (panel, kCornerRadius);

    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path rounded;
        rounded.addRoundedRectangle (panel, kCornerRadius);
        g.reduceClipRegion (rounded);

        for (int i = 0; i < 3; ++i)
        {
            const float fade = 1.0f - static_cast<float> (i) / 3.0f;
            g.setColour (theme.softShadow.withMultipliedAlpha (0.55f * fade));
            g.drawRoundedRectangle (panel.translated (0.0f, 0.8f).reduced (static_cast<float> (i)),
                                    kCornerRadius, 1.2f);
        }
    }

    // A hairline of the face's own light along the bottom lip, which is what
    // reads as depth once the shadow above it is in place.
    g.setColour (theme.softHighlight.withAlpha (0.7f));
    g.drawRoundedRectangle (panel.translated (0.0f, 1.0f), kCornerRadius, 1.0f);

    return panel.reduced (kPad);
}

void DigitalScreen::paintLevelGrid (juce::Graphics& g,
                                    juce::Rectangle<float> plot,
                                    const PedalTheme& theme,
                                    float dbFloor,
                                    float dbCeil,
                                    std::initializer_list<float> levelsDb)
{
    if (plot.isEmpty() || dbCeil <= dbFloor)
        return;

    g.setFont (theme.bodyFont (kCaptionHeight));

    for (const float db : levelsDb)
    {
        const float y = juce::jmap (juce::jlimit (dbFloor, dbCeil, db),
                                    dbFloor, dbCeil, plot.getBottom(), plot.getY());

        const bool isZero = std::abs (db) < 0.001f;

        g.setColour (theme.recessInk.withAlpha (isZero ? 0.8f : 0.55f));
        if (isZero)
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f);
        else
            drawDashedHorizontal (g, y, plot.getX(), plot.getRight(), 1.0f);

        // Caption sits on the line, in the gutter to the left of the plot, so a
        // trace running along a rule never covers its own label.
        const auto caption = (db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db));

        g.setColour (theme.recessInk);
        g.drawText (caption,
                    juce::Rectangle<float> (plot.getX() - kLabelGutterLeft,
                                            y - kCaptionHeight,
                                            kLabelGutterLeft - 4.0f,
                                            kCaptionHeight * 2.0f),
                    juce::Justification::centredRight, false);
    }
}

void DigitalScreen::paintFrequencyGrid (juce::Graphics& g,
                                        juce::Rectangle<float> plot,
                                        const PedalTheme& theme,
                                        float fMinHz,
                                        float fMaxHz,
                                        std::initializer_list<float> marksHz)
{
    if (plot.isEmpty() || fMaxHz <= fMinHz || fMinHz <= 0.0f)
        return;

    const float logMin = std::log (fMinHz);
    const float logSpan = std::log (fMaxHz) - logMin;

    g.setFont (theme.bodyFont (kCaptionHeight));

    for (const float hz : marksHz)
    {
        if (hz <= fMinHz || hz >= fMaxHz)
            continue;

        const float x = plot.getX() + (std::log (hz) - logMin) / logSpan * plot.getWidth();

        g.setColour (theme.recessInk.withAlpha (0.45f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);

        g.setColour (theme.recessInk);
        g.drawText (frequencyCaption (hz),
                    juce::Rectangle<float> (x - 24.0f, plot.getBottom(), 48.0f, kLabelGutterBottom),
                    juce::Justification::centred, false);
    }
}

} // namespace ee::ui
