#include "ee/ui/GrainScope.h"

#include <cmath>
#include <vector>

namespace ee::ui
{
namespace
{
    constexpr float kCornerRadius = 6.0f;
    constexpr float kPad = 6.0f;

    // Where the grain cloud gives way to the delay repeats, as a fraction of
    // the strip width.
    constexpr float kNowFrac = 0.40f;

    float lerp (float a, float b, float t) { return a + (b - a) * t; }
} // namespace

GrainScope::GrainScope (juce::AudioProcessorValueTreeState& state,
                        const GrainScopeSpec& specToUse,
                        const PedalTheme& themeToUse)
    : apvts (state), spec (specToUse), theme (themeToUse)
{
    setInterceptsMouseClicks (false, false);

    for (const auto* id : { &spec.sizeID, &spec.densityID, &spec.scatterID, &spec.stereoID, &spec.pitchLowID,
                            &spec.pitchHighID, &spec.delayTimeID, &spec.delayFeedbackID, &spec.delayMixID,
                            &spec.reverbDecayID, &spec.reverbMixID })
        if (id->isNotEmpty())
            apvts.addParameterListener (*id, this);

    if (spec.liveGrains)
        startTimerHz (30);
}

GrainScope::~GrainScope()
{
    stopTimer();

    for (const auto* id : { &spec.sizeID, &spec.densityID, &spec.scatterID, &spec.stereoID, &spec.pitchLowID,
                            &spec.pitchHighID, &spec.delayTimeID, &spec.delayFeedbackID, &spec.delayMixID,
                            &spec.reverbDecayID, &spec.reverbMixID })
        if (id->isNotEmpty())
            apvts.removeParameterListener (*id, this);

    cancelPendingUpdate();
}

void GrainScope::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

void GrainScope::handleAsyncUpdate()
{
    repaint();
}

void GrainScope::timerCallback()
{
    repaint();
}

float GrainScope::value (const juce::String& paramID) const
{
    if (paramID.isNotEmpty())
        if (auto* p = apvts.getParameter (paramID))
            return juce::jlimit (0.0f, 1.0f, p->getValue());

    return 0.0f;
}

void GrainScope::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    if (bounds.getWidth() < 8.0f || bounds.getHeight() < 8.0f)
        return;

    // An almost-white slot, like the module panels, with a soft inner shadow
    // round the rim so it still reads as sunk into the face.
    const bool lightFace = theme.recess.getBrightness() > 0.5f;
    const auto slotFill = lightFace ? juce::Colour (0xfffcfcfd) : theme.panel.brighter (0.10f);

    g.setColour (slotFill);
    g.fillRoundedRectangle (bounds, kCornerRadius);

    juce::Graphics::ScopedSaveState clip (g);
    {
        juce::Path clipPath;
        clipPath.addRoundedRectangle (bounds, kCornerRadius);
        g.reduceClipRegion (clipPath);
    }

    // The inner shadow: concentric strokes hugging the rim, softest and reaching
    // well in from the edge. Drawn now under the content, and again lighter at
    // the end so it also sits over the blobs that run to the edge.
    const auto innerShadow = [&] (float strength)
    {
        constexpr int depth = 16;
        for (int i = 0; i < depth; ++i)
        {
            const float f = 1.0f - static_cast<float> (i) / static_cast<float> (depth);
            g.setColour (juce::Colours::black.withAlpha (0.055f * f * f * strength));
            g.drawRoundedRectangle (bounds.reduced (0.5f + static_cast<float> (i)), kCornerRadius, 1.6f);
        }
    };
    innerShadow (1.0f);

    const auto inner = bounds.reduced (kPad);
    const float midY = inner.getCentreY();
    const float nowX = inner.getX() + inner.getWidth() * kNowFrac;

    const float size01 = value (spec.sizeID);
    const float density01 = value (spec.densityID);
    const float scatter01 = value (spec.scatterID);
    const float stereo01 = value (spec.stereoID);
    const float pLow = value (spec.pitchLowID);
    const float pHigh = value (spec.pitchHighID);
    const float dTime = value (spec.delayTimeID);
    const float dFb = value (spec.delayFeedbackID);
    const float dMix = value (spec.delayMixID);
    const float decay = value (spec.reverbDecayID);
    const float rMix = value (spec.reverbMixID);

    const auto accent = theme.glow;

    //== reverb wash ==========================================================
    // A soft pool that the whole thing dissolves into: brighter with Reverb
    // Mix, reaching further right with Decay.
    if (rMix > 0.01f)
    {
        const float reach = inner.getX() + inner.getWidth() * lerp (0.45f, 1.0f, decay);
        juce::ColourGradient wash (accent.withAlpha (0.20f * rMix), nowX, midY, accent.withAlpha (0.0f), reach, midY,
                                   false);
        g.setGradientFill (wash);
        g.fillRect (juce::Rectangle<float> (nowX, inner.getY(), inner.getRight() - nowX, inner.getHeight()));
    }

