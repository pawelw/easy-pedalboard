#include "ee/ui/PedalEditor.h"

#include "BinaryData.h"

namespace ee::ui
{
namespace
{
    constexpr float kFaceInset = 6.0f;       // dark frame around the painted face
    constexpr float kBorderInset = 9.0f;     // white line, measured in from the face
    constexpr float kBorderThickness = 5.0f;

    // Everything on the face is spaced from the inside of the white border.
    constexpr int kContentPad = 16;
    constexpr int kMargin = static_cast<int> (kFaceInset + kBorderInset + kBorderThickness * 0.5f)
                                + kContentPad;

    constexpr int kKnobGap = 12;
    constexpr float kKnobScale = 0.8f;   // rotary size relative to its column
    constexpr int kTitleHeight = 64;
    constexpr int kLogoHeight = 54;

    juce::Image brandLogo()
    {
        static const juce::Image logo = juce::ImageCache::getFromMemory (BinaryData::rocketlogo_png,
                                                                        BinaryData::rocketlogo_pngSize);
        return logo;
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
        knobs.push_back (std::make_unique<Knob> (state, knobSpec.parameterID, knobSpec.caption, theme));

    for (auto& knob : knobs)
        addAndMakeVisible (*knob);

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

juce::Rectangle<int> PedalEditor::logoArea() const
{
    return getLocalBounds().reduced (kMargin).removeFromBottom (kLogoHeight);
}

juce::Rectangle<int> PedalEditor::titleArea() const
{
    auto area = getLocalBounds().reduced (kMargin);
    area.removeFromBottom (kLogoHeight);
    return area.removeFromBottom (kTitleHeight);
}

juce::Rectangle<int> PedalEditor::knobArea() const
{
    auto area = getLocalBounds().reduced (kMargin);
    area.removeFromBottom (kLogoHeight + kTitleHeight);
    return area;
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

        // Heavy enamel edge, with a bold screened line set inside it.
        g.setColour (theme.outline);
        g.drawRoundedRectangle (face, theme.cornerRadius, kFaceInset);
        g.setColour (theme.textPrimary.withAlpha (0.92f));
        g.drawRoundedRectangle (face.reduced (kBorderInset), theme.cornerRadius * 0.7f, kBorderThickness);
    }

    // Name sits under the knobs, the way it is screened onto a real pedal.
    // That also keeps the top of the face free instead of carrying a header.
    g.setColour (theme.title);
    g.setFont (theme.titleFont (58.0f));
    g.drawText (spec.name, titleArea(), juce::Justification::centred, false);

    if (const auto logo = brandLogo(); logo.isValid())
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
                    getLocalBounds().reduced (kMargin),
                    juce::Justification::bottomLeft,
                    false);
    }
}

void PedalEditor::resized()
{
    auto area = knobArea();

    const int count = static_cast<int> (knobs.size());
    const int perRow = juce::jlimit (1, juce::jmax (1, count), spec.knobsPerRow);

    // Columns span the full content width; the rotary sits centred in its
    // column at a fraction of it.
    const int cellWidth = (area.getWidth() - kKnobGap * (perRow - 1)) / perRow;
    const int knobWidth = juce::roundToInt (static_cast<float> (cellWidth) * kKnobScale);
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
}

} // namespace ee::ui
