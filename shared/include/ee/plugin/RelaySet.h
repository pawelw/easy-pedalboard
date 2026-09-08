#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <vector>

namespace ee::plugin
{

/** Every parameter of a processor, relayed to a WebView face, in three lines.
 *
 * A JUCE web face binds each parameter by hand: a relay member, a
 * `.withOptionsFrom (relay)` in the browser's Options, and an attachment member
 * in the constructor's initialiser list. Three places, in the right order, per
 * parameter. Peak Delay has seventeen of them and that is already thirty-four
 * declarations; Peak Alpine has forty-six, and hand-writing those would be a
 * list that has to be kept in step with the parameter layout by eye - the same
 * silent drift `tests/UiSnapshot.cpp` is warned about in CLAUDE.md.
 *
 * So the list is not written down twice. This walks the processor's own
 * parameters and makes the right kind of relay for each, deciding from the
 * parameter's type: a bool gets a toggle relay, a choice gets a combo relay,
 * everything else gets a slider relay. Add a parameter and the face can bind it
 * with no editor change at all.
 *
 * Usage, and the order matters:
 *
 *     RelaySet relays { apvts };                       // 1. before the browser
 *     webView (relays.apply (Options {} ... ))         // 2. into its Options
 *     ...
 *     relays.attach (apvts);                           // 3. in the ctor body
 *
 * Step 3 has to come after the browser exists, because an attachment pushes the
 * parameter's current value the moment it is made and there has to be something
 * to push it to. Declaring the RelaySet before the browser member is what gets
 * steps 1 and 2 in the right order; the compiler will not check that for you.
 */
class RelaySet
{
public:
    explicit RelaySet (juce::AudioProcessorValueTreeState& apvts)
    {
        for (auto* parameter : apvts.processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            if (ranged == nullptr)
                continue;

            const auto id = ranged->getParameterID();

            if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
                toggles.push_back ({ id, std::make_unique<juce::WebToggleButtonRelay> (id) });
            else if (dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr)
                combos.push_back ({ id, std::make_unique<juce::WebComboBoxRelay> (id) });
            else
                sliders.push_back ({ id, std::make_unique<juce::WebSliderRelay> (id) });
        }
    }

    /** Chains `withOptionsFrom` for every relay, plus the control-parameter
        index receiver a host needs for "what am I hovering". */
    juce::WebBrowserComponent::Options apply (juce::WebBrowserComponent::Options options)
    {
        for (auto& entry : sliders)
            options = options.withOptionsFrom (*entry.relay);
        for (auto& entry : toggles)
            options = options.withOptionsFrom (*entry.relay);
        for (auto& entry : combos)
            options = options.withOptionsFrom (*entry.relay);

        return options.withOptionsFrom (indexReceiver);
    }

    /** Binds each relay to its parameter. After the browser is built - see the
        class note. */
    void attach (juce::AudioProcessorValueTreeState& apvts)
    {
        for (auto& entry : sliders)
            if (auto* p = apvts.getParameter (entry.id))
                sliderAttachments.push_back (
                    std::make_unique<juce::WebSliderParameterAttachment> (*p, *entry.relay, apvts.undoManager));

        for (auto& entry : toggles)
            if (auto* p = apvts.getParameter (entry.id))
                toggleAttachments.push_back (
                    std::make_unique<juce::WebToggleButtonParameterAttachment> (*p, *entry.relay, apvts.undoManager));

        for (auto& entry : combos)
            if (auto* p = apvts.getParameter (entry.id))
                comboAttachments.push_back (
                    std::make_unique<juce::WebComboBoxParameterAttachment> (*p, *entry.relay, apvts.undoManager));
    }

    /** What the host asks when the mouse is over a control. */
    int getControlParameterIndex() const { return indexReceiver.getControlParameterIndex(); }

    int size() const noexcept
    {
        return static_cast<int> (sliders.size() + toggles.size() + combos.size());
    }

private:
    /** A relay does not remember the id it was made with, so it is kept
        alongside - `attach` needs it to find the parameter again. */
    template <typename Relay>
    struct Entry
    {
        juce::String id;
        std::unique_ptr<Relay> relay;
    };

    std::vector<Entry<juce::WebSliderRelay>> sliders;
    std::vector<Entry<juce::WebToggleButtonRelay>> toggles;
    std::vector<Entry<juce::WebComboBoxRelay>> combos;

    juce::WebControlParameterIndexReceiver indexReceiver;

    // Declared after the relays so they are destroyed first: an attachment
    // holds a reference to its relay.
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAttachments;
    std::vector<std::unique_ptr<juce::WebComboBoxParameterAttachment>> comboAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelaySet)
};

} // namespace ee::plugin