    //== centre line =========================================================
    g.setColour (theme.title.withAlpha (0.14f));
    g.fillRect (juce::Rectangle<float> (inner.getX(), midY - 0.5f, inner.getWidth(), 1.0f));

    // The now divider.
    g.setColour (theme.title.withAlpha (0.30f));
    g.fillRect (juce::Rectangle<float> (nowX - 0.5f, inner.getY(), 1.0f, inner.getHeight()));

    //== the grain cloud =====================================================
    const auto cloud = juce::Rectangle<float> (inner.getX(), inner.getY(), nowX - inner.getX(), inner.getHeight());

    const int n = juce::roundToInt (lerp (14.0f, 90.0f, density01));
    const float spreadY = cloud.getHeight() * 0.5f * lerp (0.14f, 0.96f, stereo01);
    const float jitterX = cloud.getWidth() * 0.5f * lerp (0.03f, 0.85f, scatter01);
    const float blobW = lerp (3.0f, 15.0f, size01);
    const float pitchBias = juce::jlimit (-1.0f, 1.0f, pHigh - pLow); // up = airier, sits higher

    juce::Random rng (0x6a11f00d);

    struct Grain
    {
        float xFrac, y, alpha, w;
    };
    std::vector<Grain> grains;
    grains.reserve (static_cast<size_t> (juce::jmax (1, n)));

    for (int i = 0; i < n; ++i)
    {
        const float t = n > 1 ? static_cast<float> (i) / static_cast<float> (n - 1) : 0.5f;

        // Scatter grows toward "now"; the freshest grains are the most spread.
        const float x = cloud.getX() + t * cloud.getWidth()
                        + (rng.nextFloat() - 0.5f) * 2.0f * jitterX * (0.3f + 0.7f * t);
        const float yr = (rng.nextFloat() - 0.5f) * 2.0f;
        const float y = juce::jlimit (inner.getY() + 1.0f, inner.getBottom() - 1.0f,
                                      midY + yr * spreadY - pitchBias * cloud.getHeight() * 0.24f);

        // Older grains (left) have rung down; newer ones (right) are near full.
        const float a = juce::jlimit (0.14f, 1.0f, 0.42f + 0.55f * t);
        const float w = blobW * (0.55f + 0.85f * rng.nextFloat());

        // A touch brighter up high, so the pitch balance also reads as colour.
        const float lift = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * ((midY - y) / juce::jmax (1.0f, spreadY)));
        g.setColour (accent.brighter (0.28f * lift).withAlpha (a));
        g.fillEllipse (x - w * 0.5f, y - w * 0.5f, w, w * 0.9f);

        const float xf = juce::jlimit (0.0f, 1.0f, (x - cloud.getX()) / juce::jmax (1.0f, cloud.getWidth()));
        grains.push_back ({ xf, y, a, w });
    }

    //== the delay repeats ===================================================
    // The whole burst, thrown right at a spacing set by Time, each repeat
    // dimmer by Feedback and squeezed toward the centre line as it recedes.
    if (dMix > 0.02f && ! grains.empty())
    {
        const float spacing = juce::jmax (10.0f, (inner.getRight() - nowX) * lerp (0.08f, 0.42f, dTime));
        const float fb = lerp (0.18f, 0.9f, dFb);

        float alpha = juce::jlimit (0.0f, 1.0f, dMix);
        for (int rep = 1; rep <= 10; ++rep)
        {
            const float x0 = nowX + rep * spacing;
            alpha *= fb;
            if (x0 > inner.getRight() - 2.0f || alpha < 0.02f)
                break;

            const float squish = juce::jmax (0.22f, 1.0f - rep * 0.11f);
            const float echoWidth = cloud.getWidth() * squish * 0.55f;

            for (const auto& gr : grains)
            {
                const float ex = x0 + gr.xFrac * echoWidth;
                if (ex > inner.getRight() - 1.0f)
                    continue;

                const float ey = midY + (gr.y - midY) * squish;
                const float w = juce::jmax (1.2f, gr.w * 0.5f * squish + 1.0f);
                g.setColour (accent.withAlpha (juce::jlimit (0.0f, 0.9f, gr.alpha * alpha)));
                g.fillEllipse (ex - w * 0.5f, ey - w * 0.5f, w, w);
            }
        }
    }

    //== labels ==============================================================
    g.setFont (theme.bodyFont (8.5f).boldened().withExtraKerningFactor (0.12f));
    g.setColour (theme.textSecondary.withAlpha (0.5f));
    g.drawText ("GRAINS", juce::Rectangle<float> (inner.getX() + 2.0f, inner.getY() + 1.0f, 60.0f, 10.0f),
                juce::Justification::topLeft, false);
    g.drawText ("DELAY", juce::Rectangle<float> (nowX + 4.0f, inner.getY() + 1.0f, 60.0f, 10.0f),
                juce::Justification::topLeft, false);

    // A lighter pass of the rim shadow on top, so blobs that run to the edge
    // still sit inside the sunk slot rather than on its lip.
    innerShadow (0.4f);
}

} // namespace ee::ui
