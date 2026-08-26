#include "ee/ui/PedalTheme.h"

namespace ee::ui
{

juce::Font PedalTheme::titleFont (float height) const
{
    auto options = juce::FontOptions().withHeight (height);
    if (titleTypeface.isNotEmpty())
        options = options.withName (titleTypeface);

    return juce::Font (options);
}

juce::Font PedalTheme::bodyFont (float height) const
{
    auto options = juce::FontOptions().withHeight (height);
    if (bodyTypeface.isNotEmpty())
        options = options.withName (bodyTypeface);

    return juce::Font (options);
}

juce::String PedalTheme::pickTypeface (const juce::StringArray& preferred)
{
    const auto installed = juce::Font::findAllTypefaceNames();

    for (const auto& name : preferred)
        if (installed.contains (name))
            return name;

    return {};
}

PedalTheme PedalTheme::dark()
{
    return {};
}

PedalTheme PedalTheme::cream()
{
    PedalTheme t;

    t.background      = juce::Colour (0xff2a2a28);
    t.panel           = juce::Colour (0xfff1eee7);
    t.outline         = juce::Colour (0xff54606b);
    t.textPrimary     = juce::Colour (0xff23201d);
    t.textSecondary   = juce::Colour (0xff3a352f);

    t.knobBody        = juce::Colour (0xff1b1a19);
    t.knobOutline     = juce::Colour (0xffbdb6a9);
    t.knobPointer     = juce::Colour (0xfff6f3ec);
    t.knobTrack       = juce::Colour (0xffcdc6b9);

    t.accent          = juce::Colour (0xff8f1f1f);
    t.accentDim       = juce::Colour (0xff8a736e);
    t.accentGlow      = juce::Colour (0xffff2a1a);

    t.title           = juce::Colour (0xff141210);
    t.ledRing         = juce::Colour (0xff262220);
    t.grain           = 0.55f;

    t.titleTypeface   = pickTypeface ({ "Permanent Marker", "Marker Felt",
                                        "Bradley Hand", "Chalkboard SE" });

    t.ledOn           = juce::Colour (0xffff3a24);
    t.ledOff          = juce::Colour (0xff8d7a75);
    t.switchBody      = juce::Colour (0xffa8a49c);
    t.switchHighlight = juce::Colour (0xffe2ded4);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;

    return t;
}

} // namespace ee::ui
