#include "ee/ui/Knob.h"

#include "ee/ui/DigitalKnob.h"

namespace ee::ui
{
namespace
{
    // The value rides on top in the shorter row; the caption takes the taller
    // row below it and the larger font.
    constexpr float kValueRowHeight = static_cast<float> (Knob::valueRowHeight);
    constexpr float kCaptionRowHeight = static_cast<float> (Knob::labelHeight) - kValueRowHeight;
    static_assert (Knob::labelHeight == static_cast<int> (kValueRowHeight + kCaptionRowHeight));

    constexpr float kValueFontHeight = 12.0f;
    constexpr float kCaptionFontHeight = 14.0f;
}

Knob::Knob (juce::AudioProcessorValueTreeState& state,
            const KnobSpec& spec,
            const PedalTheme& theme)
    : apvts (state), paramID (spec.parameterID), captionText (spec.caption),
      pedalTheme (theme), capStyle (spec.capStyle.value_or (theme.controlStyle)),
      compact (spec.compact), compactCaption (spec.compactCaption),
      captionUntilTouched (spec.captionUntilTouched),
      liveValueText (spec.liveValueText), valueIcon (spec.valueIcon), capIcon (spec.capIcon)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f,
                                true);
    slider.setDoubleClickReturnValue (true, slider.getDoubleClickReturnValue());

    if (spec.capFill.has_value())
        slider.setColour (juce::Slider::rotarySliderFillColourId, *spec.capFill);

    if (spec.capBorder.has_value())
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, *spec.capBorder);

    // No dedicated slot for a rotary's value arc, so the thumb colour carries it.
    if (spec.arc.has_value())
        slider.setColour (juce::Slider::thumbColourId, *spec.arc);

    if (spec.invertedArc)
        slider.getProperties().set ("invertedArc", true);

    if (spec.bipolarArc)
        slider.getProperties().set ("bipolarArc", true);

    slider.detent = spec.centreDetent;

    // Small utility knobs (corner cuts, the centre "reso") keep the vector cap;
    // only the full-size knobs get the photographic artwork.
    if (compact)
        slider.getProperties().set ("compactKnob", true);

    // A face can hold one cap back in the other style - see KnobSpec::capStyle.
    if (spec.capStyle.has_value())
        slider.getProperties().set ("digitalCap", *spec.capStyle == ControlStyle::digital);

    if (spec.endMarker.has_value())
    {
        slider.getProperties().set ("endMarker", static_cast<int> (spec.endMarker->getARGB()));
        slider.getProperties().set ("endMarkerLabel", spec.endMarkerLabel);
    }

    // The wet/dry control shows a small spoon instead of the plain position dot.
    // Keyed off the parameter ID so every pedal with a "mix" knob picks it up
    // without touching its spec.
    if (paramID == "mix")
        slider.getProperties().set ("spoonPointer", true);

    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

    // On a one-row knob the caption gives way to the reading only while the
    // knob is actually being turned.
    if (captionUntilTouched)
    {
        slider.onDragStart = [this] { touched = true;  repaint(); };
        slider.onDragEnd   = [this] { touched = false; repaint(); };
    }

    slider.onValueChange = [this]
    {
        refreshValueText();
        if (valueIcon || capIcon)
            repaint();               // the glyph tracks the value, not the text
        if (onValueChanged)
            onValueChanged();
    };
    refreshValueText();
}

Knob::~Knob() = default;

void Knob::paintOverChildren (juce::Graphics& g)
{
    // Only the digital cap leaves a clear face to draw on; the analog one is
    // artwork all the way across.
    if (! capIcon || capStyle != ControlStyle::digital)
        return;

    const auto capBounds = slider.getBounds().toFloat().reduced (2.0f);
    const auto size = compact ? DigitalKnob::Size::small
                              : DigitalKnob::sizeForDiameter (juce::roundToInt (
                                    juce::jmin (capBounds.getWidth(), capBounds.getHeight())));

    // Well short of the pointer's ink: the glyph is a hint at what the knob
    // does, and at full strength it reads as the knob's main event instead.
    const auto ink = pedalTheme.knobPointer.interpolatedWith (pedalTheme.knobFill, 0.45f);

    capIcon (g, DigitalKnob::faceArea (capBounds, size), ink);
}

void Knob::refreshValueText()
{
    juce::String next;

    if (liveValueText)
        next = liveValueText();
    else if (auto* param = apvts.getParameter (paramID))
        next = param->getCurrentValueAsText();

    if (next != valueText)
    {
        valueText = next;
        repaint();
    }
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (getLabelHeight());
    slider.setBounds (area);
}

void Knob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    if (compact)
    {
        const auto textArea = area.removeFromBottom (static_cast<float> (compactLabelHeight));

        if (valueIcon && ! compactCaption)
        {
            valueIcon (g, textArea, pedalTheme.textPrimary);
            return;
        }

        g.setColour (pedalTheme.textPrimary);
        g.setFont (pedalTheme.bodyFont (12.0f));
        g.drawText (compactCaption ? captionText.toUpperCase() : valueText,
                    textArea, juce::Justification::centred, false);
        return;
    }

    const auto captionArea = area.removeFromBottom (kCaptionRowHeight);
    const auto valueArea = area.removeFromBottom (kValueRowHeight);

    // A caption-until-touched knob keeps its label block the same height as its
    // neighbours', so the caps still line up, but prints on the top row only:
    // the caption up close under the cap, swapped for the reading while the
    // knob is being turned.
    const auto textArea = captionUntilTouched ? valueArea : captionArea;

    if (! captionUntilTouched || touched)
    {
        if (valueIcon)
        {
            valueIcon (g, valueArea, pedalTheme.textPrimary);
        }
        else
        {
            g.setColour (pedalTheme.textPrimary);
            g.setFont (pedalTheme.bodyFont (kValueFontHeight));
            g.drawText (valueText, valueArea, juce::Justification::centredTop, false);
        }

        if (captionUntilTouched)
            return;
    }

    // The caption now carries the larger font, so a long one ("FEEDBACK") can
    // outrun a narrow knob column - shrink it to fit rather than clip it.
    const juce::String caption = captionText.toUpperCase();
    auto captionFont = pedalTheme.bodyFont (kCaptionFontHeight).boldened().withExtraKerningFactor (0.09f);
    const float captionWidth = juce::GlyphArrangement::getStringWidth (captionFont, caption);
    const float captionRoom = static_cast<float> (textArea.getWidth());
    if (captionWidth > captionRoom && captionWidth > 0.0f)
        captionFont = captionFont.withHeight (juce::jmax (11.0f, captionFont.getHeight() * captionRoom / captionWidth));

    g.setColour (pedalTheme.textSecondary);
    g.setFont (captionFont);
    g.drawText (caption, textArea, juce::Justification::centredTop, false);
}

} // namespace ee::ui
