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
}

WaveDisplay::~WaveDisplay()
{
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

    // Recessed panel, matching the faint framing Easy EQ uses for its grid.
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (bounds, kCornerRadius);
    g.setColour (theme.outline.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds, kCornerRadius, 1.0f);

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

} // namespace ee::ui
