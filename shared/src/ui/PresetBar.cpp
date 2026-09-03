#include "ee/ui/PresetBar.h"

namespace ee::ui
{
namespace
{
    constexpr int kButtonW = 26;   // the square glyph buttons at either end
    constexpr int kEdgePad = 4;     // inside the bar's rounded frame
    constexpr int kGap = 2;         // between two buttons in the same cluster
    constexpr float kCorner = 6.0f; // the bar's own frame, and the button hover chip
} // namespace

//==============================================================================
/** One of the bar's four controls: a borderless button that paints a glyph and
    lights a faint chip behind it on hover. The glyph is a lambda so the four
    share one class. */
class PresetBar::GlyphButton : public juce::Button
{
public:
    GlyphButton (const PedalTheme& themeToUse,
                 std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> drawGlyph)
        : juce::Button ({}), theme (themeToUse), glyph (std::move (drawGlyph))
    {
        setWantsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        if (isEnabled() && (highlighted || down))
        {
            g.setColour (theme.knobBody.withAlpha (down ? 0.16f : 0.08f));
            g.fillRoundedRectangle (bounds, kCorner);
        }

        const auto ink = theme.textPrimary.withMultipliedAlpha (isEnabled() ? (highlighted ? 1.0f : 0.82f) : 0.3f);
        glyph (g, bounds.reduced (bounds.getWidth() * 0.24f, bounds.getHeight() * 0.24f), ink);
    }

private:
    const PedalTheme& theme;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> glyph;
};

//==============================================================================
namespace
{
    void drawListGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        const float th = juce::jmax (1.4f, r.getHeight() * 0.16f);
        g.setColour (c);
        for (int i = 0; i < 3; ++i)
        {
            const float y = r.getY() + r.getHeight() * (0.15f + 0.35f * static_cast<float> (i));
            g.fillRoundedRectangle (r.getX(), y - th * 0.5f, r.getWidth(), th, th * 0.5f);
        }
    }

    /** A floppy disk: a rounded square with the corner clipped, a label window
        in the lower half and the shutter block in the upper right. */
    void drawSaveGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        const float cut = r.getWidth() * 0.24f;
        juce::Path body;
        body.startNewSubPath (r.getX(), r.getY());
        body.lineTo (r.getRight() - cut, r.getY());
        body.lineTo (r.getRight(), r.getY() + cut);
        body.lineTo (r.getRight(), r.getBottom());
        body.lineTo (r.getX(), r.getBottom());
        body.closeSubPath();

        g.setColour (c);
        g.strokePath (body, juce::PathStrokeType (juce::jmax (1.2f, r.getHeight() * 0.11f)));

        // Label window, lower half.
        const auto label = juce::Rectangle<float> (r.getX() + r.getWidth() * 0.20f, r.getCentreY() + r.getHeight() * 0.06f,
                                                   r.getWidth() * 0.60f, r.getHeight() * 0.30f);
        g.fillRect (label);

