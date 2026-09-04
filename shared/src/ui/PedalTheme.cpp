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

    /** Lato Light, compiled in. Every face sets all of its lettering in this
        now - captions, readouts and the pedal name alike - so the per-theme
        `titleTypeface*` / `bodyTypeface` fields are no longer read. */
    juce::Typeface::Ptr lato()
    {
        static juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::LatoLight_ttf, BinaryData::LatoLight_ttfSize);
        return face;
    }
}

juce::Font PedalTheme::titleFont (float height) const
{
    // The pedal name keeps its own face (the Birthstone script for most themes,
    // a marker face for one); only the body text moved to Lato Light.
    if (titleTypefacePtr != nullptr)
        return juce::Font (juce::FontOptions (titleTypefacePtr).withHeight (height));

    auto options = juce::FontOptions().withHeight (height);
    if (titleTypeface.isNotEmpty())
        options = options.withName (titleTypeface);

    return juce::Font (options);
}

juce::Font PedalTheme::bodyFont (float height) const
{
    return juce::Font (juce::FontOptions (lato()).withHeight (height));
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

PedalTheme PedalTheme::teal()
{
    PedalTheme t;

    // BOSS-tremolo teal face, cream legend.
    t.background      = juce::Colour (0xff10201f);
    t.panel           = juce::Colour (0xff2d8a8e);
    t.outline         = juce::Colour (0xff0b1a19);
    t.bezel           = juce::Colour (0xff8fd0d2);

    t.textPrimary     = juce::Colour (0xfffee1b8);
    t.textSecondary   = juce::Colour (0xfffee1b8);
    t.title           = juce::Colour (0xfffee1b8);

    // Black caps with a cream pointer.
    t.knobBody        = juce::Colour (0xff0e1211);
    t.knobFill        = juce::Colour (0xff161b1a);
    t.knobOutline     = juce::Colour (0xff05080a);
    t.knobPointer     = juce::Colour (0xfffee1b8);

    // Pale teal groove with a deep teal value arc - the arc fills over the
    // groove as the knob turns up, the same polarity as every other face
    // (sky/yellow/blue). Bright enough to read against the black cap, dark
    // enough to read against the teal panel.
    t.knobTrack       = juce::Colour (0xffd2e8e4);
    t.accent          = juce::Colour (0xff123f3d);
    t.accentDim       = juce::Colour (0xff5f8480);
    t.glow            = juce::Colour (0xff6fd6da);

    t.ledOn           = juce::Colour (0xff1e6d70);
    t.ledOff          = juce::Colour (0xff1f4746);
    t.ledRing         = juce::Colour (0xff0b1a19);
    t.switchBody      = juce::Colour (0xff17403f);
    t.switchHighlight = juce::Colour (0xff2f6d6c);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::gold()
{
    // Peak Delay's silver face restruck in light ocean blue.
    PedalTheme t = silver();

    t.panel           = juce::Colour (0xffa4caa8);
    t.outline         = juce::Colour (0xff4f6b52);
    t.bezel           = juce::Colour (0xffcfe9d2);   // lighter tint of the panel green

    t.knobTrack       = juce::Colour (0xfff4ead0);   // warm off-white groove
    t.accentDim       = juce::Colour (0xff9a8a55);
    t.glow            = juce::Colour (0xff3a3320);

    t.ledOff          = juce::Colour (0xffb59a4a);
    t.ledRing         = juce::Colour (0xff5a4a12);
    t.switchBody      = juce::Colour (0xffa8892f);
    t.switchHighlight = juce::Colour (0xffe6cf86);

    return t;
}

PedalTheme PedalTheme::sky()
{
    PedalTheme t;

    // Pale sky-cyan face, near-black legend and black caps - the same
    // high-contrast recipe as teal()/silver(), struck in cyan.
    t.background      = juce::Colour (0xff10181b);
    t.panel           = juce::Colour (0xff8bcbdb);
    t.outline         = juce::Colour (0xff2f4750);
    t.bezel           = juce::Colour (0xffb8e2ec);   // lighter tint of the panel

    t.textPrimary     = juce::Colour (0xff0e1a1e);
    t.textSecondary   = juce::Colour (0xff0e1a1e);
    t.title           = juce::Colour (0xff081319);

    // Black caps with a pale pointer.
    t.knobBody        = juce::Colour (0xff101416);
    t.knobFill        = juce::Colour (0xff181d1f);
    t.knobOutline     = juce::Colour (0xff05080a);
    t.knobPointer     = juce::Colour (0xffeef7fa);

    // Pale cyan groove with a deep teal value arc - bright enough to read
    // against the black cap, dark enough to read against the cyan face.
    t.knobTrack       = juce::Colour (0xffd6eef4);
    t.accent          = juce::Colour (0xff123844);
    t.accentDim       = juce::Colour (0xff4f7883);
    t.glow            = juce::Colour (0xff2b4a52);

    t.ledOn           = juce::Colour (0xff2b8fa5);
    t.ledOff          = juce::Colour (0xff5a8b96);
    t.ledRing         = juce::Colour (0xff0e2a30);
    t.switchBody      = juce::Colour (0xff5f8b96);
    t.switchHighlight = juce::Colour (0xffcdeaf1);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::yellow()
{
    PedalTheme t;

    // Boss OD-style yellow face: a warm amber panel, near-black legend and black
    // caps - the same high-contrast recipe as teal()/sky(), struck in yellow.
    t.background      = juce::Colour (0xff1a1710);
    t.panel           = juce::Colour (0xffe8b400);
    t.outline         = juce::Colour (0xff5a4a12);
    t.bezel           = juce::Colour (0xfff2cf58);   // lighter tint of the panel

    t.textPrimary     = juce::Colour (0xff231a05);
    t.textSecondary   = juce::Colour (0xff231a05);
    t.title           = juce::Colour (0xff1a1305);

    // Black caps with a warm off-white pointer.
    t.knobBody        = juce::Colour (0xff100d08);
    t.knobFill        = juce::Colour (0xff181410);
    t.knobOutline     = juce::Colour (0xff05060a);
    t.knobPointer     = juce::Colour (0xfff7efdc);

    // Warm off-white groove with a deep brown-amber value arc - bright enough to
    // read against the black cap, dark enough to read against the yellow face.
    t.knobTrack       = juce::Colour (0xfff2e4b0);
    t.accent          = juce::Colour (0xff6a3d0a);
    t.accentDim       = juce::Colour (0xff9a7a3a);
    t.glow            = juce::Colour (0xff8a5a1a);

    t.ledOn           = juce::Colour (0xffc06a1a);
    t.ledOff          = juce::Colour (0xff8a6a2a);
    t.ledRing         = juce::Colour (0xff2a1f0a);
    t.switchBody      = juce::Colour (0xff7a5a1a);
    t.switchHighlight = juce::Colour (0xffe6c060);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::orange()
{
    PedalTheme t;

    // Burnt-orange face, near-black legend and black caps - the same
    // high-contrast recipe as yellow()/teal()/sky(), struck in orange.
    t.background      = juce::Colour (0xff1a0f0a);
    t.panel           = juce::Colour (0xffe45d27);
    t.outline         = juce::Colour (0xff5a2410);
    t.bezel           = juce::Colour (0xfff08a5e);   // lighter tint of the panel

    t.textPrimary     = juce::Colour (0xff2a1206);
    t.textSecondary   = juce::Colour (0xff2a1206);
    t.title           = juce::Colour (0xff1f0d04);

    // Black caps with a warm off-white pointer.
    t.knobBody        = juce::Colour (0xff120b08);
    t.knobFill        = juce::Colour (0xff1a120d);
    t.knobOutline     = juce::Colour (0xff05060a);
    t.knobPointer     = juce::Colour (0xfff7ecdc);

    // Warm off-white groove with a deep red-brown value arc - bright enough to
    // read against the black cap, dark enough to read against the orange face.
    t.knobTrack       = juce::Colour (0xfff4ddc8);
    t.accent          = juce::Colour (0xff5e2109);
    t.accentDim       = juce::Colour (0xffa8613f);
    t.glow            = juce::Colour (0xff9a3f18);

    t.ledOn           = juce::Colour (0xffff7a3a);
    t.ledOff          = juce::Colour (0xffa85a3a);
    t.ledRing         = juce::Colour (0xff2a1206);
    t.switchBody      = juce::Colour (0xff7a3a1a);
    t.switchHighlight = juce::Colour (0xfff0a878);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::pink()
{
    PedalTheme t;

    // Light-pink (#ffb6c1) face, near-black legend and black caps - the same
    // high-contrast recipe as orange()/yellow()/teal()/sky(), struck in pink.
    t.background      = juce::Colour (0xff1a1013);
    t.panel           = juce::Colour (0xffffb6c1);
    t.outline         = juce::Colour (0xff7a4a55);
    t.bezel           = juce::Colour (0xffffd3da);   // lighter tint of the panel

    t.textPrimary     = juce::Colour (0xff2a1016);
    t.textSecondary   = juce::Colour (0xff2a1016);
    t.title           = juce::Colour (0xff200b11);

    // Black caps with a warm off-white pointer.
    t.knobBody        = juce::Colour (0xff130a0c);
    t.knobFill        = juce::Colour (0xff1b0f12);
    t.knobOutline     = juce::Colour (0xff05060a);
    t.knobPointer     = juce::Colour (0xfffbe6ea);

    // Pink-tinted off-white groove with a deep wine value arc - bright enough to
    // read against the black cap, dark enough to read against the pale-pink face.
    t.knobTrack       = juce::Colour (0xfff8e2e6);
    t.accent          = juce::Colour (0xff8a1f47);
    t.accentDim       = juce::Colour (0xffc06b88);
    t.glow            = juce::Colour (0xffd94f86);

    t.ledOn           = juce::Colour (0xffff4f97);
    t.ledOff          = juce::Colour (0xffbf6f88);
    t.ledRing         = juce::Colour (0xff2a1016);
    t.switchBody      = juce::Colour (0xff8a3a5a);
    t.switchHighlight = juce::Colour (0xfff6bacd);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::green()
{
    PedalTheme t;

    // Peak Tape's face, struck in the deep green of Peak Delay's Tape cap - the
    // one knob on the board that is already a tape machine. The only dark-panel
    // face in the range, which is the point: it reads as the machine rather than
    // as another pedal.
    t.background      = juce::Colour (0xff0d1206);
    t.panel           = juce::Colour (0xff3f6419);
    t.outline         = juce::Colour (0xff17280b);   // the Tape cap's own border
    t.bezel           = juce::Colour (0xff8fbf5a);   // lighter tint of the panel

    // Light legend, the inverse of the pale faces - a deep panel cannot carry a
    // near-black one.
    t.textPrimary     = juce::Colour (0xfff2f7e6);
    t.textSecondary   = juce::Colour (0xffd3e3b4);
    t.title           = juce::Colour (0xfff6faec);

    // Black caps with a pale pointer, the same recipe as every other face.
    t.knobBody        = juce::Colour (0xff11150c);
    t.knobFill        = juce::Colour (0xff1a2012);
    t.knobOutline     = juce::Colour (0xff05080a);
    t.knobPointer     = juce::Colour (0xfff2f7e6);

    // Dark olive groove with a bright lime value arc. Polarity is flipped from
    // the pale faces on purpose: on a deep panel the arc has to be the light
    // half of the pair to read at a glance.
    t.knobTrack       = juce::Colour (0xff26330f);
    t.accent          = juce::Colour (0xffc8e06a);
    t.accentDim       = juce::Colour (0xff6c8543);
    t.glow            = juce::Colour (0xffd8f088);

    t.ledOn           = juce::Colour (0xffc8e06a);
    t.ledOff          = juce::Colour (0xff2c3a1b);
    t.ledRing         = juce::Colour (0xff0b1206);
    t.switchBody      = juce::Colour (0xff2a3a17);
    t.switchHighlight = juce::Colour (0xff6f8f43);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::charcoal()
{
    PedalTheme t;

    // Plain charcoal box, white legend. No hue anywhere on the face - the one
    // pedal in the range that is a piece of studio equipment rather than a
    // colour, which suits a spring tank: the thing itself is a steel box.
    t.background      = juce::Colour (0xff141414);
    t.panel           = juce::Colour (0xff323232);
    t.outline         = juce::Colour (0xff1c1c1c);
    t.bezel           = juce::Colour (0xff5e5e5e);   // lighter tint of the panel

    t.textPrimary     = juce::Colours::white;
    t.textSecondary   = juce::Colours::white;
    t.title           = juce::Colours::white;

    // Black caps with a white pointer, the same recipe as every other face.
    t.knobBody        = juce::Colour (0xff0f0f0f);
    t.knobFill        = juce::Colour (0xff1a1a1a);
    t.knobOutline     = juce::Colour (0xff050505);
    t.knobPointer     = juce::Colours::white;

    // Dark groove with a near-white value arc. Polarity follows green() rather
    // than the pale faces: on a deep panel the arc has to be the light half of
    // the pair to read at a glance.
    t.knobTrack       = juce::Colour (0xff4a4a4a);
    t.accent          = juce::Colour (0xfff0f0f0);
    t.accentDim       = juce::Colour (0xff8c8c8c);
    t.glow            = juce::Colour (0xffffffff);

    t.ledOn           = juce::Colour (0xfff0f0f0);
    t.ledOff          = juce::Colour (0xff3c3c3c);
    t.ledRing         = juce::Colour (0xff101010);
    t.switchBody      = juce::Colour (0xff2a2a2a);
    t.switchHighlight = juce::Colour (0xff6a6a6a);

    t.cornerRadius = 16.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.35f;

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

PedalTheme PedalTheme::white()
{
    PedalTheme t;

    // The soft-UI face: one flat off-white card on a pale page, every control
    // shaped by a light from the top-left rather than by colour. Nothing here
    // carries a hue except the display trace, so the trace is the only thing
    // on the face that draws the eye.
    t.controlStyle = ControlStyle::digital;

    t.background      = juce::Colour (0xffe9eaee);   // the page behind the card
    t.panel           = juce::Colour (0xfffbfbfc);   // the card itself
    t.outline         = juce::Colour (0xffd8dade);
    t.bezel           = juce::Colour (0xfff4f5f7);   // barely there: the card has a shadow instead

    t.textPrimary     = juce::Colour (0xff2c2e33);
    t.textSecondary   = juce::Colour (0xff3a3d43);
    t.title           = juce::Colour (0xff2c2e33);

    // A white cap inside a charcoal ring, with the scale of ticks around it in
    // the same charcoal - `knobTrack` is what an unreached tick fades to.
    t.knobBody        = juce::Colour (0xff2c2e33);   // the ring
    t.knobFill        = juce::Colour (0xffffffff);   // the cap face
    t.knobOutline     = juce::Colour (0xffd2d4d9);
    t.knobPointer     = juce::Colour (0xff2c2e33);
    t.knobTrack       = juce::Colour (0xffc3c6cd);

    t.accent          = juce::Colour (0xff2c2e33);
    t.accentDim       = juce::Colour (0xffb9bcc3);
    t.glow            = juce::Colour (0xffc2562f);   // the one warm colour: the display trace

    t.softShadow      = juce::Colour (0x2a2b3040);
    t.softHighlight   = juce::Colour (0xffffffff);
    t.recess          = juce::Colour (0xffdcdee3);
    t.recessInk       = juce::Colour (0xff8b8f98);

    t.ledOn           = juce::Colour (0xff2c2e33);
    t.ledOff          = juce::Colour (0xffd7d9de);
    t.ledRing         = juce::Colour (0xffd7d9de);
    t.switchBody      = juce::Colour (0xffd0d3da);   // track, off
    t.switchHighlight = juce::Colour (0xffffffff);   // the sliding knob

    t.cornerRadius = 20.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.0f;

    t.logoTint = t.title;

    // The lettering is shared with every other pedal - the same script for the
    // name, the same body face for the captions. Only the controls change.
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::moss()
{
    PedalTheme t;

    // The soft-UI face struck in Peak Delay's green rather than in off-white:
    // the same flat card, the same light from the top-left, but every value
    // shifted onto the green the pedal already had. The caps are a pale green
    // too - light enough against the card to read as raised, never white.
    t.controlStyle = ControlStyle::digital;

    t.background      = juce::Colour (0xff97b39b);   // the page behind the card
    t.panel           = juce::Colour (0xffb2cfb5);   // the card - Peak Delay's own green
    t.outline         = juce::Colour (0xff8aa88d);
    t.bezel           = juce::Colour (0xffcfe4d1);

    t.textPrimary     = juce::Colour (0xff1d2e20);
    t.textSecondary   = juce::Colour (0xff2a3d2c);
    t.title           = juce::Colour (0xff17251a);

    // A green cap in a deep-green ring, and the scale of ticks around it fading
    // to `knobTrack` where the value has not reached.
    //
    // The cap is a gradient from `softHighlight` at its top to `knobFill` at its
    // bottom, so BOTH have to carry the green - a near-white highlight washes
    // the top half out however green the fill is, and the cap reads white on a
    // green face. Keep the pair lighter than `panel` all the way down: the drop
    // shadow does the rest of the lifting.
    t.knobBody        = juce::Colour (0xff23361f);   // the ring
    t.knobFill        = juce::Colour (0xffcce6c2);   // the cap face, at its darkest
    t.knobOutline     = juce::Colour (0xff8aa88d);
    t.knobPointer     = juce::Colour (0xff23361f);
    t.knobTrack       = juce::Colour (0xff86a189);

    t.accent          = juce::Colour (0xff23361f);
    t.accentDim       = juce::Colour (0xff9ab39d);

    // The reached ticks, and anything else on the face that carries a reading.
    // A deeper green than the ring, so it lifts off the card without bringing a
    // second hue onto it.
    t.glow            = juce::Colour (0xff2c5e3a);

    t.softShadow      = juce::Colour (0x3a1e3020);
    t.softHighlight   = juce::Colour (0xffe8f4e1);   // the light, green-tinted - see knobFill
    t.recess          = juce::Colour (0xffa3bfa6);
    t.recessInk       = juce::Colour (0xff43593f);

    t.ledOn           = juce::Colour (0xff2c5e3a);
    t.ledOff          = juce::Colour (0xffa3bfa6);
    t.ledRing         = juce::Colour (0xffa3bfa6);
    t.switchBody      = juce::Colour (0xffa3bfa6);   // track, off
    t.switchHighlight = juce::Colour (0xfff1f8ef);   // the sliding knob

    t.cornerRadius = 20.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.0f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

PedalTheme PedalTheme::onyx()
{
    PedalTheme t;

    // The soft-UI face at night: a near-black card on a blacker page, with one
    // pale blue-grey (#b9d3d9) carrying every reading on it - the lettering,
    // the pointers, the reached ticks. Nothing else has a hue, so anything that
    // is that colour is a value you can read.
    t.controlStyle = ControlStyle::digital;

    t.background      = juce::Colour (0xff0f1315);   // the page behind the card
    t.panel           = juce::Colour (0xff171d20);   // the card
    t.outline         = juce::Colour (0xff2a3336);
    t.bezel           = juce::Colour (0xff222b2e);

    t.textPrimary     = juce::Colour (0xffb9d3d9);
    t.textSecondary   = juce::Colour (0xff8ba3a9);
    t.title           = juce::Colour (0xffb9d3d9);

    // A black cap on a black face. The cap fill is lifted toward `softHighlight`
    // at its top (see `kCapStyle`), so the highlight has to stay a dark grey -
    // a bright one would make the top half glow and the cap would stop reading
    // as black. What separates it from the card is the drop shadow below it and
    // the rim light above, not its own lightness.
    t.knobBody        = juce::Colour (0xff0b0e10);   // the ring
    t.knobFill        = juce::Colour (0xff191f22);   // the cap face, at its darkest
    t.knobOutline     = juce::Colour (0xff2f3a3e);
    t.knobPointer     = juce::Colour (0xffb9d3d9);   // the one thing on the cap you read
    t.knobTrack       = juce::Colour (0xff39474b);   // a tick the value has not reached

    t.accent          = juce::Colour (0xffb9d3d9);
    t.accentDim       = juce::Colour (0xff4d5f65);

    // The reached ticks and the value arcs. Full strength, because on a face
    // this dark it is the only thing carrying information.
    t.glow            = juce::Colour (0xffb9d3d9);

    // A dark soft-UI inverts the usual pair: the shadow goes to near-black and
    // the light is a grey lift rather than a white one.
    t.softShadow      = juce::Colour (0x66000000);
    t.softHighlight   = juce::Colour (0xff364347);
    t.recess          = juce::Colour (0xff101416);
    t.recessInk       = juce::Colour (0xff6c8288);

    t.ledOn           = juce::Colour (0xffb9d3d9);
    t.ledOff          = juce::Colour (0xff222b2e);
    t.ledRing         = juce::Colour (0xff222b2e);
    t.switchBody      = juce::Colour (0xff222b2e);   // track, off
    t.switchHighlight = juce::Colour (0xffb9d3d9);   // the sliding knob

    t.cornerRadius = 20.0f;
    t.knobThickness = 3.0f;
    t.grain = 0.0f;

    t.logoTint = t.title;
    t.titleTypefacePtr = birthstone();

    return t;
}

} // namespace ee::ui
