#pragma once

namespace ee::ui
{

/** What the editor needs from a latching switch to place it, whichever style it
    is drawn in.

    `SlideToggle`, `DigitalSwitch` and `MiniToggle` are all `juce::Button`s of
    quite different shapes, and the layout code picks between them from the
    theme. This is the little the layout actually asks of one, so it does not
    have to know which it is holding.
*/
class SwitchControl
{
public:
    virtual ~SwitchControl() = default;

    /** The size the switch wants. It depends on the labels, so it cannot be a
        constant on the class. */
    virtual int switchWidth() const = 0;
    virtual int switchHeight() const = 0;

    /** Blank space between the component's left edge and the first letter of
        its resting label - what a caller shifts the switch left by to line that
        letter up with something below it. 0 unless the switch was asked to sit
        flush left. */
    virtual int switchLabelInset() const { return 0; }

    /** How far the track's centre sits right of the component's own centre.
        Labels of unequal width push the track off centre, so a caller placing
        the switch under a knob subtracts this to line the *track* up with the
        knob rather than the block of text around it. */
    virtual int switchTrackOffset() const { return 0; }
};

} // namespace ee::ui