        // Shutter, upper right.
        const auto shutter = juce::Rectangle<float> (r.getRight() - r.getWidth() * 0.42f, r.getY() + r.getHeight() * 0.12f,
                                                     r.getWidth() * 0.24f, r.getHeight() * 0.22f);
        g.fillRect (shutter);
    }

    void drawChevron (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool pointLeft)
    {
        const float th = juce::jmax (1.4f, r.getHeight() * 0.16f);
        const auto s = r.reduced (r.getWidth() * 0.22f, r.getHeight() * 0.08f);
        juce::Path p;
        if (pointLeft)
        {
            p.startNewSubPath (s.getRight(), s.getY());
            p.lineTo (s.getX(), s.getCentreY());
            p.lineTo (s.getRight(), s.getBottom());
        }
        else
        {
            p.startNewSubPath (s.getX(), s.getY());
            p.lineTo (s.getRight(), s.getCentreY());
            p.lineTo (s.getX(), s.getBottom());
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
} // namespace

//==============================================================================
PresetBar::PresetBar (PresetBarSpec specToUse, const PedalTheme& themeToUse)
    : spec (std::move (specToUse)), theme (themeToUse)
{
    listButton = std::make_unique<GlyphButton> (theme, [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
                                                { drawListGlyph (g, r, c); });
    saveButton = std::make_unique<GlyphButton> (theme, [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
                                                { drawSaveGlyph (g, r, c); });
    prevButton = std::make_unique<GlyphButton> (theme, [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
                                                { drawChevron (g, r, c, true); });
    nextButton = std::make_unique<GlyphButton> (theme, [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
                                                { drawChevron (g, r, c, false); });

    listButton->onClick = [this] { showListMenu(); };
    saveButton->onClick = [this] { showSaveMenu(); };
    prevButton->onClick = [this]
    {
        if (spec.onPrev)
            spec.onPrev();
        refresh();
    };
    nextButton->onClick = [this]
    {
        if (spec.onNext)
            spec.onNext();
        refresh();
    };

    listButton->setEnabled (spec.names != nullptr && spec.onSelect != nullptr);
    saveButton->setEnabled (spec.onSave != nullptr || spec.onSaveAsNew != nullptr);
    prevButton->setEnabled (spec.onPrev != nullptr);
    nextButton->setEnabled (spec.onNext != nullptr);

    for (auto* b : { listButton.get(), saveButton.get(), prevButton.get(), nextButton.get() })
        addAndMakeVisible (*b);

    refresh();
}

PresetBar::~PresetBar() = default;

void PresetBar::refresh()
{
    juce::String next;

    if (spec.names)
    {
        const auto all = spec.names();
        const int idx = spec.currentIndex ? spec.currentIndex() : -1;
        next = juce::isPositiveAndBelow (idx, all.size()) ? all[idx] : juce::String();
    }

    if (next.isEmpty())
        next = "\xE2\x80\x94"; // em dash - no stored preset selected

    if (next != name)
    {
        name = next;
        repaint();
    }
}

void PresetBar::showListMenu()
{
    if (! spec.names || ! spec.onSelect)
        return;

    const auto all = spec.names();
    const int current = spec.currentIndex ? spec.currentIndex() : -1;

    juce::PopupMenu menu;
    if (all.isEmpty())
    {
        menu.addItem (1, "No presets", false, false);
    }
    else
    {
        for (int i = 0; i < all.size(); ++i)
            menu.addItem (i + 1, all[i], true, i == current);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (listButton.get()),
                        [this] (int result)
                        {
                            if (result > 0 && spec.onSelect)
                                spec.onSelect (result - 1);
                            refresh();
                        });
}

void PresetBar::showSaveMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Save", spec.onSave != nullptr);
    menu.addItem (2, "Save as New\xE2\x80\xA6", spec.onSaveAsNew != nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (saveButton.get()),
                        [this] (int result)
                        {
                            if (result == 1 && spec.onSave)
                                spec.onSave();
                            else if (result == 2 && spec.onSaveAsNew)
                                spec.onSaveAsNew();
                            refresh();
                        });
}

void PresetBar::resized()
{
    auto area = getLocalBounds().reduced (kEdgePad);
    const int h = area.getHeight();
    const int bw = juce::jmin (kButtonW, h + 4);

    listButton->setBounds (area.removeFromLeft (bw));
    area.removeFromLeft (kGap);
    saveButton->setBounds (area.removeFromLeft (bw));

    nextButton->setBounds (area.removeFromRight (bw));
    area.removeFromRight (kGap);
    prevButton->setBounds (area.removeFromRight (bw));
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    // A recessed panel, like the digital readouts.
    g.setColour (theme.recess.isTransparent() ? theme.panel.darker (0.25f) : theme.recess);
    g.fillRoundedRectangle (bounds, kCorner);
    g.setColour (theme.outline.withAlpha (0.7f));
    g.drawRoundedRectangle (bounds, kCorner, 1.0f);

    // The name, in the gap the buttons leave in the middle.
    auto textArea = getLocalBounds().reduced (kEdgePad, 0);
    textArea.removeFromLeft (2 * kButtonW + kGap + kEdgePad);
    textArea.removeFromRight (2 * kButtonW + kGap + kEdgePad);

    g.setColour (theme.textPrimary);
    g.setFont (theme.bodyFont (12.5f));
    g.drawText (name, textArea, juce::Justification::centred, true);
}

} // namespace ee::ui
