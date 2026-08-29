#include "ee/ui/FaderStrip.h"

namespace ee::ui
{
namespace
{
    constexpr float kValueRowHeight = 18.0f;
    constexpr float kCaptionRowHeight = 14.0f;
    static_assert (FaderStrip::labelHeight == static_cast<int> (kValueRowHeight + kCaptionRowHeight));

    // How near 0 (in the slider's own units - dB here) a drag has to land
    // before the detent takes it.
    constexpr double kDetentRadius = 0.75;
}

double FaderStrip::DetentSlider::snapValue (double attemptedValue, DragMode dragMode)
{
    if (dragMode != juce::Slider::notDragging && std::abs (attemptedValue) < kDetentRadius)
        return 0.0;

    return attemptedValue;
}

juce::Range<float> faderTrackRange (juce::Rectangle<float> sliderBounds) noexcept
{
    // Node clearance, plus a proportional inset so the extremes stop short of
    // the grid edge - a visual margin only, the parameter range is unchanged.
    const float pad = kFaderNodeRadius + 4.0f + sliderBounds.getHeight() * 0.06f;
    return { sliderBounds.getY() + pad, sliderBounds.getBottom() - pad };
}

FaderStrip::FaderStrip (juce::AudioProcessorValueTreeState& state,
                        const SliderSpec& spec,
                        const PedalTheme& theme)
    : apvts (state), paramID (spec.parameterID), captionText (spec.caption), pedalTheme (theme)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // Grab the node where it is and drag from there. A click never lands dead
    // centre, so snapping to the mouse would jump the value on touch.
    slider.setSliderSnapsToMousePosition (false);

    // The joining curve has no colour slot of its own, so the fill colour rides
    // on the track slot.
    if (spec.fill.has_value())
        slider.setColour (juce::Slider::trackColourId, *spec.fill);

    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

    // Snap back to the parameter's own default (0 dB on a graphic EQ band).
    slider.setDoubleClickReturnValue (true, slider.getDoubleClickReturnValue());

    slider.onValueChange = [this]
    {
        refreshValueText();

        // The joining curve lives on the parent, so it has to repaint too.
        if (auto* parent = getParentComponent())
            parent->repaint();
    };
    refreshValueText();
}

FaderStrip::~FaderStrip() = default;

juce::Point<float> FaderStrip::nodeCentreInParent()
{
    const auto sb = slider.getBounds().toFloat();
    const auto range = faderTrackRange (sb);
    const float prop = static_cast<float> (slider.valueToProportionOfLength (slider.getValue()));

    // prop 0 = bottom of travel, 1 = top.
    const float y = juce::jmap (prop, 0.0f, 1.0f, range.getEnd(), range.getStart());

    return { sb.getCentreX() + static_cast<float> (getX()),
             y + static_cast<float> (getY()) };
}

void FaderStrip::refreshValueText()
{
    juce::String next;

    if (auto* param = apvts.getParameter (paramID))
        next = param->getCurrentValueAsText();

    if (next != valueText)
    {
        valueText = next;
        repaint();
    }
}

void FaderStrip::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (labelHeight);
    slider.setBounds (area);
}

void FaderStrip::paint (juce::Graphics& g)
{
    // Pull the label block up towards the faders rather than leaving it against
    // the bottom edge of the strip.
    constexpr float kLabelRise = 10.0f;

    auto area = getLocalBounds().toFloat()
                    .removeFromBottom (static_cast<float> (labelHeight))
                    .translated (0.0f, -kLabelRise);
    const auto captionArea = area.removeFromBottom (kCaptionRowHeight);
    const auto valueArea = area.removeFromBottom (kValueRowHeight);

    // Non-zero values pick up the curve's green; 0 stays neutral.
    g.setColour (valueText == "0" ? pedalTheme.textPrimary : kFaderCurveColour);
    g.setFont (pedalTheme.bodyFont (13.0f));
    g.drawText (valueText, valueArea, juce::Justification::centredTop, false);

    g.setColour (pedalTheme.textSecondary);
    g.setFont (pedalTheme.bodyFont (11.0f).boldened().withExtraKerningFactor (0.09f));

    // Printed as given - the fader captions carry their own casing (e.g. the
    // "3.2k" frequency labels) rather than being forced upper case.
    g.drawText (captionText, captionArea, juce::Justification::centredTop, false);
}

} // namespace ee::ui
