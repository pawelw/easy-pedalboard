#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

    PedalTheme theme;
    PedalSpec spec;
    PedalLookAndFeel lookAndFeel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<MiniToggle>> toggles;
    std::unique_ptr<juce::Component> sidePanel;
    juce::Image grain;
    juce::Image logoImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalEditor)
};

} // namespace ee::ui
