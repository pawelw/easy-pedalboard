#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** The soft-UI rotary: a white cap sunk into a charcoal ring, with a small dark
    pointer on its face and a scale of ticks around the outside that darkens up
    to the value.

    Painter only - it carries no state and owns no component. `PedalLookAndFeel`
    calls it in place of the photographic cap whenever the theme's
    `ControlStyle` is `digital`, so a knob keeps the same slider, the same
    attachment and the same label block whichever style it is drawn in.

    There is no value arc: the tick ring is the scale, which is what keeps the
    face flat.
*/
class DigitalKnob
{
public:
    /** The two sizes the style comes in. `small` is not just `large` scaled
        down - it carries a coarser scale and a proportionally heavier ring, so
        a secondary control still reads at half the diameter. */
    enum class Size
    {
        large,
        small
    };

    /** Marks the top of the travel - see `KnobSpec::endMarker`. */
    struct EndMarker
    {
        juce::Colour colour;
        juce::String label;      // printed just outside the tick; empty for none
        bool present = false;
    };

    /** Draws the cap centred in `bounds`, at the largest size that fits.

        `sliderPos` is 0..1 along the travel and decides how much of the tick
        ring is lit; `startAngle` / `endAngle` are the slider's own rotary
        limits, in radians clockwise from 12 o'clock.
    */
    static void draw (juce::Graphics& g,
                      juce::Rectangle<float> bounds,
                      float sliderPos,
                      float startAngle,
                      float endAngle,
                      Size size,
                      const PedalTheme& theme,
                      bool enabled,
                      const EndMarker& endMarker,
                      juce::Colour capFillOverride = {});

    /** The clear circle on the cap face, inside the pointer's orbit - where a
        glyph standing for the setting can be drawn without the pointer running
        over it. Same `bounds` the matching `draw` call is given. */
    static juce::Rectangle<float> faceArea (juce::Rectangle<float> bounds, Size size);

    /** Which size a cap of this diameter is drawn at. Keeps the cut-off in one
        place, so a caller that has to match the drawing (laying a switch out
        under a knob, say) does not have to guess. */
    static Size sizeForDiameter (int diameter) noexcept
    {
        return diameter >= kSmallSizeLimit ? Size::large : Size::small;
    }

    /** Caps at or above this diameter are drawn `large`. */
    static constexpr int kSmallSizeLimit = 60;

    DigitalKnob() = delete;
};

} // namespace ee::ui
