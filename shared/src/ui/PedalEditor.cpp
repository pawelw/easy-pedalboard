#include "ee/ui/PedalEditor.h"

#include "ee/ui/DigitalSwitch.h"
#include "ee/ui/DigitalToggle.h"
#include "ee/ui/FilterScope.h"
#include "ee/ui/GrainScope.h"
#include "ee/ui/PresetBar.h"
#include "ee/ui/SlideToggle.h"
#include "ee/ui/WaveDisplay.h"

#include "BinaryData.h"

#include <melatonin_blur/melatonin_blur.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ee::ui
{
namespace
{
    constexpr float kBorderThickness = 5.0f; // frame hugging the outer edge
    constexpr float kFaceInset = kBorderThickness;
    constexpr int kShadowDepth = 12; // how far the face is sunk below the frame

    // Everything on the face is spaced from the inside edge of the frame.
    constexpr int kContentPad = 16;
    constexpr int kMargin = static_cast<int> (kFaceInset) + kContentPad;
    static_assert (kMargin == kFaceContentMargin, "shared face margin out of sync");

    constexpr int kKnobGap = kKnobColumnGap;

    // Faders sit in one row below the knobs (or fill the whole control area on
    // a pedal that has no knobs). Fixed cap width so a fader is the same size
    // however many share the row.
    constexpr int kFaderWidth = 48; // strip width; the cap drawn inside is narrower
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
    constexpr int kSubKnobGap = 4;   // sub knobs cluster tighter than the main row
    constexpr int kSubRowGap = 10;   // between the main rows and the sub row
    constexpr int kSubButtonGap = 3; // between a sub knob's label and its button
    constexpr int kSubGroupPad = 12; // how far the group box stands off its knobs
    constexpr int kSubGroupCaptionHeight = 13;

    // Named knob groups laid out side by side (`knobGroupsHorizontal`).
    constexpr int kKnobGroupColsDefault = 2;    // knobs per row inside a group block
    constexpr int kKnobGroupGap = 40;           // between blocks: 8 px clear of the panels' 16 px pad each side
    constexpr int kKnobGroupPad = 16;           // how far a filled group panel stands off its knobs
    constexpr int kKnobGroupCaptionHeight = 20; // the panel caption strip - taller than the outline style's
    constexpr int kKnobGroupToggleReserve = 34; // bottom slack for a Sync toggle hung under a bottom-row knob
    constexpr int kKnobCellSidePad = 14;        // slack each side of a cap in its column (label room, horizontal groups)
    constexpr int kKnobGroupExtraTop = 32;      // extra air between the caption and the first knob
    constexpr int kKnobGroupExtraBottom = 32;   // extra air below the last knob's label

    // Every filled card's shadow, CSS `box-shadow` style: a stack of real
    // gaussian layers { colour, radius, { dx, dy }, spread }. Shared by every
    // card so they read the same; tune the whole look here.
    // The top-edge highlight is a clipped 1 px stroke, not a shadow (see
    // paintGroupBox); these layers are the dark drop shadow only. Empty = none.
    //
    // CSS: box-shadow: -2px 7px 6px -5px #00000094
    //      (offset-x offset-y blur spread colour) -> { colour, radius, {dx,dy}, spread }
    const std::vector<melatonin::ShadowParametersInt> kCardShadows {
        { juce::Colour::fromRGBA (0, 0, 0, 0x94), 5, { -2, 4 }, -3 },
    };

    // Fixed rather than a fraction of the column, so a knob is the same size on
    // every pedal however many of them a row carries.
    constexpr int kKnobDiameter = 114;

    // The title script's swashes overhang their advance widths, so every
    // measurement of the pedal name gets this much margin either side.
    constexpr float kTitleSwash = 1.14f;

    constexpr int kTitleHeight = 64;
    constexpr int kLogoHeight = 54;

    juce::Image brandLogo()
    {
        static const juce::Image logo =
            juce::ImageCache::getFromMemory (BinaryData::peaklogo_png, BinaryData::peaklogo_pngSize);
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
} // namespace

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
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
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

    int getLogicalWidth() const { return spec.width + sidePanelWidth; }
    int getLogicalHeight() const { return spec.height; }

private:
    juce::Rectangle<int> faceBounds() const;
    juce::Rectangle<int> contentArea() const;
    bool topSwitch() const;
    bool bottomSwitch() const;

    /** Whether a strip is carved off the top of the content area - for the slide
        toggle, the preset bar, or both. */
    bool hasTopStrip() const;

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

    /** Lay the named knob groups out as side-by-side blocks (each `columns` wide,
        an odd knob leading on its own short row), vertically centred against the
        tallest block. Fills `knobCells`; `knobGroupBoxes` is taken from it
        afterwards, the same as the plain layout. */
    void layOutKnobGroupsRow (juce::Rectangle<int> area);

    /** The face the pedal name is set in. The analog faces use a big script at
        58 px; the digital one has no script - it sets the name in the same
        geometric sans as the captions, at a size that reads beside them. */
    juce::Font nameFont() const;

    /** That face, shrunk if the name is wider than the row it has to sit in.
        A long name on a narrow one-knob-per-row face would otherwise be clipped
        mid-letter. */
    juce::Font fittedNameFont (juce::Rectangle<int> area) const;

    void paintFaderGraph (juce::Graphics&) const;
    void paintCutMasks (juce::Graphics&, juce::Rectangle<float> grid) const;

    /** The rounded outline with a caption let into its top edge, shared by the
        sub-knob group and the named knob groups. With `filled`, a panel a shade
        lighter than the face with the caption inside its top edge instead. */
    void paintGroupBox (juce::Graphics&,
                        juce::Rectangle<int> box,
                        const juce::String& caption,
                        bool filled = false,
                        juce::Colour fill = {}) const;

    void resetFaders();

    PedalTheme theme;
    PedalSpec spec;

    std::vector<std::unique_ptr<Knob>> knobs; // a null entry is a spacer column

    /** Bounds of every knob-grid slot, spacers included. */
    std::vector<juce::Rectangle<int>> knobCells;
    std::unique_ptr<Knob> centreKnob;
    std::unique_ptr<Knob> topRightKnob;
    std::vector<std::unique_ptr<Knob>> cornerKnobs;
    std::vector<std::unique_ptr<Knob>> subKnobs;
    std::vector<std::unique_ptr<MiniToggle>> subButtons; // index-aligned with subKnobs; null where none
    std::vector<std::unique_ptr<GroupTrim>> groupTrims;
    std::vector<std::unique_ptr<FaderStrip>> faders;
    /** A toggle is a lit bezel button, or - where the spec asks for one, or the
        theme is digital - a small sliding switch. Both are `juce::Button`s of
        different shapes, so the layout goes through `SwitchControl` rather than
        through either class. `metrics` always points at `button`. */
    struct ToggleEntry
    {
        std::unique_ptr<juce::Button> button;
        SwitchControl* metrics = nullptr;
    };

    std::vector<ToggleEntry> toggles;

    /** The big two-way switch, in whichever style the theme asks for. */
    std::unique_ptr<juce::Button> slideToggle;
    SwitchControl* slideToggleMetrics = nullptr;
    std::unique_ptr<PresetBar> presetBar;
    std::unique_ptr<WaveDisplay> waveDisplay;
    std::unique_ptr<FilterScope> filterScope;
    std::unique_ptr<GrainScope> grainScope;
    std::unique_ptr<juce::TextButton> faderResetButton;

    /** Vertical rule between the group-trim cluster and the corner cut knobs.
        Empty when either cluster is absent. */
    juce::Rectangle<int> groupTrimDivider;

    /** Vertical rule splitting the knob grid into two clusters. Empty unless
        the spec asks for one. */
    juce::Rectangle<int> knobDivider;

    /** Outline around the sub-knob cluster, when the spec names the group. */
    juce::Rectangle<int> subKnobGroup;

    /** Outline around each named knob group, index-aligned with
        `spec.knobGroups`. Empty unless the spec names groups. */
    std::vector<juce::Rectangle<int>> knobGroupBoxes;

    std::unique_ptr<juce::Component> sidePanel;
    int sidePanelWidth = 0;
    juce::Image grain;
    juce::Image logoImage;
    juce::Image emblemImage; // spec.titleImage, tinted if the spec asks

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
};

//==============================================================================
PedalEditor::Face::Face (juce::AudioProcessorValueTreeState& state,
                         PedalSpec specToUse,
                         const PedalTheme& themeToUse,
                         PedalLookAndFeel& lnf)
    : theme (themeToUse), spec (std::move (specToUse))
{
    setLookAndFeel (&lnf);

    for (const auto& knobSpec : spec.knobs)
    {
        if (knobSpec.parameterID.isEmpty())
        {
            knobs.push_back (nullptr); // a spacer: holds its column, draws nothing
            continue;
        }

        // `captionUntilTouchedKnobs` flips every knob on the face to the
        // caption-at-rest / value-while-turning behaviour without touching each
        // KnobSpec.
        auto ks = knobSpec;
        if (spec.captionUntilTouchedKnobs)
            ks.captionUntilTouched = true;

        knobs.push_back (std::make_unique<Knob> (state, ks, theme));
    }

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

    if (spec.topRightKnob.has_value())
    {
        topRightKnob = std::make_unique<Knob> (state, *spec.topRightKnob, theme);
        addAndMakeVisible (*topRightKnob);
    }

    for (const auto& sliderSpec : spec.sliders)
        faders.push_back (std::make_unique<FaderStrip> (state, sliderSpec, theme));

    for (auto& fader : faders)
        addAndMakeVisible (*fader);

    for (const auto& knobSpec : spec.cornerKnobs)
    {
        auto knob = std::make_unique<Knob> (state, knobSpec, theme);
        knob->onValueChanged = [this] { repaint(); }; // the cut masks track it
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
            repaint(); // the response curve follows the faders
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
        ToggleEntry entry;

        if (toggleSpec.asSwitch.has_value())
        {
            // The toggle's own parameter drives the switch; the nested spec is
            // only there to carry the two labels and how they are ordered.
            auto switchSpec = *toggleSpec.asSwitch;
            switchSpec.parameterID = toggleSpec.parameterID;

            auto sw = std::make_unique<DigitalSwitch> (state, switchSpec, theme, DigitalSwitch::Size::compact);
            entry.metrics = sw.get();
            entry.button = std::move (sw);
        }
        else if (toggleSpec.controlStyle.value_or (theme.controlStyle) == ControlStyle::digital)
        {
            auto button = std::make_unique<DigitalToggle> (state, toggleSpec, theme);
            entry.metrics = button.get();
            entry.button = std::move (button);
        }
        else
        {
            auto button = std::make_unique<MiniToggle> (state, toggleSpec, theme);
            entry.metrics = button.get();
            entry.button = std::move (button);
        }

        // A toggle (e.g. tempo sync) can change the unit a knob reads in.
        entry.button->onStateChange = [this]
        {
            for (auto& knob : knobs)
                if (knob != nullptr)
                    knob->refreshValueText();
            if (waveDisplay != nullptr)
                waveDisplay->repaint();
        };

        if (toggleSpec.onClick)
            entry.button->onClick = toggleSpec.onClick;

        addAndMakeVisible (*entry.button);
        toggles.push_back (std::move (entry));
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
        if (theme.controlStyle == ControlStyle::digital)
        {
            auto sw = std::make_unique<DigitalSwitch> (state, *spec.slideToggle, theme);
            slideToggleMetrics = sw.get();
            slideToggle = std::move (sw);
        }
        else
        {
            auto sw = std::make_unique<SlideToggle> (state, *spec.slideToggle, theme);
            slideToggleMetrics = sw.get();
            slideToggle = std::move (sw);
        }

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

    if (spec.presetBar.has_value())
    {
        presetBar = std::make_unique<PresetBar> (*spec.presetBar, theme);
        addAndMakeVisible (*presetBar);
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

    if (spec.grainScope.has_value())
    {
        grainScope = std::make_unique<GrainScope> (state, *spec.grainScope, theme);
        addAndMakeVisible (*grainScope);
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
                const auto a =
                    static_cast<juce::uint8> (juce::jlimit (0.0f, 255.0f, std::abs (n) * theme.grain * 26.0f));
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

bool PedalEditor::Face::hasTopStrip() const
{
    return topSwitch() || presetBar != nullptr;
}

bool PedalEditor::Face::hasBottomBand() const
{
    return waveDisplay != nullptr || filterScope != nullptr || grainScope != nullptr;
}

int PedalEditor::Face::bottomBandHeight() const
{
    if (waveDisplay != nullptr)
        return spec.waveDisplay->height + kWaveDisplayGap;
    if (filterScope != nullptr)
        return spec.filterScope->height + kWaveDisplayGap;
    if (grainScope != nullptr)
        return spec.grainScope->height + kWaveDisplayGap;
    return 0;
}

juce::Rectangle<int> PedalEditor::Face::switchStripArea() const
{
    if (hasTopStrip())
        return contentArea().removeFromTop (kSwitchStripHeight);

    return {};
}

juce::Rectangle<int> PedalEditor::Face::waveDisplayArea() const
{
    if (! hasBottomBand())
        return {};

    auto band = contentArea().removeFromBottom (bottomBandHeight());
    band.translate (0, -spec.displayBandRise);
    return band.withTrimmedBottom (2); // sit low, near the pedal name
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

    // A group box stands off its knobs on every side, and its caption sits
    // half out of the top edge - so the row has to claim that space or the
    // outline draws over whatever is above and below it.
    if (spec.subKnobGroupCaption.isNotEmpty())
        h += kSubGroupPad + kSubGroupCaptionHeight;

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
    if (hasTopStrip())
        area.removeFromTop (kSwitchStripHeight + kSwitchStripGap);
    if (hasBottomBand())
        area.removeFromBottom (bottomBandHeight() + spec.displayBandRise + kSubRowGap);

    return area.removeFromBottom (subRowHeight());
}

juce::Rectangle<int> PedalEditor::Face::knobArea() const
{
    auto area = contentArea();

    if (hasTopStrip())
        area.removeFromTop (kSwitchStripHeight + kSwitchStripGap);

    if (hasBottomBand())
        area.removeFromBottom (bottomBandHeight() + spec.displayBandRise + (subKnobs.empty() ? 0 : kSubRowGap));

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

    const juce::Rectangle<float> gridBounds (area.getX(), travel.getStart(), area.getWidth(), travel.getLength());

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
    struct Band
    {
        float x, hz, db;
    };
    std::vector<Band> bands;
    std::vector<juce::Point<float>> nodes; // fallback when no frequencies are given
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
            bands.push_back ({ node.x, hz, static_cast<float> (faders[i]->getSlider().getValue()) });
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
            const float rIn = juce::jmin (kCornerRadius, (prev - cur).getDistanceFromOrigin() * 0.45f);
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
                const auto& a = bands[0];
                const auto& b = bands[1];
                return a.x + (b.x - a.x) * (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
            }
            for (size_t i = 1; i < bands.size(); ++i)
                if (L <= lg (bands[i].hz))
                {
                    const auto& a = bands[i - 1];
                    const auto& b = bands[i];
                    return a.x + (b.x - a.x) * (L - lg (a.hz)) / (lg (b.hz) - lg (a.hz));
                }
            const auto& a = bands[bands.size() - 2];
            const auto& b = bands.back();
            return b.x + (b.x - a.x) * (L - lg (b.hz)) / (lg (b.hz) - lg (a.hz));
        };

        const auto gainDbAtHz = [&] (float hz)
        {
            const float L = lg (hz);
            if (L <= lg (bands.front().hz))
                return bands.front().db;
            if (L >= lg (bands.back().hz))
                return bands.back().db;
            for (size_t i = 1; i < bands.size(); ++i)
                if (L <= lg (bands[i].hz))
                {
                    const auto& a = bands[i - 1];
                    const auto& b = bands[i];
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
            if (side == CutSide::low && v > static_cast<float> (s.getMinimum()) + 0.5f)
                loHz = v;
            if (side == CutSide::high && v < static_cast<float> (s.getMaximum()) - 0.5f)
                hiHz = v;
        }

        constexpr float kCutSlopeDbPerOct = 14.0f;
        const auto rolloffDb = [&] (float hz)
        {
            float d = 0.0f;
            if (loHz > 0.0f && hz < loHz)
                d -= kCutSlopeDbPerOct * std::log2 (loHz / juce::jmax (hz, 1.0f));
            if (hiHz > 0.0f && hz > hiHz)
                d -= kCutSlopeDbPerOct * std::log2 (hz / hiHz);
            return d;
        };

        const float midY = travel.getStart() + travel.getLength() * 0.5f;
        const float pxPerDb = travel.getLength() / 30.0f; // +/-15 dB across the grid
        const auto yForDb = [&] (float db) { return midY - db * pxPerDb; };
        const auto pointAtHz = [&] (float hz)
        { return juce::Point<float> (xForHz (hz), yForDb (gainDbAtHz (hz) + rolloffDb (hz))); };

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

        std::sort (points.begin(), points.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        points.erase (
            std::unique (points.begin(), points.end(), [] (auto& a, auto& b) { return std::abs (a.x - b.x) < 1.0f; }),
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
                      juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        const float t = juce::jlimit (0.0f, 1.0f, (std::log (juce::jmax (hz, 1.0f)) - logMin) / logSpan);
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
        juce::ColourGradient grad (juce::Colours::black.withAlpha (0.16f), fromLeft ? band.getX() : band.getRight(),
                                   band.getCentreY(), juce::Colours::transparentBlack,
                                   fromLeft ? band.getRight() : band.getX(), band.getCentreY(), false);
        g.setGradientFill (grad);
        g.fillRect (band);
    }
}

void PedalEditor::Face::resetFaders()
{
    const auto toDefault = [] (juce::Slider& s)
    { s.setValue (s.getDoubleClickReturnValue(), juce::sendNotificationSync); };

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

void PedalEditor::Face::layOutKnobGroupsRow (juce::Rectangle<int> area)
{
    const int count = static_cast<int> (knobs.size());
    knobCells.assign (static_cast<size_t> (count), {});
    if (count == 0 || area.isEmpty())
        return;

    // A filled panel stands `kKnobGroupPad` off its knobs on the sides, more
    // above (the caption strip) and below (room for a Sync toggle). Keep that
    // whole envelope inside the content area rather than letting it run onto the
    // frame, and centre the envelope - not just the knob band - in what is left.
    const bool filled = spec.filledKnobGroups;
    const int overhangSide = filled ? kKnobGroupPad : 0;
    const int overhangTop = filled ? (kKnobGroupPad + kKnobGroupCaptionHeight + kKnobGroupPad / 2 + kKnobGroupExtraTop) : 0;
    bool anyHangingToggle = false;
    for (const auto& tg : spec.toggles)
        if (tg.centeredBelow || tg.centeredAbove)
            anyHangingToggle = true;
    const int overhangBottom =
        filled ? (kKnobGroupPad + kKnobGroupExtraBottom + (anyHangingToggle ? kKnobGroupToggleReserve : 0)) : 0;

    area = area.reduced (overhangSide, 0);

    // One block per group that actually takes knobs: `columns` wide, with an odd
    // knob left over leading on a short row of its own at the top.
    struct Block
    {
        int first, n, cols, rows, firstRow;
    };
    std::vector<Block> blocks;
    int consumed = 0;
    for (const auto& group : spec.knobGroups)
    {
        const int n = juce::jlimit (0, juce::jmax (0, count - consumed), group.count);
        if (n <= 0)
            continue;

        const int cols = juce::jmax (1, juce::jmin (n, group.columns > 0 ? group.columns : kKnobGroupColsDefault));
        const int firstRow = (n % cols == 0) ? cols : (n % cols);
        const int rows = 1 + (n - firstRow) / cols;
        blocks.push_back ({ consumed, n, cols, rows, firstRow });
        consumed += n;
    }
    if (blocks.empty())
        return;

    // One cell width serves every block, so a knob is the same size in all of
    // them. Gaps: `kKnobGap` between columns within a block, `kKnobGroupGap`
    // between blocks.
    int totalCols = 0;
    for (const auto& b : blocks)
        totalCols += b.cols;

    const int nBlocks = static_cast<int> (blocks.size());
    const int betweenBlocks = kKnobGroupGap * (nBlocks - 1);
    const int betweenCols = kKnobGap * (totalCols - nBlocks);
    const int maxKnob = spec.knobDiameter > 0 ? spec.knobDiameter : kKnobDiameter;

    // The column is normally the full share of the width, but a card looks far
    // tighter with the columns pulled in to just clear the widest cap (main size
    // or a per-knob override) - no big side gutters.
    int biggestKnob = maxKnob;
    for (const auto& k : spec.knobs)
        biggestKnob = juce::jmax (biggestKnob, k.diameter);

    const int fullCell = juce::jmax (1, (area.getWidth() - betweenBlocks - betweenCols) / juce::jmax (1, totalCols));
    const int cellW = juce::jmin (fullCell, biggestKnob + kKnobCellSidePad * 2);
    const int knobW = juce::jmin (maxKnob, cellW);
    const int rowH = knobW + Knob::labelHeight;
    const int rowGap = spec.knobRowGap > 0 ? spec.knobRowGap : kKnobGap;

    // Vertically centre every block against the tallest, and centre that band in
    // the area when it does not fill it.
    int maxRows = 1;
    for (const auto& b : blocks)
        maxRows = juce::jmax (maxRows, b.rows);
    const int bandH = maxRows * rowH + (maxRows - 1) * rowGap;

    const int envH = overhangTop + bandH + overhangBottom;
    int bandTop = area.getY() + overhangTop;
    if (const int slack = area.getHeight() - envH; slack > 0)
        bandTop += slack / 2;
    if (spec.knobBlockRise > 0)
        bandTop = juce::jmax (contentArea().getY() + overhangTop, bandTop - spec.knobBlockRise);

    // Centre the row of blocks in the area horizontally too.
    int usedW = betweenBlocks;
    for (const auto& b : blocks)
        usedW += b.cols * cellW + (b.cols - 1) * kKnobGap;
    int x = area.getX() + juce::jmax (0, (area.getWidth() - usedW) / 2);

    for (const auto& b : blocks)
    {
        const int blockW = b.cols * cellW + (b.cols - 1) * kKnobGap;

        // Every block starts at the same top, so cards of different knob counts
        // line up along their top edge rather than staircasing.
        int y = bandTop;

        int k = b.first;
        for (int r = 0; r < b.rows; ++r)
        {
            const int inRow = (r == 0) ? b.firstRow : b.cols;
            const int rowW = inRow * cellW + (inRow - 1) * kKnobGap;
            int rx = x + (blockW - rowW) / 2; // a short lead row sits centred over the block

            for (int i = 0; i < inRow; ++i, ++k)
            {
                const juce::Rectangle<int> cell (rx, y, cellW, rowH);
                knobCells[static_cast<size_t> (k)] = cell;

                if (auto& knob = knobs[static_cast<size_t> (k)]; knob != nullptr)
                {
                    // The component spans the whole cell so a long caption has
                    // the full column width to sit in; the cap is driven by the
                    // row height, so it stays `knobW` however wide the cell is.
                    // A per-knob diameter override still grows the cap (and the
                    // component down into the row gap).
                    const int own = spec.knobs[static_cast<size_t> (k)].diameter;
                    const int capH = own > 0 ? juce::jmin (own, cellW) : knobW;
                    const int h = juce::jmax (rowH, capH + Knob::labelHeight);
                    knob->setBounds (cell.getX(), cell.getY(), cell.getWidth(), h);
                }

                rx += cellW + kKnobGap;
            }

            y += rowH + rowGap;
        }

        x += blockW + kKnobGroupGap;
    }
}

juce::Font PedalEditor::Face::nameFont() const
{
    return theme.titleFont (58.0f);
}

juce::Font PedalEditor::Face::fittedNameFont (juce::Rectangle<int> area) const
{
    const auto font = nameFont();

    // The title face is a script whose swashes overhang their advance widths,
    // so measure it with the same margin the beside-logo layout uses.
    const float needed = juce::GlyphArrangement::getStringWidth (font, spec.name) * kTitleSwash;
    const float available = static_cast<float> (area.getWidth());

    if (needed <= available || needed <= 0.0f)
        return font;

    return font.withHeight (font.getHeight() * available / needed);
}

void PedalEditor::Face::paintGroupBox (juce::Graphics& g,
                                       juce::Rectangle<int> boxInt,
                                       const juce::String& captionText,
                                       bool filled,
                                       juce::Colour fill) const
{
    if (boxInt.isEmpty())
        return;

    if (filled)
    {
        // A raised card one level in from the face, lifted by the same soft
        // shadow the whole face casts, with the caption inside its top edge. The
        // fill is the spec's own colour when it set one, otherwise a shade off
        // `panel`; the edge and caption are taken from the fill so they stay
        // legible whatever hue it is.
        const auto box = boxInt.toFloat();
        constexpr float kCorner = 14.0f;

        const bool custom = ! fill.isTransparent();
        const auto panelFill =
            custom ? fill : theme.panel.brighter (theme.controlStyle == ControlStyle::digital ? 0.13f : 0.06f);

        juce::Path card;
        card.addRoundedRectangle (box, kCorner);

        // Soft-UI lift, CSS-`box-shadow` style: a layered set of real gaussian
        // shadows. All the tuning is `kCardShadows` at the top of this file;
        // every card shares it, so they read the same. Empty list -> no shadow.
        if (! kCardShadows.empty())
            melatonin::DropShadow (kCardShadows).render (g, card);

        g.setColour (panelFill);
        g.fillRoundedRectangle (box, kCorner);

        // A crisp 1 px highlight on the top edge only. The clip band is just
        // tall enough to reach the 45 degree point of each top corner arc - half
        // way round the curve - so the highlight fades out mid-corner and never
        // runs down the sides or along the bottom.
        {
            constexpr float kLipWidth = 1.0f;
            const float halfCorner = kCorner * (1.0f - juce::MathConstants<float>::sqrt2 / 2.0f); // r(1 - cos45)
            const int band = juce::jmax (2, juce::roundToInt (halfCorner + kLipWidth));

            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (box.withHeight (static_cast<float> (band)).getSmallestIntegerContainer());
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (box.reduced (kLipWidth * 0.5f), kCorner, kLipWidth);
        }

        g.setColour (custom ? panelFill.contrasting (0.85f) : theme.textSecondary);
        g.setFont (theme.bodyFont (13.0f).boldened());
        g.drawText (captionText.toUpperCase(),
                    box.withTrimmedLeft (14.0f)
                        .withHeight (static_cast<float> (kKnobGroupCaptionHeight))
                        .translated (0.0f, 7.0f),
                    juce::Justification::centredLeft, false);
        return;
    }

    // Stroked as a plain rounded rectangle and then broken open where the
    // lettering goes, which is a good deal harder to get wrong than stitching
    // the outline together out of arcs and three sides.
    const auto box = boxInt.toFloat();
    const auto font = theme.bodyFont (10.0f);
    const juce::String caption = captionText.toUpperCase();
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, caption);

    g.setColour (theme.outline.withAlpha (0.75f));
    g.drawRoundedRectangle (box, 10.0f, 1.0f);

    // Erase the run of the top edge the caption sits on. The face is a flat
    // colour here, so painting over it is indistinguishable from a gap.
    const juce::Rectangle<float> gap (box.getX() + kSubGroupPad, box.getY() - 1.0f, textWidth + 8.0f, 3.0f);
    g.setColour (theme.panel);
    g.fillRect (gap);

    g.setColour (theme.textSecondary);
    g.setFont (font);
    g.drawText (caption, gap.withSizeKeepingCentre (textWidth + 2.0f, static_cast<float> (kSubGroupCaptionHeight)),
                juce::Justification::centred, false);
}

void PedalEditor::Face::paint (juce::Graphics& g)
{
    auto bounds = faceBounds().toFloat();

    if (theme.backgroundImage.isValid() && theme.controlStyle != ControlStyle::digital)
    {
        const float bgPanY = 128.2f; // pixels; + moves the art down
        const float bgZoom = 1.55f;  // > 1 zooms in (crops)

        auto dest = bounds.withSizeKeepingCentre (bounds.getWidth() * bgZoom, bounds.getHeight() * bgZoom)
                        .translated (0.0f, bgPanY);

        // Clip the art to the rounded outline of the frame, so the square
        // corners outside the rounding stay transparent instead of showing it.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path outline;
            outline.addRoundedRectangle (bounds, theme.cornerRadius + kBorderThickness);
            g.reduceClipRegion (outline);
            g.drawImage (theme.backgroundImage, dest, juce::RectanglePlacement::stretchToFit);
        }

        const auto face = bounds.reduced (kFaceInset);

        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path rounded;
            rounded.addRoundedRectangle (face, theme.cornerRadius);
            g.reduceClipRegion (rounded);

            for (int i = 0; i < kShadowDepth; ++i)
            {
                const float fade = 1.0f - static_cast<float> (i) / static_cast<float> (kShadowDepth);
                g.setColour (juce::Colours::black.withAlpha (0.30f * fade * fade));
                g.drawRoundedRectangle (face.reduced (0.5f + static_cast<float> (i)), theme.cornerRadius, 1.6f);
            }
        }

        g.setColour (theme.bezel);
        g.drawRoundedRectangle (bounds.reduced (kBorderThickness * 0.5f), theme.cornerRadius + kBorderThickness * 0.5f,
                                kBorderThickness);
    }
    else if (theme.backgroundImage.isValid())
    {
        g.drawImage (theme.backgroundImage, bounds, juce::RectanglePlacement::stretchToFit);
    }
    else if (theme.controlStyle == ControlStyle::digital)
    {
        // A raised card on a pale page: the soft-UI face has no frame and no
        // recess, only the shadow it casts on the page below it.
        g.fillAll (theme.background);

        const auto face = bounds.reduced (kFaceInset);

        juce::Path card;
        card.addRoundedRectangle (face, theme.cornerRadius);
        juce::DropShadow (theme.softShadow, kShadowDepth, { 0, kShadowDepth / 3 }).drawForPath (g, card);

        g.setColour (theme.panel);
        g.fillRoundedRectangle (face, theme.cornerRadius);

        // The light the card catches along its top edge, and the hairline that
        // keeps it off a page of nearly the same value.
        g.setColour (theme.softHighlight.withAlpha (0.9f));
        g.drawRoundedRectangle (face.reduced (0.5f).translated (0.0f, 0.5f), theme.cornerRadius, 1.0f);
        g.setColour (theme.outline.withAlpha (0.6f));
        g.drawRoundedRectangle (face.reduced (0.5f), theme.cornerRadius, 1.0f);
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
                g.drawRoundedRectangle (face.reduced (0.5f + static_cast<float> (i)), theme.cornerRadius, 1.6f);
            }
        }

        g.setColour (theme.bezel);
        g.drawRoundedRectangle (bounds.reduced (kBorderThickness * 0.5f), theme.cornerRadius + kBorderThickness * 0.5f,
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

    // Box around a named sub-knob group, and around each named main-knob group,
    // with the caption let into the top edge.
    if (! subKnobGroup.isEmpty())
        paintGroupBox (g, subKnobGroup, spec.subKnobGroupCaption);

    for (size_t i = 0; i < knobGroupBoxes.size() && i < spec.knobGroups.size(); ++i)
    {
        const auto& group = spec.knobGroups[i];
        const auto fill = group.fill.value_or (juce::Colour {});
        paintGroupBox (g, knobGroupBoxes[i], group.caption, spec.filledKnobGroups, fill);

        // The group mark: centred at the top of the panel, in the gap between
        // the caption line and the first knob.
        if (group.icon && spec.filledKnobGroups)
        {
            const auto box = knobGroupBoxes[i].toFloat();
            constexpr float kIconSize = 14.0f;
            const juce::Rectangle<float> iconArea (box.getCentreX() - kIconSize * 0.5f,
                                                   box.getY() + static_cast<float> (kKnobGroupCaptionHeight) + 8.0f,
                                                   kIconSize, kIconSize);
            const auto ink = fill.isTransparent() ? theme.textSecondary : fill.contrasting (0.8f);
            group.icon (g, iconArea, ink);
        }
    }

    // A small emblem for the effect, centred in the gap the knobs leave above
    // the name. Drawn before the name so it can never sit over the lettering.
    if (emblemImage.isValid() && spec.titleImageHeight > 0)
    {
        const auto title = titleArea();
        const float h = static_cast<float> (spec.titleImageHeight);
        const float w = h * static_cast<float> (emblemImage.getWidth()) /
                        static_cast<float> (juce::jmax (1, emblemImage.getHeight()));

        const auto slot = juce::Rectangle<float> (w, h).withCentre (
            { static_cast<float> (title.getCentreX()), static_cast<float> (title.getY()) - h * 0.5f });

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
        const float aspect = logoImage.isValid() ? static_cast<float> (logoImage.getWidth()) /
                                                       static_cast<float> (juce::jmax (1, logoImage.getHeight()))
                                                 : 1.0f;

        auto row = logoSlot;
        const auto titleFont = nameFont();
        const int logoW = juce::roundToInt (static_cast<float> (row.getHeight()) * aspect);
        // The title face is a script with swashes that overhang its advance
        // widths, so the measured string gets a margin either side of it.
        const int nameW =
            juce::roundToInt (juce::GlyphArrangement::getStringWidth (titleFont, spec.name) * kTitleSwash + 10.0f);

        // A face that wants the pair off the right margin pulls it back in
        // before the cluster is cut, so both emblem and name move together.
        if (spec.titleRowAlignRight && ! spec.titleRowCentred)
            row.removeFromRight (spec.titleRowRightInset);

        const int clusterW = logoW + kLogoNameGap + nameW;
        auto cluster = spec.titleRowCentred
                           ? row.withSizeKeepingCentre (clusterW, row.getHeight())
                           : (spec.titleRowAlignRight ? row.removeFromRight (clusterW) : row.removeFromLeft (clusterW));

        if (spec.titleRowDrop != 0)
            cluster.translate (0, spec.titleRowDrop);

        logoSlot = cluster.removeFromLeft (logoW);
        cluster.removeFromLeft (kLogoNameGap);
        nameArea = cluster;
    }

    g.setColour (theme.title);
    g.setFont (fittedNameFont (nameArea));
    g.drawText (spec.name, nameArea,
                spec.titleBesideLogo ? juce::Justification::centredLeft : juce::Justification::centred, false);

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
        g.drawText (spec.version, faceBounds().reduced (kMargin),
                    spec.titleBesideLogo ? juce::Justification::bottomRight : juce::Justification::bottomLeft, false);
    }
}

void PedalEditor::Face::resized()
{
    if (sidePanel != nullptr)
        sidePanel->setBounds (getLocalBounds().withTrimmedLeft (spec.width));

    auto area = knobArea();

    const int count = static_cast<int> (knobs.size());

    // Named groups laid out side by side get their own path; everything else is
    // the plain grid (one row per group, or the flat knobsPerRow block).
    const bool horizontalGroups = spec.knobGroupsHorizontal && ! spec.knobGroups.empty() && count > 0;
    int perRow = 1;

    if (horizontalGroups)
    {
        layOutKnobGroupsRow (area);
    }
    else
    {
        // How many knobs on each row. One row per named group (plus a trailing
        // row for anything past the last group); otherwise the knobsPerRow grid.
        std::vector<int> rowCounts;
        if (! spec.knobGroups.empty())
        {
            int consumed = 0;
            for (const auto& group : spec.knobGroups)
            {
                const int n = juce::jlimit (0, juce::jmax (0, count - consumed), group.count);
                if (n > 0)
                    rowCounts.push_back (n);
                consumed += n;
            }
            if (consumed < count)
                rowCounts.push_back (count - consumed);
        }
        else
        {
            const int per = juce::jlimit (1, juce::jmax (1, count), spec.knobsPerRow);
            for (int f = 0; f < count; f += per)
                rowCounts.push_back (juce::jmin (per, count - f));
        }

        // The widest row sets the column grid; narrower rows centre within it.
        for (const int n : rowCounts)
            perRow = juce::jmax (perRow, n);

        // Columns span the full content width; the rotary sits centred in its
        // column at a fixed size.
        const int cellWidth = (area.getWidth() - kKnobGap * (perRow - 1)) / perRow;
        const int maxKnob = spec.knobDiameter > 0 ? spec.knobDiameter : kKnobDiameter;
        const int knobWidth = juce::jmin (maxKnob, cellWidth);
        const int rowHeight = knobWidth + Knob::labelHeight;

        // A face with something between its rows can ask for them to be spread;
        // the block stays centred in its area, so the extra lifts the top row
        // and drops the bottom one by half each.
        const int rowGap = spec.knobRowGap > 0 ? spec.knobRowGap : kKnobGap;

        // Centre the knob block when it does not fill the area - a single row
        // with a switch strip above and a preview band below would otherwise sit
        // high. Only done on the new layout, so the other pedals are untouched.
        if (count > 0 && (hasBottomBand() || slideToggle != nullptr))
        {
            const int rows = static_cast<int> (rowCounts.size());
            const int blockHeight = rows * rowHeight + (rows - 1) * rowGap;
            const int slack = area.getHeight() - blockHeight;
            if (slack > 0)
                area.removeFromTop (slack / 2);
        }

        // Lift the block clear of where the area starts. Applied after the
        // centring so it still bites once the rows already fill their area -
        // which is when a face that wants them higher has nothing left to take.
        if (spec.knobBlockRise > 0)
        {
            const int headroom = juce::jmax (0, area.getY() - contentArea().getY());
            area.translate (0, -juce::jmin (spec.knobBlockRise, headroom));
        }

        knobCells.assign (static_cast<size_t> (count), {});

        int first = 0;
        for (const int inRow : rowCounts)
        {
            auto knobRow = area.removeFromTop (rowHeight);
            area.removeFromTop (rowGap);

            // A row that does not fill the column grid is centred, so a short
            // group (or a lone last knob) sits under the gaps of the row above
            // rather than hanging off the left. Full rows keep their maths.
            if (inRow < perRow)
            {
                const int usedWidth = inRow * cellWidth + (inRow - 1) * kKnobGap;
                knobRow.removeFromLeft ((knobRow.getWidth() - usedWidth) / 2);
            }

            for (int i = 0; i < inRow; ++i)
            {
                auto cell = knobRow.removeFromLeft (cellWidth);

                // Kept for every slot, spacers included, so a toggle can be
                // anchored to an empty cell.
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

            first += inRow;
        }
    }

    // Box around each named knob group, taken from the cells the group's knobs
    // ended up in. A filled panel stands further off its knobs, carries a taller
    // caption strip, and - if any group hangs a Sync toggle under a knob in its
    // last row - every panel reserves the same slack below, so they all read as
    // one height however the toggles fall.
    knobGroupBoxes.clear();
    {
        // Knobs on a group's last row: the short lead row is at the top, so the
        // last row is always `cols` wide unless the whole group is one row.
        const auto lastRowCols = [] (int n, int columns)
        {
            const int cols = juce::jmax (1, columns > 0 ? columns : kKnobGroupColsDefault);
            return juce::jmin (n, cols);
        };

        int bottomReserve = 0;
        if (spec.filledKnobGroups)
        {
            int groupFirst = 0;
            for (const auto& group : spec.knobGroups)
            {
                const int n = juce::jlimit (0, juce::jmax (0, count - groupFirst), group.count);
                if (n <= 0)
                    continue;
                const int cols = lastRowCols (n, group.columns);
                for (const auto& tg : spec.toggles)
                    if (tg.centeredBelow && tg.afterKnobIndex >= groupFirst + n - cols &&
                        tg.afterKnobIndex < groupFirst + n)
                        bottomReserve = kKnobGroupToggleReserve;
                groupFirst += n;
            }
        }

        int groupFirst = 0;
        for (const auto& group : spec.knobGroups)
        {
            const int n = juce::jlimit (0, juce::jmax (0, count - groupFirst), group.count);
            if (n <= 0)
                continue;

            juce::Rectangle<int> block;
            for (int i = 0; i < n; ++i)
            {
                const auto& cell = knobCells[static_cast<size_t> (groupFirst + i)];
                block = block.isEmpty() ? cell : block.getUnion (cell);
            }

            if (spec.filledKnobGroups)
                knobGroupBoxes.push_back (block.expanded (kKnobGroupPad)
                                              .withTrimmedTop (-(kKnobGroupCaptionHeight + kKnobGroupPad / 2 + kKnobGroupExtraTop))
                                              .withTrimmedBottom (-(bottomReserve + kKnobGroupExtraBottom)));
            else
                knobGroupBoxes.push_back (
                    block.expanded (kSubGroupPad, kSubGroupPad / 2).withTrimmedTop (-kSubGroupCaptionHeight / 2));

            groupFirst += n;
        }
    }

    // Rule down the gap between two columns, as tall as the whole block.
    knobDivider = {};
    if (const int after = spec.knobDividerAfterColumn; after > 0 && after < perRow && count > after)
    {
        auto block = knobCells.front();
        for (const auto& cell : knobCells)
            block = block.getUnion (cell);

        const int left = knobCells[static_cast<size_t> (after) - 1].getRight();
        const int right = knobCells[static_cast<size_t> (after)].getX();

        knobDivider = juce::Rectangle<int> ((left + right) / 2, block.getY() + 4, 1, block.getHeight() - 8);
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

            const auto capBounds = knobs[i]->getBounds().withTrimmedBottom (knobs[i]->getLabelHeight());
            caps = caps.isEmpty() ? capBounds : caps.getUnion (capBounds);
        }

        // Corner-knob size unless the spec asks for its own, and whichever label
        // block that knob actually draws.
        const int diameter = spec.centreKnob->diameter > 0 ? spec.centreKnob->diameter : kCornerKnobDiameter;
        const int labelHeight = centreKnob->getLabelHeight();

        auto bounds = juce::Rectangle<int> (diameter, diameter + labelHeight).withCentre (caps.getCentre());

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
        const int cellW = juce::jmin (row.getWidth() / juce::jmax (1, n), juce::jmax (kSubKnobDiameter + 26, 84));
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
                button->setBounds (
                    juce::Rectangle<int> (MiniToggle::preferredWidth, MiniToggle::preferredHeight)
                        .withCentre ({ cell.getCentreX(), cell.getY() + MiniToggle::preferredHeight / 2 }));
            }
        }

        // The box, taken from what the knobs ended up occupying rather than
        // from the cell maths, so it stays right whatever the row does.
        subKnobGroup = {};
        if (spec.subKnobGroupCaption.isNotEmpty())
        {
            auto block = subKnobs.front()->getBounds();
            for (const auto& k : subKnobs)
                block = block.getUnion (k->getBounds());

            subKnobGroup = block.expanded (kSubGroupPad, kSubGroupPad / 2).withTrimmedTop (-kSubGroupCaptionHeight / 2);
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
        const int compactDia = spec.compactKnobDiameter > 0 ? spec.compactKnobDiameter : kCornerKnobDiameter;
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
                groupTrimDivider =
                    juce::Rectangle<int> ((groupsRight + knobsLeft) / 2, strip.getY() + 2, 1, compactDia - 4);
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
        faderResetButton->setBounds (juce::Rectangle<int> (kResetButtonWidth, kResetButtonHeight)
                                         .withPosition (title.getX(), title.getCentreY() - kResetButtonHeight / 2));
    }

    // A toggle either straddles the gap after `afterKnobIndex` or sits centred
    // above that knob's cap, depending on the spec.
    for (size_t t = 0; t < toggles.size(); ++t)
    {
        const auto& tSpec = spec.toggles[t];
        const int index = tSpec.afterKnobIndex;
        auto& button = *toggles[t].button;
        const int toggleW = toggles[t].metrics->switchWidth();
        const int toggleH = toggles[t].metrics->switchHeight();

        // Pinned to the top-right corner of a knob-group panel, clear of the
        // caption let into its top-left.
        if (const int gp = tSpec.groupPanelIndex; gp >= 0)
        {
            if (gp >= static_cast<int> (knobGroupBoxes.size()))
            {
                button.setVisible (false);
                continue;
            }

            const auto box = knobGroupBoxes[static_cast<size_t> (gp)];
            constexpr int inset = 12;
            juce::Point<int> tr { box.getRight() - inset - toggleW / 2, box.getY() + inset + toggleH / 2 };
            tr.x -= toggles[t].metrics->switchTrackOffset();

            button.setVisible (true);
            button.setBounds (juce::Rectangle<int> (toggleW, toggleH).withCentre (tr));
            continue;
        }

        const bool spacerAnchor = index >= 0 && index < count && knobs[static_cast<size_t> (index)] == nullptr;

        const bool haveAnchor =
            (tSpec.centeredAbove || tSpec.centeredBelow || tSpec.centeredRight || spacerAnchor)
                ? (index >= 0 && index < count)
                : (index >= 0 && index + 1 < count && knobs[static_cast<size_t> (index)] != nullptr &&
                   knobs[static_cast<size_t> (index + 1)] != nullptr);

        if (! haveAnchor || index >= static_cast<int> (knobCells.size()))
        {
            button.setVisible (false);
            continue;
        }

        const auto cell = knobCells[static_cast<size_t> (index)];
        juce::Point<int> centre;

        if (spacerAnchor)
        {
            // An empty column: the button takes the middle of it, level with the
            // caps either side.
            centre = { cell.getCentreX(), cell.getY() + (cell.getHeight() - Knob::labelHeight) / 2 };
        }
        else if (tSpec.centeredBelow)
        {
            // Hung just under the knob's own printed label - not under its
            // cell, which may carry an empty row the label never uses.
            const auto& anchor = *knobs[static_cast<size_t> (index)];
            centre = { anchor.getBounds().getCentreX(), anchor.printedTextBottom() + toggleH / 2 + tSpec.belowGap };
        }
        else if (tSpec.centeredAbove)
        {
            // Just above the cap: the cap fills the knob bounds minus the label
            // block at the bottom.
            const auto anchor = knobs[static_cast<size_t> (index)]->getBounds();
            centre = { anchor.getCentreX(), anchor.getY() - toggleH / 2 - 5 };
        }
        else if (tSpec.centeredRight)
        {
            // Beside the cap, vertically centred on it - the cap is the square
            // at the top of the knob bounds, `Knob::labelHeight` shy of the
            // bottom.
            const auto anchor = knobs[static_cast<size_t> (index)]->getBounds();
            const int capSize = anchor.getHeight() - Knob::labelHeight;
            centre = { anchor.getCentreX() + capSize / 2 + toggleW / 2 + 3, anchor.getY() + capSize / 2 };
        }
        else
        {
            const auto anchor = knobs[static_cast<size_t> (index)]->getBounds();
            const auto right = knobs[static_cast<size_t> (index + 1)]->getBounds();
            if (right.getY() != anchor.getY())
            {
                button.setVisible (false);
                continue;
            }
            centre = { (anchor.getRight() + right.getX()) / 2,
                       anchor.getY() + (anchor.getHeight() - Knob::labelHeight) / 2 - tSpec.gapRise };
        }

        // Labels of unequal width sit the track off the component's centre;
        // shifting by that much puts the track itself on the anchor.
        centre.x -= toggles[t].metrics->switchTrackOffset();

        button.setVisible (true);
        button.setBounds (juce::Rectangle<int> (toggleW, toggleH).withCentre (centre));
    }

    // Sliding switch: top-left of its own strip, or - in bottom mode -
    // left-aligned on the pedal-name row, level with the title. A face whose
    // switch is the only thing in its strip can centre it instead.
    if (slideToggle != nullptr)
    {
        // On a face whose name has moved onto the logo row, "bottom" means that
        // row: the switch takes the left of it, opposite the name.
        const auto strip = ! bottomSwitch() ? switchStripArea() : spec.titleBesideLogo ? logoArea() : titleArea();
        const int switchW = slideToggleMetrics->switchWidth();
        const int switchH = slideToggleMetrics->switchHeight();

        const int x = spec.slideToggleCentred
                          ? strip.getCentreX() - switchW / 2 - slideToggleMetrics->switchTrackOffset()
                          : strip.getX() - slideToggleMetrics->switchLabelInset();

        slideToggle->setBounds (juce::Rectangle<int> (switchW, switchH)
                                    .withPosition (x, strip.getCentreY() - switchH / 2 - spec.slideToggleRise));
    }

    // Preset bar: centred in the top strip. The slide switch, if any, keeps its
    // top-left corner of the same strip - the bar is narrow enough to clear it.
    if (presetBar != nullptr)
    {
        const auto strip = switchStripArea();
        const int w = juce::jmin (spec.presetBar->width, strip.getWidth());
        presetBar->setBounds (
            juce::Rectangle<int> (w, strip.getHeight()).withCentre ({ strip.getCentreX(), strip.getCentreY() }));
    }

    // A small knob hard against the top-right, cap centred on the switch strip;
    // its one text line hangs into the gap below the strip.
    if (topRightKnob != nullptr)
    {
        const int d = juce::jmax (20, spec.topRightKnobDiameter);
        const int x = contentArea().getRight() - d;
        const int y = switchStripArea().getCentreY() - d / 2;
        topRightKnob->setBounds (x, y, d, d + Knob::labelHeight);
    }

    if (waveDisplay != nullptr)
        waveDisplay->setBounds (waveDisplayArea());

    if (filterScope != nullptr)
        filterScope->setBounds (waveDisplayArea());

    if (grainScope != nullptr)
        grainScope->setBounds (waveDisplayArea());
}

//==============================================================================
PedalEditor::PedalEditor (juce::AudioProcessor& processor,
                          juce::AudioProcessorValueTreeState& state,
                          PedalSpec specToUse,
                          PedalTheme themeToUse)
    : juce::AudioProcessorEditor (processor), theme (std::move (themeToUse)), lookAndFeel (theme)
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
    setSize (juce::roundToInt (baseWidth * kDefaultZoom), juce::roundToInt (baseHeight * kDefaultZoom));
}

PedalEditor::~PedalEditor()
{
    setLookAndFeel (nullptr);
}

void PedalEditor::applyResizeLimits()
{
    setResizeLimits (juce::roundToInt (baseWidth * kMinZoom), juce::roundToInt (baseHeight * kMinZoom),
                     juce::roundToInt (baseWidth * kMaxZoom), juce::roundToInt (baseHeight * kMaxZoom));

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double)baseWidth / (double)baseHeight);
}

void PedalEditor::setSidePanel (std::unique_ptr<juce::Component> panel, int panelWidth)
{
    face->setSidePanel (std::move (panel), panelWidth);

    baseWidth = face->getLogicalWidth();
    baseHeight = face->getLogicalHeight();

    applyResizeLimits();
    setSize (juce::roundToInt (baseWidth * kDefaultZoom), juce::roundToInt (baseHeight * kDefaultZoom));
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
