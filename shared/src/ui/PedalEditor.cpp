#include "ee/ui/PedalEditor.h"

#include "ee/ui/FilterScope.h"
#include "ee/ui/SlideToggle.h"
#include "ee/ui/WaveDisplay.h"

#include "BinaryData.h"

#include <algorithm>
#include <cmath>

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
    static_assert (kMargin == kFaceContentMargin, "shared face margin out of sync");

    constexpr int kKnobGap = kKnobColumnGap;

    // Faders sit in one row below the knobs (or fill the whole control area on
    // a pedal that has no knobs). Fixed cap width so a fader is the same size
    // however many share the row.
    constexpr int kFaderWidth = 48;   // strip width; the cap drawn inside is narrower
    constexpr int kFaderRowGap = 12;

    // Drop below the strip of corner knobs / group trims before the fader grid,
    // so the grid does not sit tight against the caps.
    constexpr int kFaderGridGap = 16;

    // The fader RESET pill, which sits below the grid on the pedal-name row.
    constexpr int kResetButtonWidth = 54;
    constexpr int kResetButtonHeight = 20;

    // Small knobs pinned top-right. The component is wider than the cap so the
    // "4.5 kHz" style readout fits underneath.
    constexpr int kCornerKnobDiameter = 48;
    constexpr int kCornerKnobWidth = 60;
    constexpr int kCornerKnobGap = 6;

    // Sits above the middle of the rotaries, where the caps have curved away
    // and there is more room either side of it.
    constexpr int kToggleRise = 26;

    // Sliding switch in a strip carved off the top of the knob area, with a
    // little breathing room below before the caps.
    constexpr int kSwitchStripHeight = 30;
    constexpr int kSwitchStripGap = 10;

    // A slide switch parked at the very bottom instead of the top (Peak Wah's
    // Mono/Stereo), left-aligned in its own thin strip.
    constexpr int kBottomStripHeight = 26;

    // Gap between the LFO preview band and the pedal name below it.
    constexpr int kWaveDisplayGap = 10;

    // Between the emblem and the pedal name when they share the bottom row.
    constexpr int kLogoNameGap = 14;

    // Secondary row of small knobs below the main knob block, each optionally
    // carrying a latching button beneath its label.
    constexpr int kSubKnobDiameter = 72;
    constexpr int kSubKnobGap = 4;      // sub knobs cluster tighter than the main row
    constexpr int kSubRowGap = 10;      // between the main rows and the sub row
    constexpr int kSubButtonGap = 3;    // between a sub knob's label and its button

    // Fixed rather than a fraction of the column, so a knob is the same size on
    // every pedal however many of them a row carries.
    constexpr int kKnobDiameter = 114;

    constexpr int kTitleHeight = 64;
    constexpr int kLogoHeight = 54;

    juce::Image brandLogo()
    {
        static const juce::Image logo = juce::ImageCache::getFromMemory (BinaryData::peaklogo_png,
                                                                        BinaryData::peaklogo_pngSize);
        return logo;
    }

    /** Keeps the artwork's shape and replaces its colour. */
    juce::Image tinted (const juce::Image& source, juce::Colour colour)
    {
        juce::Image out (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);

        const juce::Image::BitmapData src (source, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < source.getHeight(); ++y)
            for (int x = 0; x < source.getWidth(); ++x)
                dst.setPixelColour (x, y, colour.withAlpha (src.getPixelColour (x, y).getFloatAlpha()));

        return out;
    }

    // Group-trim range, in the faders' own units (dB on a graphic EQ). Full
    // travel from rest to either end applies +/- this to every band it drives.
    constexpr double kGroupTrimSpan = 15.0;
}

//==============================================================================
/** Small rotary that nudges a set of faders together. Holds no parameter: each
    move applies the change in its own value as a delta to the faders it drives,
    so it reads as a relative group trim. Styled to match the compact corner
    knobs. */
class GroupTrim : public juce::Component
{
public:
    GroupTrim (juce::String captionText, const PedalTheme& themeToUse)
        : caption (std::move (captionText)), theme (themeToUse)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f, true);
        slider.setRange (-kGroupTrimSpan, kGroupTrimSpan, 0.1);
        slider.setValue (0.0, juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, 0.0);

        // Styled as a compact utility knob, so it keeps the vector cap rather
        // than the full-size photographic artwork.
        slider.getProperties().set ("compactKnob", true);

        addAndMakeVisible (slider);

        slider.onValueChange = [this]
        {
            const double v = slider.getValue();
            const double delta = v - lastValue;
            lastValue = v;
            if (onDelta && std::abs (delta) > 1.0e-9)
                onDelta (delta);
        };
    }

    /** Back to centre without driving the faders (used by the RESET button,
        which flattens the faders itself). */
    void resetToCentre()
    {
        lastValue = 0.0;
        slider.setValue (0.0, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromBottom (Knob::compactLabelHeight);
        slider.setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto textArea = area.removeFromBottom (static_cast<float> (Knob::compactLabelHeight));

        g.setColour (theme.textPrimary);
        g.setFont (theme.bodyFont (12.0f));
        g.drawText (caption.toUpperCase(), textArea, juce::Justification::centred, false);
    }

    /** Called with the signed change in the knob's value on every move. */
    std::function<void (double)> onDelta;

private:
    juce::Slider slider;
    juce::String caption;
    const PedalTheme& theme;
    double lastValue = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroupTrim)
};

//==============================================================================
/** The pedal face: everything the old editor drew and laid out, at design size.
    PedalEditor owns one of these and scales it to fill the window. */
class PedalEditor::Face : public juce::Component
{
public:
    Face (juce::AudioProcessorValueTreeState& state,
          PedalSpec specToUse,
          const PedalTheme& themeToUse,
          PedalLookAndFeel& lnf);

    ~Face() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setSidePanel (std::unique_ptr<juce::Component> panel, int panelWidth);

    int getLogicalWidth() const  { return spec.width + sidePanelWidth; }
    int getLogicalHeight() const { return spec.height; }

private:
    juce::Rectangle<int> faceBounds() const;
    juce::Rectangle<int> contentArea() const;
    bool topSwitch() const;
    bool bottomSwitch() const;
    bool hasBottomBand() const;
    int bottomBandHeight() const;
    juce::Rectangle<int> switchStripArea() const;
    juce::Rectangle<int> waveDisplayArea() const;
    juce::Rectangle<int> subKnobArea() const;
    int subLabelHeight() const;
    int subRowHeight() const;
    juce::Rectangle<int> knobArea() const;
    juce::Rectangle<int> titleArea() const;
    juce::Rectangle<int> logoArea() const;

