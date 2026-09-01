#include "ee/ui/DigitalSwitch.h"

namespace ee::ui
{
namespace
{
    /** Everything the two sizes disagree about. */
    struct Metrics
    {
        float trackHeight;
        float knobInset;
        float trackSlack;   // track width beyond the two knob-circles it holds
        float labelGap;
        float fontHeight;
    };

    constexpr Metrics kFull    { 26.0f, 3.0f, 10.0f, 10.0f, 11.5f };
    constexpr Metrics kCompact { 19.0f, 2.5f,  7.0f,  7.0f,  9.5f };

    const Metrics& metricsFor (DigitalSwitch::Size size)
    {
        return size == DigitalSwitch::Size::full ? kFull : kCompact;
    }
}

DigitalSwitch::DigitalSwitch (juce::AudioProcessorValueTreeState& state,
                              const SlideToggleSpec& spec,
                              const PedalTheme& theme,
                              Size sizeToUse)
    : juce::Button (spec.parameterID),
      pedalTheme (theme),
      // With `invertPosition` the "on" label is the one on the left, so the
      // knob's resting end and the label under it stay in step.
      labelLeft (spec.invertPosition ? spec.labelOn : spec.labelOff),
      labelRight (spec.invertPosition ? spec.labelOff : spec.labelOn),
      accent (spec.accent.value_or (theme.switchHighlight)),
      labelColour (spec.labelColour.value_or (theme.textPrimary)),
      size (sizeToUse),
      flushLeft (spec.labelFlushLeft),
      inverted (spec.invertPosition)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, spec.parameterID, *this);
}

DigitalSwitch::~DigitalSwitch() = default;

juce::Font DigitalSwitch::labelFont() const
{
    return pedalTheme.bodyFont (metricsFor (size).fontHeight)
        .boldened()
        .withExtraKerningFactor (0.10f);
}

float DigitalSwitch::trackWidth() const
{
    const auto& m = metricsFor (size);
    return 2.0f * (m.trackHeight - 2.0f * m.knobInset) + m.trackSlack;
}

float DigitalSwitch::labelWidth (const juce::String& text) const
{
    if (text.isEmpty())
        return 0.0f;

    return juce::GlyphArrangement::getStringWidth (labelFont(), text.toUpperCase());
}

bool DigitalSwitch::knobIsRight() const
{
    return inverted ? ! getToggleState() : getToggleState();
}

int DigitalSwitch::switchWidth() const
{
    const auto& m = metricsFor (size);

    float w = trackWidth();
    if (labelLeft.isNotEmpty())
        w += labelWidth (labelLeft) + m.labelGap;
    if (labelRight.isNotEmpty())
        w += labelWidth (labelRight) + m.labelGap;

    // A hair of slack either side: the bold faces here overhang their advance
    // widths just enough to clip the last letter without it.
    return juce::roundToInt (w) + 4;
}

int DigitalSwitch::switchHeight() const
{
    return juce::roundToInt (metricsFor (size).trackHeight) + 4;
}

int DigitalSwitch::switchTrackOffset() const
{
    const auto& m = metricsFor (size);

    // Same walk left to right that `paintButton` makes, stopping at the middle
    // of the track.
    float centre = 2.0f;
    if (labelLeft.isNotEmpty())
        centre += labelWidth (labelLeft) + m.labelGap;
    centre += trackWidth() * 0.5f;

    return juce::roundToInt (centre - static_cast<float> (switchWidth()) * 0.5f);
}

int DigitalSwitch::switchLabelInset() const
{
    // The labels are measured, not boxed, so the first letter already starts on
    // the component edge - bar the 2 px of slack `switchWidth` adds.
    return flushLeft ? 2 : 0;
}

void DigitalSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& m = metricsFor (size);
    const auto bounds = getLocalBounds().toFloat();
    const bool right = knobIsRight();

    const float trackH = juce::jmin (m.trackHeight, bounds.getHeight());
    const float trackW = trackWidth();

    // Laid out left to right from the component edge, so a caller that pinned
    // the switch to a margin gets its first letter on that margin.
    float x = bounds.getX() + 2.0f;

    g.setFont (labelFont());

    if (labelLeft.isNotEmpty())
    {
        const float w = labelWidth (labelLeft);
        g.setColour (right ? labelColour.withAlpha (0.38f) : labelColour);
        g.drawText (labelLeft.toUpperCase(),
                    juce::Rectangle<float> (x, bounds.getY(), w, bounds.getHeight()),
                    juce::Justification::centredLeft, false);
        x += w + m.labelGap;
    }

    const auto track = juce::Rectangle<float> (x, bounds.getCentreY() - trackH * 0.5f, trackW, trackH);
    x += trackW + m.labelGap;

    if (labelRight.isNotEmpty())
    {
        g.setColour (right ? labelColour : labelColour.withAlpha (0.38f));
        g.drawText (labelRight.toUpperCase(),
                    juce::Rectangle<float> (x, bounds.getY(), labelWidth (labelRight), bounds.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    //== the track ==========================================================
    // Knob right = set: the track fills solid. Knob left = clear: it stays a
    // pale groove, so the two states read from across the room.
    const float radius = trackH * 0.5f;

    g.setColour (right ? pedalTheme.knobBody : pedalTheme.switchBody);
    g.fillRoundedRectangle (track, radius);

    if (! right)
    {
        // A shadow cast in from the top edge, so an empty track reads as a
        // groove rather than a flat grey pill.
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path rounded;
        rounded.addRoundedRectangle (track, radius);
        g.reduceClipRegion (rounded);

        g.setColour (pedalTheme.softShadow.withMultipliedAlpha (0.7f));
        g.drawRoundedRectangle (track.translated (0.0f, 1.2f), radius, 1.6f);
    }

    if (highlighted || down)
    {
        g.setColour ((right ? pedalTheme.softHighlight : pedalTheme.knobBody)
                         .withAlpha (down ? 0.14f : 0.07f));
        g.fillRoundedRectangle (track, radius);
    }

    //== the knob ===========================================================
    const float kd = trackH - 2.0f * m.knobInset;
    const float kx = right ? track.getRight() - m.knobInset - kd
                           : track.getX() + m.knobInset;
    const auto knob = juce::Rectangle<float> (kx, track.getY() + m.knobInset, kd, kd);

    juce::Path knobPath;
    knobPath.addEllipse (knob);
    juce::DropShadow (pedalTheme.softShadow.withMultipliedAlpha (1.6f),
                      juce::roundToInt (juce::jmax (2.0f, kd * 0.45f)), { 0, 1 })
        .drawForPath (g, knobPath);

    g.setColour (accent);
    g.fillEllipse (knob);

    // Against a pale track the knob is white on near-white; a hairline is what
    // keeps its edge. On the dark track the fill already carries it.
    if (! right)
    {
        g.setColour (pedalTheme.knobBody.withAlpha (0.14f));
        g.drawEllipse (knob.reduced (0.5f), 1.0f);
    }
}

} // namespace ee::ui
