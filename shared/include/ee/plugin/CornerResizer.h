#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace ee::plugin
{

/** The native half of the bottom-left resize grip every resizable WebView face
    shows. The grip itself is drawn by the page (`installResizableFace` in
    `@synthpeak/pedal-ui/juce`); this is what it drives.

    It used to be a juce::Component laid over the corner, and nobody could ever
    see it or grab it: the WebView is a native view (WKWebView, WebView2), and a
    native view sits on top of everything JUCE paints into the same window, so
    a JUCE component "in front of" it is behind it on screen. The page is the
    only thing that can draw over the page.

    So the page owns the pointer and reports the drag here as a distance from
    where it started, in screen pixels - which stays true while the window
    changes size under the pointer, where page coordinates would not. This
    applies it through the ComponentBoundsConstrainer every editor already sets
    up for its zoom range and fixed aspect ratio, so a drag can never leave
    them.

    Bottom-left, and dragging it left or down makes the face bigger - but only
    the size changes, never the editor's position. A plugin cannot move its
    host's window, and an editor pushed off (0, 0) inside it is clipped by some
    hosts' wrappers, so the window grows from its top-left like any other
    plugin window does and the grip reads the drag as "this much bigger".

    Wiring, in an editor: a member declared before the WebView -

        ee::plugin::CornerResizer resizeGrip { *this };

    - and its options passed through `resizeGrip.bridge (...)`. Nothing to lay
    out in `resized()`. It does nothing until the editor has been made
    resizable, which the faces do on their first size report. */
class CornerResizer
{
public:
    explicit CornerResizer (juce::AudioProcessorEditor& editorToResize) : editor (editorToResize) {}

    /** `options` plus the three native functions the page's grip calls. */
    juce::WebBrowserComponent::Options bridge (juce::WebBrowserComponent::Options options)
    {
        using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;

        return options
            .withNativeFunction ("cornerResizeStart",
                                 [this] (const juce::Array<juce::var>&, Completion complete)
                                 {
                                     start();
                                     complete (true);
                                 })
            .withNativeFunction ("cornerResizeDrag",
                                 [this] (const juce::Array<juce::var>& args, Completion complete)
                                 {
                                     if (args.size() >= 2)
                                         drag (juce::roundToInt (static_cast<double> (args[0])),
                                               juce::roundToInt (static_cast<double> (args[1])));
                                     complete (true);
                                 })
            .withNativeFunction ("cornerResizeEnd",
                                 [this] (const juce::Array<juce::var>&, Completion complete)
                                 {
                                     end();
                                     complete (true);
                                 });
    }

private:
    void start()
    {
        auto* constrainer = editor.getConstrainer();
        if (constrainer == nullptr || ! editor.isResizable())
            return;

        startBounds = editor.getBounds();
        dragging = true;
        constrainer->resizeStart();
    }

    /** `dx`, `dy`: how far the pointer has moved since the drag began. Left
        (negative dx) widens, down (positive dy) deepens - the corner is being
        pulled away from the top-right. */
    void drag (int dx, int dy)
    {
        auto* constrainer = editor.getConstrainer();
        if (! dragging || constrainer == nullptr)
            return;

        const juce::Rectangle<int> proposed (startBounds.getX(), startBounds.getY(), startBounds.getWidth() - dx,
                                             startBounds.getHeight() + dy);

        // Constrained as a bottom-right stretch, because that is what the
        // window actually does - see the class note.
        constrainer->setBoundsForComponent (&editor, proposed, false, false, true, true);
    }

    void end()
    {
        if (! dragging)
            return;

        dragging = false;

        if (auto* constrainer = editor.getConstrainer())
            constrainer->resizeEnd();
    }

    juce::AudioProcessorEditor& editor;
    juce::Rectangle<int> startBounds;
    bool dragging = false;
};

} // namespace ee::plugin
