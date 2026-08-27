#include "ee/ui/PedalTheme.h"

#include "BinaryData.h"

namespace ee::ui
{
namespace
{
    juce::Typeface::Ptr birthstone()
    {
        static juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (
            BinaryData::BirthstoneRegular_ttf, BinaryData::BirthstoneRegular_ttfSize);
        return face;
    }
}

juce::Font PedalTheme::titleFont (float height) const
{
    if (titleTypefacePtr != nullptr)
        return juce::Font (juce::FontOptions (titleTypefacePtr).withHeight (height));

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

PedalTheme PedalTheme::blue()
{
    PedalTheme t;

    t.background      = juce::Colour (0xff121417);
    t.panel           = juce::Colour (0xff1f9fde);
    t.outline         = juce::Colour (0xff0d1014);
    t.bezel           = juce::Colour (0xff87ceef);

    t.textPrimary     = juce::Colours::white;
    t.textSecondary   = juce::Colours::white;
    t.title           = juce::Colours::white;

    t.knobBody        = juce::Colour (0xff111214);
    t.knobOutline     = juce::Colour (0xff05070a);
    t.knobPointer     = juce::Colours::white;
    t.knobTrack       = juce::Colour (0xffbfd0dc);

    t.accent          = juce::Colour (0xff12719f);
    t.accentDim       = juce::Colour (0xff4c6472);
    t.glow            = juce::Colour (0xff5cc8ff);

    t.ledOn           = juce::Colour (0xff12719f);
    t.ledOff          = juce::Colour (0xff1d3a4a);
    t.ledRing         = juce::Colour (0xff0e1418);
    t.switchBody      = juce::Colour (0xff23282d);
    t.switchHighlight = juce::Colour (0xff353c44);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::silver()
{
    PedalTheme t;

    t.background      = juce::Colour (0xff17181a);
    t.panel           = juce::Colour (0xffc9ced4);
    t.outline         = juce::Colour (0xff6d747c);
    t.bezel           = juce::Colour (0xffeaeef2);

    t.textPrimary     = juce::Colour (0xff101214);
    t.textSecondary   = juce::Colour (0xff23272c);
    t.title           = juce::Colour (0xff0c0e10);

    t.knobBody        = juce::Colour (0xff0e1012);
    t.knobFill        = juce::Colour (0xff1a1d20);
    t.knobOutline     = juce::Colour (0xff05070a);
    t.knobPointer     = juce::Colour (0xfff4f6f8);
    t.knobTrack       = juce::Colour (0xfff0f3f6);

    // Dark arc on a bright track, the inverse of the blue face but the same
    // amount of contrast.
    t.accent          = juce::Colour (0xff15181b);
    t.accentDim       = juce::Colour (0xff868d95);
    t.glow            = juce::Colour (0xff2d333a);

    t.ledOn           = juce::Colour (0xff1b1e22);
    t.ledOff          = juce::Colour (0xff9aa2ab);
    t.ledRing         = juce::Colour (0xff70777f);
    t.switchBody      = juce::Colour (0xff8b929a);
    t.switchHighlight = juce::Colour (0xffe4e8ec);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.45f;   // brushed-metal speckle

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
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
