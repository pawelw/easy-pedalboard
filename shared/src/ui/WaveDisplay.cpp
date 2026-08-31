#include "ee/ui/WaveDisplay.h"

#include "ee/dsp/Lfo.h"

namespace ee::ui
{
namespace
{
    constexpr float kCornerRadius = 6.0f;
    constexpr float kVerticalPad = 7.0f;   // keeps the peaks off the frame

    // Rate is a 0..1 knob; map it to how many LFO cycles fill the width, so a
    // faster rate visibly packs more waves in.
    constexpr float kMinCycles = 1.5f;
    constexpr float kMaxCycles = 6.0f;
}

WaveDisplay::WaveDisplay (juce::AudioProcessorValueTreeState& state,
                          const WaveDisplaySpec& specToUse,
                          const PedalTheme& themeToUse)
    : apvts (state), spec (specToUse), theme (themeToUse)
{
    setInterceptsMouseClicks (false, false);

    for (const auto* id : { &spec.amountID, &spec.rateID, &spec.shapeID, &spec.modeID })
        if (id->isNotEmpty())
            apvts.addParameterListener (*id, this);

    // A live scope repaints on its own clock rather than only on knob moves.
    if (spec.livePhase)
        startTimerHz (45);
}

WaveDisplay::~WaveDisplay()
{
    stopTimer();

    for (const auto* id : { &spec.amountID, &spec.rateID, &spec.shapeID, &spec.modeID })
        if (id->isNotEmpty())
            apvts.removeParameterListener (*id, this);

    cancelPendingUpdate();
}

void WaveDisplay::parameterChanged (const juce::String&, float)
{
    // Listener callbacks can arrive off the message thread.
    triggerAsyncUpdate();
}

void WaveDisplay::handleAsyncUpdate()
{
    repaint();
}

void WaveDisplay::timerCallback()
{
    repaint();
}

float WaveDisplay::normalised (const juce::String& paramID) const
{
    if (auto* p = apvts.getParameter (paramID))
        return p->getValue();

    return 0.0f;
}

void WaveDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    if (bounds.isEmpty())
        return;

    // Recessed panel, matching the faint framing Peak EQ uses for its grid.
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (bounds, kCornerRadius);
    g.setColour (theme.outline.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds, kCornerRadius, 1.0f);

    if (spec.livePhase)
    {
        paintLive (g, bounds);
        return;
    }

    const float amount01 = normalised (spec.amountID);
    const float rate01   = normalised (spec.rateID);
    const float shape01  = normalised (spec.shapeID);
    const bool  paired   = normalised (spec.modeID) > 0.5f;

    const float midY   = bounds.getCentreY();
    const float amp    = amount01 * (bounds.getHeight() * 0.5f - kVerticalPad);
    const float cycles = juce::jmap (rate01, 0.0f, 1.0f, kMinCycles, kMaxCycles);

    // Baseline.
    g.setColour (theme.title.withAlpha (0.25f));
    g.drawHorizontalLine (juce::roundToInt (midY), bounds.getX(), bounds.getRight());

    const auto buildTrace = [&] (float sign)
    {
        juce::Path p;
        const int steps = juce::jmax (2, juce::roundToInt (bounds.getWidth()));

        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (steps);
            const float x = bounds.getX() + t * bounds.getWidth();
            const float v = ee::dsp::lfoValue (t * cycles, shape01);
            const float y = midY - sign * v * amp;

            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }

        return p;
    };

    const auto stroke = juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);

    if (paired)
    {
        // The curve and its mirror - left and right motion of an auto-pan.
        g.setColour (theme.glow);
        g.strokePath (buildTrace (1.0f), stroke);
        g.setColour (theme.title.withAlpha (0.85f));
        g.strokePath (buildTrace (-1.0f), stroke);
    }
    else
    {
        g.setColour (theme.title);
        g.strokePath (buildTrace (1.0f), stroke);
    }
}

void WaveDisplay::paintLive (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // One full LFO cycle across the width, a playhead sweeping it at the running
    // phase (snapping left on every note retrigger), and a dot riding the trace
    // at that phase. Amplitude follows the live depth (Amount * gate), so the
    // wave collapses toward flat when the gate closes.
    float phase = spec.livePhase();
    if (! std::isfinite (phase))
        phase = 0.0f;
    phase -= std::floor (phase);

    float depth = spec.liveDepth ? spec.liveDepth() : normalised (spec.amountID);
    depth = juce::jlimit (0.0f, 1.0f, depth);

    const float shape01 = normalised (spec.shapeID);
    const bool  paired   = normalised (spec.modeID) > 0.5f;

    const float midY = bounds.getCentreY();
    const float amp  = depth * (bounds.getHeight() * 0.5f - kVerticalPad);

    g.setColour (theme.title.withAlpha (0.22f));
    g.drawHorizontalLine (juce::roundToInt (midY), bounds.getX(), bounds.getRight());

    const int steps = juce::jmax (2, juce::roundToInt (bounds.getWidth()));
    const auto stroke = juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);

    const auto trace = [&] (float sign)
    {
        juce::Path p;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (steps);
            const float x = bounds.getX() + t * bounds.getWidth();
            const float y = midY - sign * ee::dsp::lfoValue (t, shape01) * amp;
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        return p;
    };

    g.setColour (theme.glow.withAlpha (0.9f));
    g.strokePath (trace (1.0f), stroke);
    if (paired)
    {
        g.setColour (theme.title.withAlpha (0.6f));
        g.strokePath (trace (-1.0f), stroke);
    }

    // Playhead.
    const float px = bounds.getX() + phase * bounds.getWidth();
    g.setColour (theme.title.withAlpha (0.3f));
    g.drawVerticalLine (juce::roundToInt (px), bounds.getY() + 2.0f, bounds.getBottom() - 2.0f);

    const auto dot = [&] (float ph, float sign, juce::Colour c, float r)
    {
        const float y = midY - sign * ee::dsp::lfoValue (ph, shape01) * amp;
        g.setColour (c);
        g.fillEllipse (px - r, y - r, r * 2.0f, r * 2.0f);
    };

    dot (phase, 1.0f, theme.glow, 3.5f);
    if (paired)
        dot (phase + 0.5f, -1.0f, theme.title.withAlpha (0.75f), 3.0f);
}

} // namespace ee::ui
