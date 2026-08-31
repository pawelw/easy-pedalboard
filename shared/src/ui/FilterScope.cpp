#include "ee/ui/FilterScope.h"

#include <cmath>

namespace ee::ui
{
namespace
{
    constexpr float kCornerRadius = 6.0f;
    constexpr float kPad = 6.0f;

    constexpr float kFMin = 30.0f;
    constexpr float kFMax = 18000.0f;
    constexpr float kDbFloor = -2.0f;
    constexpr float kDbCeil = 15.0f;

    // Defaults when the spec names no colours: the static curve reads as
    // "frequency" whatever the pedal's theme, the moving pair as left / right.
    const juce::Colour kBaseColour  { 0xff5ac8e6 };   // blue
    const juce::Colour kLeftColour  { 0xffe8934a };   // orange
    const juce::Colour kRightColour { 0xffd8b24a };   // amber

    float call (const std::function<float()>& fn, float fallback)
    {
        if (! fn)
            return fallback;
        const float v = fn();
        return std::isfinite (v) ? v : fallback;
    }
}

FilterScope::FilterScope (const FilterScopeSpec& specToUse, const PedalTheme& themeToUse)
    : spec (specToUse), theme (themeToUse)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (45);
}

FilterScope::~FilterScope()
{
    stopTimer();
}

void FilterScope::timerCallback()
{
    repaint();
}

void FilterScope::drawBump (juce::Graphics& g, juce::Rectangle<float> plot,
                            float fcHz, float peakDb, float bw,
                            juce::Colour colour, float thickness) const
{
    fcHz = juce::jlimit (kFMin, kFMax, fcHz);

    const float logMin = std::log (kFMin);
    const float logSpan = std::log (kFMax) - logMin;
    const float logFc = std::log (fcHz);

    juce::Path p;
    const int steps = juce::jmax (2, juce::roundToInt (plot.getWidth()));
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (steps);
        const float lf = logMin + t * logSpan;
        const float d = (lf - logFc) / bw;
        const float db = peakDb * std::exp (-0.5f * d * d);

        const float x = plot.getX() + t * plot.getWidth();
        const float y = juce::jmap (juce::jlimit (kDbFloor, kDbCeil, db),
                                    kDbFloor, kDbCeil, plot.getBottom(), plot.getY());
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

void FilterScope::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    if (bounds.isEmpty())
        return;

    // Recessed panel, matching the framing the other displays use.
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (bounds, kCornerRadius);
    g.setColour (theme.outline.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds, kCornerRadius, 1.0f);

    const auto plot = bounds.reduced (kPad);
    if (plot.isEmpty())
        return;

    const float logMin = std::log (kFMin);
    const float logSpan = std::log (kFMax) - logMin;

    // Decade grid.
    g.setColour (theme.title.withAlpha (0.12f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = plot.getX() + (std::log (hz) - logMin) / logSpan * plot.getWidth();
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }
    const float baseline = juce::jmap (0.0f, kDbFloor, kDbCeil, plot.getBottom(), plot.getY());
    g.setColour (theme.title.withAlpha (0.18f));
    g.drawHorizontalLine (juce::roundToInt (baseline), plot.getX(), plot.getRight());

    const float res01 = juce::jlimit (0.0f, 1.0f, call (spec.resonance01, 0.5f));
    const float peakDb = juce::jmap (res01, 0.0f, 1.0f, 3.0f, 22.0f);
    const float bw     = juce::jmap (res01, 0.0f, 1.0f, 0.85f, 0.18f);

    const float baseHz = juce::jlimit (kFMin, kFMax, call (spec.baseFreqHz, 500.0f));
    const float fcL = baseHz * std::pow (spec.sweepRatioMax, call (spec.modL, 0.0f));
    const float fcR = baseHz * std::pow (spec.sweepRatioMax, call (spec.modR, 0.0f));

    const auto baseColour = spec.baseColour.value_or (kBaseColour);
    const auto leftColour  = spec.sweepColour.value_or (kLeftColour);
    const auto rightColour = spec.sweepColour.value_or (kRightColour);

    // Moving curves under the static one.
    drawBump (g, plot, fcL, peakDb, bw, leftColour.withAlpha (0.9f), 1.8f);
    drawBump (g, plot, fcR, peakDb, bw, rightColour.withAlpha (0.9f), 1.8f);
    drawBump (g, plot, baseHz, peakDb, bw, baseColour, 2.2f);

    // Dot at the static curve's apex - rises with resonance, still unless Freq
    // or Q move.
    const float dotX = plot.getX() + (std::log (baseHz) - logMin) / logSpan * plot.getWidth();
    const float dotY = juce::jmap (juce::jlimit (kDbFloor, kDbCeil, peakDb),
                                   kDbFloor, kDbCeil, plot.getBottom(), plot.getY());
    g.setColour (baseColour);
    g.fillEllipse (dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse (dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f, 1.0f);
}

} // namespace ee::ui
