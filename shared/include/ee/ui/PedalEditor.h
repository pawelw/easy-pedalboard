#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/FaderStrip.h"
#include "ee/ui/Knob.h"
#include "ee/ui/MiniToggle.h"
#include "ee/ui/PedalLookAndFeel.h"
#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

/** Generic pedal editor driven entirely by a PedalSpec.

    Every effect in this repo uses this class directly; none of them need their
    own editor subclass.

    The pedal face is drawn at its design size by an inner component and scaled
    to fill the window, so the whole thing zooms when the corner grip (or the
    host) resizes it. Aspect ratio is locked.
*/
class PedalEditor : public juce::AudioProcessorEditor
{
public:
    PedalEditor (juce::AudioProcessor& processor,
                 juce::AudioProcessorValueTreeState& state,
                 PedalSpec spec,
                 PedalTheme theme = PedalTheme::dark());

    ~PedalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Attaches a panel to the right of the pedal and widens the window to fit
        it. Used by the development tuning tools; the face is unaffected. */
    void setSidePanel (std::unique_ptr<juce::Component> panel, int panelWidth);

    /** How far the face can be scaled from its design size, and where it opens. */
    static constexpr float kMinZoom = 0.6f;
    static constexpr float kMaxZoom = 2.0f;
    static constexpr float kDefaultZoom = 0.85f;

private:
    /** The pedal face, drawn once at design size and then scaled by the editor. */
    class Face;

    void applyResizeLimits();

    PedalTheme theme;
    PedalLookAndFeel lookAndFeel;

    std::unique_ptr<Face> face;
    std::unique_ptr<juce::ResizableCornerComponent> resizeGrip;

    int baseWidth = 0;
    int baseHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalEditor)
};

} // namespace ee::ui
