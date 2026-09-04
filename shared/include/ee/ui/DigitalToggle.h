#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"
#include "ee/ui/SwitchControl.h"

namespace ee::ui
{

/** The soft-UI latching button: a rounded-square outline carrying a glyph or a
    short caption.

    State is carried by weight, not by colour. On, the outline and its contents
    are the face's own ink; off, both fall back to the same pale grey the unlit
    ticks use, so the button reads as "not doing anything" rather than as a
    second colour competing with the display.

    The digital counterpart of `MiniToggle`; both satisfy `SwitchControl`, so
    the editor lays either out without knowing which it built.
*/
class DigitalToggle : public juce::Button,
                      public SwitchControl
{
public:
    DigitalToggle (juce::AudioProcessorValueTreeState& state,
                   const ToggleSpec& spec,
                   const PedalTheme& theme);

    ~DigitalToggle() override;

    /** A glyph gets a square; a caption gets a wider box to print in. The glyph
        square defaults to 25 but a spec can override it (`ToggleSpec::iconSize`). */
    static constexpr int defaultIconSize = 25;
    static constexpr int captionWidth = 52;
    static constexpr int captionHeight = 28;

    int switchWidth() const override;
    int switchHeight() const override;

protected:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const PedalTheme& pedalTheme;
    juce::String captionText;
    juce::Colour onColour;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;
    int iconSize = defaultIconSize;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitalToggle)
};

} // namespace ee::ui
