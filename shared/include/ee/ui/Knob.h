#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Rotary control plus the value readout and caption underneath it. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state,
          const KnobSpec& spec,
          const PedalTheme& theme);

    ~Knob() override;

    /** Height of the value and caption rows below the rotary. */
    static constexpr int labelHeight = 32;

    /** Same, compact: value readout only, no caption. */
    static constexpr int compactLabelHeight = 16;

    /** The top row of the label block - the value readout (the shorter of the
        two rows now, set in a smaller font than the caption below it), and the
        only row a caption-until-touched knob prints in. */
    static constexpr int valueRowHeight = 14;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** The cap glyph goes on top of the slider, which is a child - so it is
        painted here rather than in `paint`, which would put it underneath. */
    void paintOverChildren (juce::Graphics&) override;

    juce::Slider& getSlider() noexcept { return slider; }

    /** Re-reads the value text (from `liveValueText` if the spec set one, else
        the parameter) and repaints if it changed. Call this when something the
        `liveValueText` closure depends on has moved. */
    void refreshValueText();

    int getLabelHeight() const noexcept { return compact ? compactLabelHeight : labelHeight; }

    /** Bottom of the text this knob actually prints, in its parent's
        coordinates - lower than its own bounds' bottom whenever it leaves a
        label row empty. What to hang a button off. */
    int printedTextBottom() const noexcept
    {
        return getBottom() - (captionUntilTouched && ! compact ? labelHeight - valueRowHeight : 0);
    }

    /** Called after the value changes, once the readout has refreshed. Lets a
        parent react (e.g. repaint artwork that depends on the value). */
    std::function<void()> onValueChanged;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramID;
    juce::String captionText;
    const PedalTheme& pedalTheme;

    /** This knob's own style - the theme's unless the spec overrode it. */
    ControlStyle capStyle = ControlStyle::analog;

    bool compact = false;
    bool compactCaption = false;
    bool captionUntilTouched = false;
    bool touched = false;          // the knob is being dragged: show the reading

    std::function<juce::String()> liveValueText;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> valueIcon;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> capIcon;

    /** A slider that pulls onto the middle of its range while being dragged.
        JUCE routes every mouse-driven value through snapValue(), and nothing
        else, so automation and typed entry are unaffected. */
    struct DetentSlider : juce::Slider
    {
        double snapValue (double attemptedValue, DragMode dragMode) override
        {
            if (! detent || dragMode == notDragging)
                return attemptedValue;

            const double centre = (getMinimum() + getMaximum()) * 0.5;
            const double window = (getMaximum() - getMinimum()) * kDetentFraction;

            return std::abs (attemptedValue - centre) < window ? centre : attemptedValue;
        }

        /** Half-width of the pull, as a fraction of the whole range. */
        static constexpr double kDetentFraction = 0.02;

        bool detent = false;
    };

    DetentSlider slider;
    juce::String valueText;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace ee::ui
