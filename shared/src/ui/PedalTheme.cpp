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

PedalTheme PedalTheme::dark()
{
    return {};
}

} // namespace ee::ui