    void layOutFaders (juce::Rectangle<int> area);
    juce::Rectangle<int> faderArea() const;

    void paintFaderGraph (juce::Graphics&) const;
    void paintCutMasks (juce::Graphics&, juce::Rectangle<float> grid) const;
    void resetFaders();

    PedalTheme theme;
    PedalSpec spec;

    std::vector<std::unique_ptr<Knob>> knobs;   // a null entry is a spacer column

    /** Bounds of every knob-grid slot, spacers included. */
    std::vector<juce::Rectangle<int>> knobCells;
    std::unique_ptr<Knob> centreKnob;
    std::vector<std::unique_ptr<Knob>> cornerKnobs;
    std::vector<std::unique_ptr<Knob>> subKnobs;
    std::vector<std::unique_ptr<MiniToggle>> subButtons;   // index-aligned with subKnobs; null where none
    std::vector<std::unique_ptr<GroupTrim>> groupTrims;
    std::vector<std::unique_ptr<FaderStrip>> faders;
    std::vector<std::unique_ptr<MiniToggle>> toggles;
    std::unique_ptr<SlideToggle> slideToggle;
    std::unique_ptr<WaveDisplay> waveDisplay;
    std::unique_ptr<FilterScope> filterScope;
    std::unique_ptr<juce::TextButton> faderResetButton;

    /** Vertical rule between the group-trim cluster and the corner cut knobs.
        Empty when either cluster is absent. */
    juce::Rectangle<int> groupTrimDivider;

    /** Vertical rule splitting the knob grid into two clusters. Empty unless
        the spec asks for one. */
    juce::Rectangle<int> knobDivider;

    std::unique_ptr<juce::Component> sidePanel;
    int sidePanelWidth = 0;
    juce::Image grain;
    juce::Image logoImage;
    juce::Image emblemImage;   // spec.titleImage, tinted if the spec asks

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
};

