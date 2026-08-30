#include "ee/ui/Knob.h"

namespace ee::ui
{
namespace
{
    constexpr float kValueRowHeight = 18.0f;
    constexpr float kCaptionRowHeight = 14.0f;
    static_assert (Knob::labelHeight == static_cast<int> (kValueRowHeight + kCaptionRowHeight));
}

Knob::Knob (juce::AudioProcessorValueTreeState& state,
            const KnobSpec& spec,
            const PedalTheme& theme)
    : apvts (state), paramID (spec.parameterID), captionText (spec.caption),
      pedalTheme (theme), compact (spec.compact), compactCaption (spec.compactCaption),
      liveValueText (spec.liveValueText)
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

    // The wet/dry control shows a small spoon instead of the plain position dot.
    // Keyed off the parameter ID so every pedal with a "mix" knob picks it up
    // without touching its spec.
    if (paramID == "mix")
        slider.getProperties().set ("spoonPointer", true);

    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

    slider.onValueChange = [this]
    {
        refreshValueText();
        if (onValueChanged)
            onValueChanged();
    };
    refreshValueText();
}

Knob::~Knob() = default;

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

        g.setColour (pedalTheme.textPrimary);
        g.setFont (pedalTheme.bodyFont (12.0f));
        g.drawText (compactCaption ? captionText.toUpperCase() : valueText,
                    textArea, juce::Justification::centred, false);
        return;
    }

    const auto captionArea = area.removeFromBottom (kCaptionRowHeight);
    const auto valueArea = area.removeFromBottom (kValueRowHeight);

    g.setColour (pedalTheme.textPrimary);
    g.setFont (pedalTheme.bodyFont (16.0f));
    g.drawText (valueText, valueArea, juce::Justification::centredTop, false);

    g.setColour (pedalTheme.textSecondary);
    g.setFont (pedalTheme.bodyFont (12.0f).boldened().withExtraKerningFactor (0.09f));
    g.drawText (captionText.toUpperCase(), captionArea, juce::Justification::centredTop, false);
}

} // namespace ee::ui
