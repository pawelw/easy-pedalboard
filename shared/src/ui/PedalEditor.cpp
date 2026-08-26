#include "ee/ui/PedalEditor.h"

namespace ee::ui
{
namespace
{
    constexpr int kMargin = 18;
    constexpr int kTopPad = 6;
    constexpr int kTitleHeight = 64;
    constexpr int kKnobRowHeight = 132;
    constexpr int kFootSwitchHeight = 92;
    constexpr int kFootSwitchBottomPad = 22;
    constexpr int kKnobGap = 10;
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
        knobs.push_back (std::make_unique<Knob> (state, knobSpec.parameterID, knobSpec.caption, theme));

    for (auto& knob : knobs)
        addAndMakeVisible (*knob);

    if (spec.bypassParameterID.isNotEmpty())
    {
        footSwitch = std::make_unique<FootSwitch> (state, spec.bypassParameterID, theme);
        addAndMakeVisible (*footSwitch);
    }

    setSize (spec.width, spec.height);

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

void PedalEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (theme.backgroundImage.isValid())
    {
        g.drawImage (theme.backgroundImage, bounds, juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.fillAll (theme.background);

        const auto face = bounds.reduced (6.0f);
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

        // Heavy enamel edge, with a lighter screened line set inside it.
        g.setColour (theme.outline);
        g.drawRoundedRectangle (face, theme.cornerRadius, 6.0f);
        g.setColour (theme.textPrimary.withAlpha (0.4f));
        g.drawRoundedRectangle (face.reduced (9.0f), theme.cornerRadius * 0.7f, 1.4f);
    }

    // Name sits under the knobs, the way it is screened onto a real pedal.
    // That also keeps the top of the face free instead of carrying a header.
    auto footer = getLocalBounds().reduced (kMargin, 0);
    footer = footer.withHeight (kTitleHeight)
                   .withY (getHeight() - kMargin - kFootSwitchBottomPad - kFootSwitchHeight - kTitleHeight);

    g.setColour (theme.title);
    g.setFont (theme.titleFont (58.0f));
    g.drawText (spec.name, footer, juce::Justification::centred, false);

    if (spec.version.isNotEmpty())
    {
        g.setColour (theme.textSecondary.withAlpha (0.7f));
        g.setFont (theme.bodyFont (10.5f));
        g.drawText (spec.version,
                    getLocalBounds().reduced (kMargin + 4, kMargin - 2),
                    juce::Justification::bottomLeft,
                    false);
    }
}

void PedalEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin, kMargin);
    area.removeFromTop (kTopPad);

    // Claimed before the knobs so the switch cannot creep up into the title.
    auto switchStrip = area.removeFromBottom (kFootSwitchBottomPad + kFootSwitchHeight);
    switchStrip.removeFromBottom (kFootSwitchBottomPad);
    area.removeFromBottom (kTitleHeight);

    const int count = static_cast<int> (knobs.size());
    const int perRow = juce::jlimit (1, juce::jmax (1, count), spec.knobsPerRow);

    for (int first = 0; first < count; first += perRow)
    {
        auto knobRow = area.removeFromTop (kKnobRowHeight);
        knobRow.removeFromTop (kKnobGap);

        const int inRow = juce::jmin (perRow, count - first);
        const int totalGap = kKnobGap * (inRow - 1);
        const int knobWidth = (knobRow.getWidth() - totalGap) / inRow;

        for (int i = 0; i < inRow; ++i)
        {
            knobs[static_cast<size_t> (first + i)]->setBounds (knobRow.removeFromLeft (knobWidth));
            if (i < inRow - 1)
                knobRow.removeFromLeft (kKnobGap);
        }
    }

    if (footSwitch != nullptr)
        footSwitch->setBounds (switchStrip);
}

} // namespace ee::ui