//==============================================================================
PedalEditor::Face::Face (juce::AudioProcessorValueTreeState& state,
                         PedalSpec specToUse,
                         const PedalTheme& themeToUse,
                         PedalLookAndFeel& lnf)
    : theme (themeToUse),
      spec (std::move (specToUse))
{
    setLookAndFeel (&lnf);

    for (const auto& knobSpec : spec.knobs)
        knobs.push_back (knobSpec.parameterID.isEmpty()
                             ? nullptr   // a spacer: holds its column, draws nothing
                             : std::make_unique<Knob> (state, knobSpec, theme));

    for (auto& knob : knobs)
        if (knob != nullptr)
            addAndMakeVisible (*knob);

    // Added after the row knobs so it draws on top where its small cap overlaps
    // their bounds in the middle of the grid.
    if (spec.centreKnob.has_value())
    {
        centreKnob = std::make_unique<Knob> (state, *spec.centreKnob, theme);
        addAndMakeVisible (*centreKnob);
    }

    for (const auto& sliderSpec : spec.sliders)
        faders.push_back (std::make_unique<FaderStrip> (state, sliderSpec, theme));

    for (auto& fader : faders)
        addAndMakeVisible (*fader);

    for (const auto& knobSpec : spec.cornerKnobs)
    {
        auto knob = std::make_unique<Knob> (state, knobSpec, theme);
        knob->onValueChanged = [this] { repaint(); };   // the cut masks track it
        addAndMakeVisible (*knob);
        cornerKnobs.push_back (std::move (knob));
    }

    // Group-trim knobs: each pushes its listed faders by the same delta.
    for (const auto& trimSpec : spec.groupTrims)
    {
        auto trim = std::make_unique<GroupTrim> (trimSpec.caption, theme);
        const std::vector<int> indices = trimSpec.sliderIndices;

        trim->onDelta = [this, indices] (double delta)
        {
            for (const int i : indices)
            {
                if (i < 0 || i >= static_cast<int> (faders.size()))
                    continue;

                auto& s = faders[static_cast<size_t> (i)]->getSlider();
                s.setValue (juce::jlimit (s.getMinimum(), s.getMaximum(), s.getValue() + delta),
                            juce::sendNotificationSync);
            }
            repaint();   // the response curve follows the faders
        };

        addAndMakeVisible (*trim);
        groupTrims.push_back (std::move (trim));
    }

    // A pedal driven by faders gets a reset that flattens them all. It lives in
    // the strip above the grid.
    if (! faders.empty())
    {
        faderResetButton = std::make_unique<juce::TextButton> ("RESET");
        faderResetButton->setWantsKeyboardFocus (false);
        // Small dark pill with a light label.
        faderResetButton->setColour (juce::TextButton::buttonColourId, theme.knobBody);
        faderResetButton->setColour (juce::TextButton::textColourOffId, juce::Colour (0xfff2f4f6));
        faderResetButton->onClick = [this] { resetFaders(); };
        addAndMakeVisible (*faderResetButton);
    }

    // Added after the knobs so they sit on top where the two bounds overlap.
    for (const auto& toggleSpec : spec.toggles)
    {
        toggles.push_back (std::make_unique<MiniToggle> (state, toggleSpec, theme));

        // A toggle (e.g. tempo sync) can change the unit a knob reads in.
        toggles.back()->onStateChange = [this]
        {
            for (auto& knob : knobs)
                if (knob != nullptr)
                    knob->refreshValueText();
            if (waveDisplay != nullptr)
                waveDisplay->repaint();
        };

        if (toggleSpec.onClick)
            toggles.back()->onClick = toggleSpec.onClick;

        addAndMakeVisible (*toggles.back());
    }

    // Secondary knob row, each cell optionally carrying a button beneath it.
    for (const auto& sub : spec.subKnobs)
    {
        KnobSpec knobSpec;
        knobSpec.parameterID = sub.parameterID;
        knobSpec.caption = sub.caption;
        knobSpec.liveValueText = sub.liveValueText;
        knobSpec.valueIcon = sub.valueIcon;
        knobSpec.captionUntilTouched = sub.captionUntilTouched;
        subKnobs.push_back (std::make_unique<Knob> (state, knobSpec, theme));
        addAndMakeVisible (*subKnobs.back());

        std::unique_ptr<MiniToggle> button;
        if (sub.buttonParameterID.isNotEmpty())
        {
            ToggleSpec tSpec;
            tSpec.parameterID = sub.buttonParameterID;
            tSpec.caption = sub.buttonCaption;
            tSpec.litColour = sub.buttonLitColour;

            button = std::make_unique<MiniToggle> (state, tSpec, theme);
            button->onStateChange = [this]
            {
                // Sync can change what the Time knob's readout means.
                for (auto& k : subKnobs)
                    k->refreshValueText();
                for (auto& k : knobs)
                    if (k != nullptr)
                        k->refreshValueText();
            };
            if (sub.buttonOnClick)
                button->onClick = sub.buttonOnClick;
            addAndMakeVisible (*button);
        }
        subButtons.push_back (std::move (button));
    }

    if (spec.slideToggle.has_value())
    {
        slideToggle = std::make_unique<SlideToggle> (state, *spec.slideToggle, theme);
        slideToggle->onStateChange = [this]
        {
            // The switch can change what a knob's readout means, and what the
            // preview draws.
            for (auto& knob : knobs)
                if (knob != nullptr)
                    knob->refreshValueText();
            if (waveDisplay != nullptr)
                waveDisplay->repaint();
        };
        addAndMakeVisible (*slideToggle);
    }

    if (spec.waveDisplay.has_value())
    {
        waveDisplay = std::make_unique<WaveDisplay> (state, *spec.waveDisplay, theme);
        addAndMakeVisible (*waveDisplay);
    }

    if (spec.filterScope.has_value())
    {
        filterScope = std::make_unique<FilterScope> (*spec.filterScope, theme);
        addAndMakeVisible (*filterScope);
    }

    logoImage = brandLogo();

    if (theme.logoTint.has_value() && logoImage.isValid())
        logoImage = tinted (logoImage, *theme.logoTint);

    // Recoloured once here rather than on every repaint - tinting walks every
    // pixel of the source artwork.
    emblemImage = spec.titleImage;

    if (spec.titleImageTint.has_value() && emblemImage.isValid())
        emblemImage = tinted (emblemImage, *spec.titleImageTint);

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

PedalEditor::Face::~Face()
{
    setLookAndFeel (nullptr);
}

void PedalEditor::Face::setSidePanel (std::unique_ptr<juce::Component> panel, int panelWidth)
{
    sidePanel = std::move (panel);
    sidePanelWidth = (sidePanel != nullptr) ? panelWidth : 0;

    if (sidePanel != nullptr)
        addAndMakeVisible (*sidePanel);
}

juce::Rectangle<int> PedalEditor::Face::faceBounds() const
{
    return getLocalBounds().withWidth (spec.width);
}

juce::Rectangle<int> PedalEditor::Face::logoArea() const
{
    return faceBounds().reduced (kMargin).removeFromBottom (kLogoHeight);
}

juce::Rectangle<int> PedalEditor::Face::titleArea() const
{
    auto area = faceBounds().reduced (kMargin);
    area.removeFromBottom (kLogoHeight);

    // With the name on the logo row there is no row of its own: what is left is
    // the hairline where it used to start, so anything anchored to the name row
    // still lands somewhere sensible.
    return area.removeFromBottom (spec.titleBesideLogo ? 0 : kTitleHeight);
}

juce::Rectangle<int> PedalEditor::Face::contentArea() const
{
    auto area = faceBounds().reduced (kMargin);
    area.removeFromBottom (kLogoHeight + (spec.titleBesideLogo ? 0 : kTitleHeight));
    return area;
}

bool PedalEditor::Face::topSwitch() const
{
    return slideToggle != nullptr && ! spec.slideToggleBottom;
}

bool PedalEditor::Face::bottomSwitch() const
{
    // "Bottom" now means left-aligned on the pedal-name row - it carves off no
    // strip of its own.
    return slideToggle != nullptr && spec.slideToggleBottom;
}

bool PedalEditor::Face::hasBottomBand() const
{
    return waveDisplay != nullptr || filterScope != nullptr;
}

int PedalEditor::Face::bottomBandHeight() const
{
    if (waveDisplay != nullptr)
        return spec.waveDisplay->height + kWaveDisplayGap;
    if (filterScope != nullptr)
        return spec.filterScope->height + kWaveDisplayGap;
    return 0;
}

juce::Rectangle<int> PedalEditor::Face::switchStripArea() const
{
    if (topSwitch())
        return contentArea().removeFromTop (kSwitchStripHeight);

    return {};
}

juce::Rectangle<int> PedalEditor::Face::waveDisplayArea() const
{
    if (! hasBottomBand())
        return {};

    auto band = contentArea().removeFromBottom (bottomBandHeight());
    band.translate (0, -spec.displayBandRise);
    return band.withTrimmedBottom (2);   // sit low, near the pedal name
}

int PedalEditor::Face::subRowHeight() const
{
    if (subKnobs.empty())
        return 0;

    int h = kSubKnobDiameter + subLabelHeight();
    for (const auto& b : subButtons)
        if (b != nullptr)
        {
            h += kSubButtonGap + MiniToggle::preferredHeight;
            break;
        }
    return h;
}

int PedalEditor::Face::subLabelHeight() const
{
    // A row of one-line sub knobs is shorter than a row of two-line ones; the
    // tallest in the row sets the height they all lay out to.
    int h = 0;
    for (const auto& k : subKnobs)
        h = juce::jmax (h, k->getLabelHeight());
    return h;
}

juce::Rectangle<int> PedalEditor::Face::subKnobArea() const
{
    if (subKnobs.empty())
        return {};

    auto area = contentArea();
    if (topSwitch())
        area.removeFromTop (kSwitchStripHeight + kSwitchStripGap);
    if (hasBottomBand())
        area.removeFromBottom (bottomBandHeight() + spec.displayBandRise + kSubRowGap);

    return area.removeFromBottom (subRowHeight());
}

juce::Rectangle<int> PedalEditor::Face::knobArea() const
{
    auto area = contentArea();

    if (topSwitch())
        area.removeFromTop (kSwitchStripHeight + kSwitchStripGap);

    if (hasBottomBand())
        area.removeFromBottom (bottomBandHeight() + spec.displayBandRise
                                   + (subKnobs.empty() ? 0 : kSubRowGap));

    if (! subKnobs.empty())
        area.removeFromBottom (subRowHeight() + kSubRowGap);

    return area;
}

juce::Rectangle<int> PedalEditor::Face::faderArea() const
{
    if (faders.empty())
        return {};

    auto r = faders.front()->getBounds();
    for (const auto& fader : faders)
        r = r.getUnion (fader->getBounds());

    return r.withTrimmedBottom (FaderStrip::labelHeight);
}

void PedalEditor::Face::paintFaderGraph (juce::Graphics& g) const
{
    if (faders.empty())
        return;

    const auto area = faderArea().toFloat();
    if (area.isEmpty())
        return;

    // Rules line up with the node travel, so the top and bottom rule meet the
    // extremes of the faders.
    const auto travel = faderTrackRange (area);

    constexpr float kGridCorner = 6.0f;

    const juce::Rectangle<float> gridBounds (area.getX(), travel.getStart(),
                                             area.getWidth(), travel.getLength());

    juce::Path gridClip;
    gridClip.addRoundedRectangle (gridBounds, kGridCorner);

    // A hair of a frame around the ruled area, drawn before the clip so its
    // full stroke shows.
    g.setColour (theme.outline.withAlpha (0.3f));
    g.drawRoundedRectangle (gridBounds, kGridCorner, 1.0f);

    // Everything inside the grid is clipped to the rounded frame so no rule,
    // shading or curve tail runs past a corner.
    juce::Graphics::ScopedSaveState gridState (g);
    g.reduceClipRegion (gridClip);

    // Shading for the trimmed ends of the spectrum.
    paintCutMasks (g, gridBounds);

    // Horizontal rules, one per 5 dB step across a +/-15 dB face, the middle
    // one (0 dB) a touch stronger. The top and bottom steps are left to the
    // frame.
    constexpr int kHLines = 7;
    for (int i = 1; i < kHLines - 1; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kHLines - 1);
        const float ly = travel.getStart() + t * travel.getLength();
        const bool centre = (i == kHLines / 2);

        g.setColour (theme.outline.withAlpha (centre ? 0.5f : 0.22f));
        g.fillRect (juce::Rectangle<float> (area.getX(), ly - 0.5f, area.getWidth(), 1.0f));
    }

    // Vertical rule under every fader; gather the band faders (a level fader is
    // skipped) with the frequency and gain each one carries.
    struct Band { float x, hz, db; };
    std::vector<Band> bands;
    std::vector<juce::Point<float>> nodes;   // fallback when no frequencies are given
    bool haveFrequencies = true;

    for (size_t i = 0; i < faders.size(); ++i)
    {
        const auto node = faders[i]->nodeCentreInParent();

        g.setColour (theme.outline.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (node.x - 0.5f, travel.getStart(), 1.0f, travel.getLength()));

        const bool onCurve = (i >= spec.sliders.size() || spec.sliders[i].joinCurve);
        if (! onCurve)
            continue;

        nodes.push_back (node);

        const float hz = (i < spec.sliders.size()) ? spec.sliders[i].axisHz : 0.0f;
        if (hz > 0.0f)
            bands.push_back ({ node.x, hz,
                               static_cast<float> (faders[i]->getSlider().getValue()) });
        else
            haveFrequencies = false;
    }

    const auto unit = [] (juce::Point<float> v)
    {
        const float len = juce::jmax (1.0e-4f, v.getDistanceFromOrigin());
        return juce::Point<float> (v.x / len, v.y / len);
    };

    // Eased polyline: straight between points, corners rounded off.
    const auto easedPath = [&unit] (const std::vector<juce::Point<float>>& p)
    {
        juce::Path curve;
        if (p.size() < 2)
            return curve;

        constexpr float kCornerRadius = 8.0f;
        curve.startNewSubPath (p.front());

        for (size_t i = 1; i + 1 < p.size(); ++i)
        {
            const auto prev = p[i - 1], cur = p[i], next = p[i + 1];
            const float rIn  = juce::jmin (kCornerRadius, (prev - cur).getDistanceFromOrigin() * 0.45f);
            const float rOut = juce::jmin (kCornerRadius, (next - cur).getDistanceFromOrigin() * 0.45f);
            curve.lineTo (cur + unit (prev - cur) * rIn);
            curve.quadraticTo (cur, cur + unit (next - cur) * rOut);
        }

        curve.lineTo (p.back());
        return curve;
    };

    std::vector<juce::Point<float>> points;

    if (haveFrequencies && bands.size() >= 2)
    {
        // Map between x and log2(frequency) along the band anchors, and read the
        // fader gain at any frequency by interpolating between them.
        const auto lg = [] (float hz) { return std::log2 (juce::jmax (hz, 1.0f)); };

        const auto xForHz = [&] (float hz)
        {
            const float L = lg (hz);
            if (L <= lg (bands.front().hz))
            {
                const auto& a = bands[0]; const auto& b = bands[1];
                return a.x + (b.x - a.x) * (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
            }
            for (size_t i = 1; i < bands.size(); ++i)
                if (L <= lg (bands[i].hz))
                {
                    const auto& a = bands[i - 1]; const auto& b = bands[i];
                    return a.x + (b.x - a.x) * (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
                }
            const auto& a = bands[bands.size() - 2]; const auto& b = bands.back();
            return b.x + (b.x - a.x) * (L - lg (b.hz)) / (lg (b.hz) - lg (a.hz));
        };

        const auto gainDbAtHz = [&] (float hz)
        {
            const float L = lg (hz);
            if (L <= lg (bands.front().hz)) return bands.front().db;
            if (L >= lg (bands.back().hz))  return bands.back().db;
            for (size_t i = 1; i < bands.size(); ++i)
                if (L <= lg (bands[i].hz))
                {
                    const auto& a = bands[i - 1]; const auto& b = bands[i];
                    const float t = (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
                    return a.db + t * (b.db - a.db);
                }
            return bands.back().db;
        };

        // Active cut frequencies.
        float loHz = 0.0f, hiHz = 0.0f;
        for (size_t i = 0; i < cornerKnobs.size() && i < spec.cornerKnobs.size(); ++i)
        {
            const auto side = spec.cornerKnobs[i].cutSide;
            if (side == CutSide::none)
                continue;

            auto& s = cornerKnobs[i]->getSlider();
            const auto v = static_cast<float> (s.getValue());
            if (side == CutSide::low  && v > static_cast<float> (s.getMinimum()) + 0.5f) loHz = v;
            if (side == CutSide::high && v < static_cast<float> (s.getMaximum()) - 0.5f) hiHz = v;
        }

        constexpr float kCutSlopeDbPerOct = 14.0f;
        const auto rolloffDb = [&] (float hz)
        {
            float d = 0.0f;
            if (loHz > 0.0f && hz < loHz) d -= kCutSlopeDbPerOct * std::log2 (loHz / juce::jmax (hz, 1.0f));
            if (hiHz > 0.0f && hz > hiHz) d -= kCutSlopeDbPerOct * std::log2 (hz / hiHz);
            return d;
        };

        const float midY = travel.getStart() + travel.getLength() * 0.5f;
        const float pxPerDb = travel.getLength() / 30.0f;   // +/-15 dB across the grid
        const auto yForDb = [&] (float db) { return midY - db * pxPerDb; };
        const auto pointAtHz = [&] (float hz)
        {
            return juce::Point<float> (xForHz (hz), yForDb (gainDbAtHz (hz) + rolloffDb (hz)));
        };

        // Low-cut tail: octave steps down from the corner, until well off-grid.
        if (loHz > 0.0f)
            for (float f = loHz; f >= 15.0f; f *= 0.5f)
            {
                const auto p = pointAtHz (f);
                points.push_back (p);
                if (p.x < area.getX() - 24.0f || p.y > travel.getEnd() + 90.0f)
                    break;
            }

        for (const auto& b : bands)
            points.push_back ({ b.x, yForDb (b.db + rolloffDb (b.hz)) });

        // High-cut tail: octave steps up from the corner.
        if (hiHz > 0.0f)
            for (float f = hiHz; f <= 22000.0f; f *= 2.0f)
            {
                const auto p = pointAtHz (f);
                points.push_back (p);
                if (p.x > area.getRight() + 24.0f || p.y > travel.getEnd() + 90.0f)
                    break;
            }

        std::sort (points.begin(), points.end(),
                   [] (auto& a, auto& b) { return a.x < b.x; });
        points.erase (std::unique (points.begin(), points.end(),
                                   [] (auto& a, auto& b) { return std::abs (a.x - b.x) < 1.0f; }),
                      points.end());
    }
    else
    {
        points = nodes;
    }

    if (points.size() >= 2)
    {
        // The outer clip already confines the tail to the rounded grid, so it
        // visibly runs off the bottom.
        g.setColour (kFaderCurveColour);
        g.strokePath (easedPath (points),
                      juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    }
}

void PedalEditor::Face::paintCutMasks (juce::Graphics& g, juce::Rectangle<float> grid) const
{
    // A shared log-frequency axis across every cut knob's full range, so the
    // low- and high-cut shading land on the same x where their ranges meet.
    float axisMinHz = 0.0f;
    float axisMaxHz = 0.0f;

    for (size_t i = 0; i < cornerKnobs.size() && i < spec.cornerKnobs.size(); ++i)
    {
        if (spec.cornerKnobs[i].cutSide == CutSide::none)
            continue;

        auto& s = cornerKnobs[i]->getSlider();
        const auto lo = static_cast<float> (s.getMinimum());
        const auto hi = static_cast<float> (s.getMaximum());

        axisMinHz = (axisMinHz <= 0.0f) ? lo : juce::jmin (axisMinHz, lo);
        axisMaxHz = juce::jmax (axisMaxHz, hi);
    }

    if (axisMinHz <= 0.0f || axisMaxHz <= axisMinHz)
        return;

    const float logMin = std::log (axisMinHz);
    const float logSpan = std::log (axisMaxHz) - logMin;

    const auto xForHz = [&] (float hz)
    {
        const float t = juce::jlimit (0.0f, 1.0f,
                                      (std::log (juce::jmax (hz, 1.0f)) - logMin) / logSpan);
        return grid.getX() + t * grid.getWidth();
    };

    for (size_t i = 0; i < cornerKnobs.size() && i < spec.cornerKnobs.size(); ++i)
    {
        const auto side = spec.cornerKnobs[i].cutSide;
        if (side == CutSide::none)
            continue;

        const float edgeX = xForHz (static_cast<float> (cornerKnobs[i]->getSlider().getValue()));
        const bool fromLeft = (side == CutSide::low);

        const auto band = fromLeft ? grid.withRight (edgeX) : grid.withLeft (edgeX);
        if (band.getWidth() < 0.5f)
            continue;

        // Dense at the outer edge the shading grew from, fading to nothing at
        // the cutoff so there is no visible line where it stops.
        juce::ColourGradient grad (juce::Colours::black.withAlpha (0.16f),
                                   fromLeft ? band.getX() : band.getRight(), band.getCentreY(),
                                   juce::Colours::transparentBlack,
                                   fromLeft ? band.getRight() : band.getX(), band.getCentreY(),
                                   false);
        g.setGradientFill (grad);
        g.fillRect (band);
    }
}

void PedalEditor::Face::resetFaders()
{
    const auto toDefault = [] (juce::Slider& s)
    {
        s.setValue (s.getDoubleClickReturnValue(), juce::sendNotificationSync);
    };

    // Recentre the group trims first, silently: the faders are flattened just
    // below, so there is nothing for their delta to push.
    for (auto& trim : groupTrims)
        trim->resetToCentre();

    for (auto& fader : faders)
        toDefault (fader->getSlider());

    // The corner cut knobs go back to their defaults too (low cut off, high
    // cut wide open).
    for (auto& knob : cornerKnobs)
        toDefault (knob->getSlider());
}

void PedalEditor::Face::layOutFaders (juce::Rectangle<int> area)
{
    const int count = static_cast<int> (faders.size());

    if (count == 0 || area.isEmpty())
        return;

    // Trim a little off the bottom of the fader/grid band, leaving a gap
    // between the value readouts and the pedal name.
    area = area.removeFromTop (juce::roundToInt (area.getHeight() * 0.95f));

    // Even columns across the area; the fader sits centred in its column at a
    // fixed width, spanning the full height so the throw is as long as the face
    // allows.
    const int cellWidth = area.getWidth() / count;
    const int faderWidth = juce::jmin (kFaderWidth, cellWidth);

    for (int i = 0; i < count; ++i)
    {
        auto cell = area.removeFromLeft (i < count - 1 ? cellWidth : area.getWidth());
        faders[static_cast<size_t> (i)]->setBounds (cell.withSizeKeepingCentre (faderWidth, cell.getHeight()));
    }
}

void PedalEditor::Face::paint (juce::Graphics& g)
{
    auto bounds = faceBounds().toFloat();

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

    // Sits under the fader children, which paint their stems and nodes on top.
    paintFaderGraph (g);

    // Rule separating the group-trim cluster from the corner cut knobs.
    if (! groupTrimDivider.isEmpty())
    {
        g.setColour (theme.outline.withAlpha (0.3f));
        g.fillRect (groupTrimDivider);
    }

    // Rule splitting the knob grid into its two clusters.
    if (! knobDivider.isEmpty())
    {
        g.setColour (theme.outline.withAlpha (0.35f));
        g.fillRect (knobDivider);
    }

    // A small emblem for the effect, centred in the gap the knobs leave above
    // the name. Drawn before the name so it can never sit over the lettering.
    if (emblemImage.isValid() && spec.titleImageHeight > 0)
    {
        const auto title = titleArea();
        const float h = static_cast<float> (spec.titleImageHeight);
        const float w = h * static_cast<float> (emblemImage.getWidth())
                          / static_cast<float> (juce::jmax (1, emblemImage.getHeight()));

        const auto slot = juce::Rectangle<float> (w, h)
                              .withCentre ({ static_cast<float> (title.getCentreX()),
                                             static_cast<float> (title.getY()) - h * 0.5f });

        g.drawImage (emblemImage, slot, juce::RectanglePlacement::centred);
    }

    // Name sits under the knobs, the way it is screened onto a real pedal.
    // That also keeps the top of the face free instead of carrying a header.
    // `titleBesideLogo` puts it on the logo row instead, to the right of the
    // emblem, and hands the row it would have had back to the controls.
    auto nameArea = titleArea();
    auto logoSlot = logoArea();

    if (spec.titleBesideLogo)
    {
        // The emblem keeps a column of its own on the left, at the row's full
        // height; the name takes everything to the right of it.
        const float aspect = logoImage.isValid()
                                 ? static_cast<float> (logoImage.getWidth())
                                       / static_cast<float> (juce::jmax (1, logoImage.getHeight()))
                                 : 1.0f;

        auto row = logoSlot;
        const auto titleFont = theme.titleFont (58.0f);
        const int logoW = juce::roundToInt (static_cast<float> (row.getHeight()) * aspect);
        // The title face is a script with swashes that overhang its advance
        // widths, so the measured string gets a margin either side of it.
        const int nameW = juce::roundToInt (
            juce::GlyphArrangement::getStringWidth (titleFont, spec.name) * 1.14f + 10.0f);

        auto cluster = spec.titleRowAlignRight
                           ? row.removeFromRight (logoW + kLogoNameGap + nameW)
                           : row.removeFromLeft (logoW + kLogoNameGap + nameW);

        logoSlot = cluster.removeFromLeft (logoW);
        cluster.removeFromLeft (kLogoNameGap);
        nameArea = cluster;
    }

    g.setColour (theme.title);
    g.setFont (theme.titleFont (58.0f));
    g.drawText (spec.name, nameArea,
                spec.titleBesideLogo ? juce::Justification::centredLeft
                                     : juce::Justification::centred,
                false);

    if (const auto logo = logoImage; logo.isValid())
    {
        g.setOpacity (0.92f);
        g.drawImage (logo, logoSlot.toFloat(), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    if (spec.version.isNotEmpty())
    {
        // Out of the emblem's way when that has moved into the bottom-left.
        g.setColour (theme.textSecondary.withAlpha (0.7f));
        g.setFont (theme.bodyFont (10.5f));
        g.drawText (spec.version,
                    faceBounds().reduced (kMargin),
                    spec.titleBesideLogo ? juce::Justification::bottomRight
                                         : juce::Justification::bottomLeft,
                    false);
    }
}

void PedalEditor::Face::resized()
{
    if (sidePanel != nullptr)
        sidePanel->setBounds (getLocalBounds().withTrimmedLeft (spec.width));

    auto area = knobArea();

    const int count = static_cast<int> (knobs.size());
    const int perRow = juce::jlimit (1, juce::jmax (1, count), spec.knobsPerRow);

    // Columns span the full content width; the rotary sits centred in its
    // column at a fixed size.
    const int cellWidth = (area.getWidth() - kKnobGap * (perRow - 1)) / perRow;
    const int maxKnob = spec.knobDiameter > 0 ? spec.knobDiameter : kKnobDiameter;
    const int knobWidth = juce::jmin (maxKnob, cellWidth);
    const int rowHeight = knobWidth + Knob::labelHeight;

    // A face with something between its rows can ask for them to be spread; the
    // block stays centred in its area, so the extra lifts the top row and drops
    // the bottom one by half each.
    const int rowGap = spec.knobRowGap > 0 ? spec.knobRowGap : kKnobGap;

    // Centre the knob block when it does not fill the area - a single row with a
    // switch strip above and a preview band below would otherwise sit high. Only
    // done on the new layout, so the other pedals are untouched.
    if (count > 0 && (hasBottomBand() || slideToggle != nullptr))
    {
        const int rows = (count + perRow - 1) / perRow;
        const int blockHeight = rows * rowHeight + (rows - 1) * rowGap;
        const int slack = area.getHeight() - blockHeight;
        if (slack > 0)
            area.removeFromTop (slack / 2);
    }

    // Lift the block clear of where the area starts. Applied after the centring
    // so it still bites once the rows already fill their area - which is when a
    // face that wants them higher has nothing left to take.
    if (spec.knobBlockRise > 0)
    {
        const int headroom = juce::jmax (0, area.getY() - contentArea().getY());
        area.translate (0, -juce::jmin (spec.knobBlockRise, headroom));
    }

    knobCells.assign (static_cast<size_t> (count), {});

    for (int first = 0; first < count; first += perRow)
    {
        auto knobRow = area.removeFromTop (rowHeight);
        area.removeFromTop (rowGap);

        const int inRow = juce::jmin (perRow, count - first);

        // A trailing row that does not fill its columns is centred, so a lone
        // last knob sits under the gap between the pair above it rather than
        // hanging off the left. Full rows keep their exact column maths.
        if (inRow < perRow)
        {
            const int usedWidth = inRow * cellWidth + (inRow - 1) * kKnobGap;
            knobRow.removeFromLeft ((knobRow.getWidth() - usedWidth) / 2);
        }

        for (int i = 0; i < inRow; ++i)
        {
            auto cell = knobRow.removeFromLeft (cellWidth);

            // Kept for every slot, spacers included, so a toggle can be anchored
            // to an empty cell.
            knobCells[static_cast<size_t> (first + i)] = cell;

            if (auto& knob = knobs[static_cast<size_t> (first + i)]; knob != nullptr)
            {
                // A knob may ask for a smaller cap than the row's. Its bounds
                // keep the row height, so its cap centre and its label rows
                // still line up with its neighbours' - it is just a smaller
                // cap in the same cell.
                const int own = spec.knobs[static_cast<size_t> (first + i)].diameter;
                const int width = own > 0 ? juce::jmin (own, knobWidth) : knobWidth;

                auto bounds = cell.withSizeKeepingCentre (width, rowHeight);

                // Out towards the edges: left of the row's middle goes left,
                // right goes right, and a middle column stays where it is.
                if (spec.knobColumnSpread != 0 && inRow > 1)
                {
                    const int side = i * 2 < inRow - 1 ? -1 : (i * 2 > inRow - 1 ? 1 : 0);
                    bounds.translate (side * spec.knobColumnSpread, 0);
                }

                knob->setBounds (bounds);
            }

            if (i < inRow - 1)
                knobRow.removeFromLeft (kKnobGap);
        }
    }

    // Rule down the gap between two columns, as tall as the whole block.
    knobDivider = {};
    if (const int after = spec.knobDividerAfterColumn;
        after > 0 && after < perRow && count > after)
    {
        auto block = knobCells.front();
        for (const auto& cell : knobCells)
            block = block.getUnion (cell);

        const int left = knobCells[static_cast<size_t> (after) - 1].getRight();
        const int right = knobCells[static_cast<size_t> (after)].getX();

        knobDivider = juce::Rectangle<int> ((left + right) / 2, block.getY() + 4,
                                            1, block.getHeight() - 8);
    }

    // Centre knob: dropped into the middle of the caps' bounding box, at the
    // corner-knob size so it reads as a utility control against the main row.
    if (centreKnob != nullptr && count > 0)
    {
        juce::Rectangle<int> caps;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            if (knobs[i] == nullptr)
                continue;

            const auto capBounds =
                knobs[i]->getBounds().withTrimmedBottom (knobs[i]->getLabelHeight());
            caps = caps.isEmpty() ? capBounds : caps.getUnion (capBounds);
        }

        // Corner-knob size unless the spec asks for its own, and whichever label
        // block that knob actually draws.
        const int diameter = spec.centreKnob->diameter > 0 ? spec.centreKnob->diameter
                                                           : kCornerKnobDiameter;
        const int labelHeight = centreKnob->getLabelHeight();

        auto bounds = juce::Rectangle<int> (diameter, diameter + labelHeight)
                          .withCentre (caps.getCentre());

        // A full-size centre knob carries a tall label block - value and caption
        // both - which would push its cap well above the middle if the whole
        // component were centred. Sit the cap on the centre instead and let the
        // label hang below it. Compact centre knobs keep the original placement,
        // so Peak Reverb's RESO does not move.
        if (! spec.centreKnob->compact)
            bounds.setY (caps.getCentreY() - diameter / 2);

        centreKnob->setBounds (bounds);
    }

    // Secondary knob row: small knobs clustered in the middle, each with its
    // button (if any) pinned centred beneath its label.
    if (! subKnobs.empty())
    {
        auto row = subKnobArea();
        const int n = static_cast<int> (subKnobs.size());
        // Cells wide enough for the caption, packed tight rather than spread.
        const int cellW = juce::jmin (row.getWidth() / juce::jmax (1, n),
                                      juce::jmax (kSubKnobDiameter + 26, 84));
        const int blockW = n * cellW + (n - 1) * kSubKnobGap;
        row.removeFromLeft (juce::jmax (0, (row.getWidth() - blockW) / 2));
        const int knobCellH = kSubKnobDiameter + subLabelHeight();

        for (int i = 0; i < n; ++i)
        {
            auto cell = row.removeFromLeft (cellW);
            if (i < n - 1)
                row.removeFromLeft (kSubKnobGap);

            subKnobs[static_cast<size_t> (i)]->setBounds (
                cell.removeFromTop (knobCellH).withSizeKeepingCentre (kSubKnobDiameter, knobCellH));

            if (auto& button = subButtons[static_cast<size_t> (i)])
            {
                cell.removeFromTop (kSubButtonGap);
                button->setBounds (juce::Rectangle<int> (MiniToggle::preferredWidth,
                                                         MiniToggle::preferredHeight)
                                       .withCentre ({ cell.getCentreX(),
                                                      cell.getY() + MiniToggle::preferredHeight / 2 }));
            }
        }
    }

    // Faders take whatever is left below the knob rows. On a knob-less pedal
    // (a graphic EQ) that is the whole control area.
    if (! faders.empty())
    {
        if (count > 0)
            area.removeFromTop (kFaderRowGap);

        // Group-trim knobs (hard left) and any corner cut knobs (hard right)
        // share a strip in the gap above the grid. A wide face can ask for a
        // larger cap; the cell keeps the same text padding either way.
        const int compactDia = spec.compactKnobDiameter > 0 ? spec.compactKnobDiameter
                                                            : kCornerKnobDiameter;
        const int cornerCellW = compactDia + (kCornerKnobWidth - kCornerKnobDiameter);
        const int compactKnobH = compactDia + Knob::compactLabelHeight;

        int stripH = 0;
        if (! groupTrims.empty())
            stripH = juce::jmax (stripH, compactKnobH);
        if (! cornerKnobs.empty())
            stripH = juce::jmax (stripH, compactKnobH);

        groupTrimDivider = {};

        if (stripH > 0)
        {
            const auto strip = area.removeFromTop (stripH);

            // Trims carry only a short caption, so their cell is just the cap
            // width - narrower than a corner knob's text-padded cell, which
            // leaves room for the divider beside the cut knobs.
            const int trimW = compactDia;

            int leftX = strip.getX();
            for (auto& trim : groupTrims)
            {
                trim->setBounds (leftX, strip.getY(), trimW, compactKnobH);
                leftX += trimW + kCornerKnobGap;
            }

            int x = strip.getRight() - cornerCellW;
            for (auto it = cornerKnobs.rbegin(); it != cornerKnobs.rend(); ++it)
            {
                (*it)->setBounds (x, strip.getY(), cornerCellW, compactKnobH);
                x -= cornerCellW + kCornerKnobGap;
            }

            // Rule halfway between the two clusters, as tall as the caps.
            if (! groupTrims.empty() && ! cornerKnobs.empty())
            {
                const int groupsRight = leftX - kCornerKnobGap;
                const int knobsLeft = x + cornerCellW + kCornerKnobGap;
                groupTrimDivider = juce::Rectangle<int> ((groupsRight + knobsLeft) / 2,
                                                         strip.getY() + 2, 1,
                                                         compactDia - 4);
            }
        }

        // Nudge the grid down, clear of the strip caps.
        area.removeFromTop (kFaderGridGap);

        layOutFaders (area);
    }

    // Fader RESET: below the grid, left-aligned, level with the pedal name.
    if (faderResetButton != nullptr)
    {
        const auto title = titleArea();
        faderResetButton->setBounds (
            juce::Rectangle<int> (kResetButtonWidth, kResetButtonHeight)
                .withPosition (title.getX(), title.getCentreY() - kResetButtonHeight / 2));
    }

    // A toggle either straddles the gap after `afterKnobIndex` or sits centred
    // above that knob's cap, depending on the spec.
    for (size_t t = 0; t < toggles.size(); ++t)
    {
        const auto& tSpec = spec.toggles[t];
        const int index = tSpec.afterKnobIndex;

        const bool spacerAnchor = index >= 0 && index < count
                                      && knobs[static_cast<size_t> (index)] == nullptr;

        const bool haveAnchor = (tSpec.centeredAbove || tSpec.centeredBelow || spacerAnchor)
            ? (index >= 0 && index < count)
            : (index >= 0 && index + 1 < count
               && knobs[static_cast<size_t> (index)] != nullptr
               && knobs[static_cast<size_t> (index + 1)] != nullptr);

        if (! haveAnchor || index >= static_cast<int> (knobCells.size()))
        {
            toggles[t]->setVisible (false);
            continue;
        }

        const auto cell = knobCells[static_cast<size_t> (index)];
        juce::Point<int> centre;

        if (spacerAnchor)
        {
            // An empty column: the button takes the middle of it, level with the
            // caps either side.
            centre = { cell.getCentreX(),
                       cell.getY() + (cell.getHeight() - Knob::labelHeight) / 2 };
        }
        else if (tSpec.centeredBelow)
        {
            // Hung just under the knob's own printed label - not under its
            // cell, which may carry an empty row the label never uses.
            const auto& anchor = *knobs[static_cast<size_t> (index)];
            centre = { anchor.getBounds().getCentreX(),
                       anchor.printedTextBottom() + MiniToggle::preferredHeight / 2 + 4 };
        }
        else if (tSpec.centeredAbove)
        {
            // Just above the cap: the cap fills the knob bounds minus the label
            // block at the bottom.
            const auto anchor = knobs[static_cast<size_t> (index)]->getBounds();
            centre = { anchor.getCentreX(),
                       anchor.getY() - MiniToggle::preferredHeight / 2 - 5 };
        }
        else
        {
            const auto anchor = knobs[static_cast<size_t> (index)]->getBounds();
            const auto right = knobs[static_cast<size_t> (index + 1)]->getBounds();
            if (right.getY() != anchor.getY())
            {
                toggles[t]->setVisible (false);
                continue;
            }
            centre = { (anchor.getRight() + right.getX()) / 2,
                       anchor.getY() + (anchor.getHeight() - Knob::labelHeight) / 2 - kToggleRise };
        }

        toggles[t]->setVisible (true);
        toggles[t]->setBounds (juce::Rectangle<int> (MiniToggle::preferredWidth,
                                                     MiniToggle::preferredHeight)
                                   .withCentre (centre));
    }

    // Sliding switch: top-left of its own strip, or - in bottom mode -
    // left-aligned on the pedal-name row, level with the title. A face whose
    // switch is the only thing in its strip can centre it instead.
    if (slideToggle != nullptr)
    {
        // On a face whose name has moved onto the logo row, "bottom" means that
        // row: the switch takes the left of it, opposite the name.
        const auto strip = ! bottomSwitch()      ? switchStripArea()
                         : spec.titleBesideLogo  ? logoArea()
                                                 : titleArea();
        const int x = spec.slideToggleCentred
                          ? strip.getCentreX() - SlideToggle::preferredWidth / 2
                          : strip.getX() - slideToggle->labelInset();

        slideToggle->setBounds (juce::Rectangle<int> (SlideToggle::preferredWidth,
                                                      SlideToggle::preferredHeight)
                                    .withPosition (x,
                                                   strip.getCentreY() - SlideToggle::preferredHeight / 2
                                                       - spec.slideToggleRise));
    }

    if (waveDisplay != nullptr)
        waveDisplay->setBounds (waveDisplayArea());

    if (filterScope != nullptr)
        filterScope->setBounds (waveDisplayArea());
}

//==============================================================================
PedalEditor::PedalEditor (juce::AudioProcessor& processor,
                          juce::AudioProcessorValueTreeState& state,
                          PedalSpec specToUse,
                          PedalTheme themeToUse)
    : juce::AudioProcessorEditor (processor),
      theme (std::move (themeToUse)),
      lookAndFeel (theme)
{
    setLookAndFeel (&lookAndFeel);

    face = std::make_unique<Face> (state, std::move (specToUse), theme, lookAndFeel);
    addAndMakeVisible (*face);

    baseWidth = face->getLogicalWidth();
    baseHeight = face->getLogicalHeight();

    setResizable (true, false);
    applyResizeLimits();

    resizeGrip = std::make_unique<juce::ResizableCornerComponent> (this, getConstrainer());
    addAndMakeVisible (*resizeGrip);

    // Open a little smaller than the design size.
    setSize (juce::roundToInt (baseWidth  * kDefaultZoom),
             juce::roundToInt (baseHeight * kDefaultZoom));
}

PedalEditor::~PedalEditor()
{
    setLookAndFeel (nullptr);
}

void PedalEditor::applyResizeLimits()
{
    setResizeLimits (juce::roundToInt (baseWidth  * kMinZoom),
                     juce::roundToInt (baseHeight * kMinZoom),
                     juce::roundToInt (baseWidth  * kMaxZoom),
                     juce::roundToInt (baseHeight * kMaxZoom));

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
}

void PedalEditor::setSidePanel (std::unique_ptr<juce::Component> panel, int panelWidth)
{
    face->setSidePanel (std::move (panel), panelWidth);

    baseWidth = face->getLogicalWidth();
    baseHeight = face->getLogicalHeight();

    applyResizeLimits();
    setSize (juce::roundToInt (baseWidth  * kDefaultZoom),
             juce::roundToInt (baseHeight * kDefaultZoom));
}

void PedalEditor::paint (juce::Graphics& g)
{
    // The face covers the whole window; this only shows through sub-pixel seams.
    g.fillAll (theme.background);
}

void PedalEditor::resized()
{
    if (face == nullptr || baseWidth <= 0 || baseHeight <= 0)
        return;

    const float scale = juce::jmax (0.1f, static_cast<float> (getWidth()) / static_cast<float> (baseWidth));

    face->setBounds (0, 0, baseWidth, baseHeight);
    face->setTransform (juce::AffineTransform::scale (scale));

    if (resizeGrip != nullptr)
    {
        // Big enough that the strokes, drawn well inside it, clear the frame
        // and drop shadow.
        const int s = juce::jmax (32, juce::roundToInt (50.0f * scale));
        resizeGrip->setBounds (getWidth() - s, getHeight() - s, s, s);
        resizeGrip->toFront (false);
    }
}

} // namespace ee::ui
