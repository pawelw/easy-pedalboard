#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ee::plugin
{

/** The corner grip every resizable WebView face shows, bottom-left rather
    than JUCE's own bottom-right default - it reads as part of the face
    itself, in the corner nearest the header's own brand mark, rather than a
    DAW-chrome afterthought bolted onto whichever corner the framework
    happens to default to.

    juce::ResizableCornerComponent has no bottom-left mode to ask for: it is
    built around growing the component from a fixed top-left, i.e. dragging
    moves the bottom-right point and nothing else does. This is that same
    idea mirrored - dragging moves the *left* edge and the *bottom* edge,
    while the top and right stay where they are, so the top-right corner is
    what holds still under a drag (the same feel a bottom-right grip gives,
    just the other way round). It otherwise leans on the same
    ComponentBoundsConstrainer every editor already sets up for its aspect
    ratio and zoom range, via the same setBoundsForComponent() JUCE's own
    corner resizer calls - only the isStretchingLeft/isStretchingBottom flags
    differ. */
class CornerResizer : public juce::Component
{
public:
    static constexpr int kSize = 18;

    CornerResizer (juce::Component& componentToResize, juce::ComponentBoundsConstrainer& boundsConstrainer)
        : target (componentToResize), constrainer (boundsConstrainer)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::BottomLeftCornerResizeCursor);
    }

    void paint (juce::Graphics& g) override
    {
        // Two parallel strokes at 45deg, the ordinary two-line "drag to
        // resize" glyph - "/" rather than the bottom-right grip's stock "\",
        // since this corner's diagonal runs the other way. Brighter while
        // the pointer is over it or dragging, the same on/hover language
        // PowerToggle-style chrome elsewhere in these faces uses.
        const auto b = getLocalBounds().toFloat();
        const float alpha = isMouseOverOrDragging() ? 0.85f : 0.45f;
        g.setColour (juce::Colours::white.withAlpha (alpha));

        const float thickness = 1.6f;
        const float margin = 3.0f;
        // Long stroke corner-to-corner, short one set in a few pixels toward
        // the centre - the same two-stroke spacing every OS's own resize
        // glyph uses, just drawn by hand rather than reached for a bitmap.
        g.drawLine (margin, b.getBottom() - margin, b.getRight() - margin, margin, thickness);
        g.drawLine (margin, b.getBottom() - margin * 3.2f, b.getRight() - margin * 3.2f, margin, thickness);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        originalBounds = target.getBounds();
        constrainer.resizeStart();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Mirrored ResizableCornerComponent::mouseDrag: the left edge follows
        // the drag (x, and width the opposite way) while the top and right
        // stay put, and the bottom edge follows it exactly as it would for a
        // bottom-right grip.
        const int dx = e.getDistanceFromDragStartX();
        const int dy = e.getDistanceFromDragStartY();

        const juce::Rectangle<int> proposed (originalBounds.getX() + dx,
                                              originalBounds.getY(),
                                              originalBounds.getWidth() - dx,
                                              originalBounds.getHeight() + dy);

        constrainer.setBoundsForComponent (&target, proposed, false, true, true, false);
    }

    void mouseUp (const juce::MouseEvent&) override { constrainer.resizeEnd(); }

private:
    juce::Component& target;
    juce::ComponentBoundsConstrainer& constrainer;
    juce::Rectangle<int> originalBounds;
};

} // namespace ee::plugin
