#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ee/ui/PedalSpec.h"
#include "ee/ui/PedalTheme.h"

#include <memory>

namespace ee::ui
{

/** The preset strip that sits centred in the switch strip: a list button that
    drops the preset menu, a save button that offers "Save" / "Save as New", the
    current preset name in the middle, and prev / next arrows either side of the
    right edge. All the behaviour is the host processor's - this only draws the
    chrome and calls the `PresetBarSpec` hooks. */
class PresetBar : public juce::Component
{
public:
    PresetBar (PresetBarSpec spec, const PedalTheme& theme);
    ~PresetBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Re-read the current name from the spec and repaint. Call after the
        processor has changed preset behind the bar's back (host automation, a
        state restore). */
    void refresh();

private:
    void showListMenu();
    void showSaveMenu();

    class GlyphButton;

    PresetBarSpec spec;
    PedalTheme theme;

    std::unique_ptr<GlyphButton> listButton;
    std::unique_ptr<GlyphButton> saveButton;
    std::unique_ptr<GlyphButton> prevButton;
    std::unique_ptr<GlyphButton> nextButton;

    juce::String name;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace ee::ui
