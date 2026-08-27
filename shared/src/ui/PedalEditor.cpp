#include "ee/ui/PedalEditor.h"

#include "BinaryData.h"

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

    // Added after the knobs so they sit on top where the two bounds overlap.
    for (const auto& toggleSpec : spec.toggles)
    {
        toggles.push_back (std::make_unique<MiniToggle> (state, toggleSpec.parameterID,
                                                         toggleSpec.caption, theme));
        addAndMakeVisible (*toggles.back());
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
                                              left.getY() + (left.getHeight() - Knob::labelHeight) / 2);

        toggles[t]->setVisible (true);
        toggles[t]->setBounds (juce::Rectangle<int> (MiniToggle::preferredWidth,
                                                     MiniToggle::preferredHeight)
                                   .withCentre (centre));
    }
}

} // namespace ee::ui
