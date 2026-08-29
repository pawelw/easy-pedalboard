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

private:
    juce::Rectangle<int> faceBounds() const;
    juce::Rectangle<int> knobArea() const;
    juce::Rectangle<int> titleArea() const;
    juce::Rectangle<int> logoArea() const;

    /** Lays a single row of faders across the given area. */
    void layOutFaders (juce::Rectangle<int> area);

    /** Slider region spanned by the fader row, once laid out. Empty when the
        pedal has no faders. */
    juce::Rectangle<int> faderArea() const;

    /** Faint grid plus the curve joining the fader nodes. */
    void paintFaderGraph (juce::Graphics&) const;

    /** Translucent shading over the grid for whatever the corner cut knobs are
        removing from each end of the spectrum. */
    void paintCutMasks (juce::Graphics&, juce::Rectangle<float> grid) const;

    /** Returns every fader to its parameter default. */
    void resetFaders();

    PedalTheme theme;
    PedalSpec spec;
    PedalLookAndFeel lookAndFeel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Knob>> cornerKnobs;
    std::vector<std::unique_ptr<FaderStrip>> faders;
    std::vector<std::unique_ptr<MiniToggle>> toggles;
    std::unique_ptr<juce::TextButton> faderResetButton;
    std::unique_ptr<juce::Component> sidePanel;
    juce::Image grain;
    juce::Image logoImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalEditor)
};

} // namespace ee::ui
