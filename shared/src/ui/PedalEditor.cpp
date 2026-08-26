#include "ee/ui/PedalEditor.h"

namespace ee::ui
{
namespace
{
    constexpr int kMargin = 18;
    constexpr int kHeaderHeight = 74;
    constexpr int kKnobRowHeight = 132;
    constexpr int kFootSwitchHeight = 118;
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
        g.setColour (theme.outline);
        g.drawRoundedRectangle (face, theme.cornerRadius, 1.4f);
    }

    auto header = getLocalBounds().reduced (kMargin, 0).withHeight (kHeaderHeight).withY (kMargin);

    g.setColour (theme.textPrimary);
    g.setFont (theme.titleFont (28.0f).withExtraKerningFactor (0.06f));
    g.drawText (spec.name.toUpperCase(), header.removeFromTop (34), juce::Justification::centredTop, false);

    if (spec.tagline.isNotEmpty())
    {
        g.setColour (theme.textSecondary);
        g.setFont (theme.bodyFont (11.0f));
        g.drawText (spec.tagline, header.removeFromTop (18), juce::Justification::centredTop, false);
    }

    const float lineY = static_cast<float> (kMargin + kHeaderHeight);
    g.setColour (theme.outline);
    g.drawLine (static_cast<float> (kMargin), lineY, static_cast<float> (getWidth() - kMargin), lineY, 1.0f);
}

void PedalEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin, kMargin);
    area.removeFromTop (kHeaderHeight);

    auto knobRow = area.removeFromTop (kKnobRowHeight);
    knobRow.removeFromTop (kKnobGap);

    if (! knobs.empty())
    {
        const int count = static_cast<int> (knobs.size());
        const int totalGap = kKnobGap * (count - 1);
        const int knobWidth = (knobRow.getWidth() - totalGap) / count;

        for (int i = 0; i < count; ++i)
        {
            knobs[static_cast<size_t> (i)]->setBounds (knobRow.removeFromLeft (knobWidth));
            if (i < count - 1)
                knobRow.removeFromLeft (kKnobGap);
        }
    }

    if (footSwitch != nullptr)
    {
        const int size = juce::jmin (area.getWidth(), kFootSwitchHeight);
        footSwitch->setBounds (area.withSizeKeepingCentre (size, juce::jmin (area.getHeight(), kFootSwitchHeight)));
    }
}

} // namespace ee::ui
