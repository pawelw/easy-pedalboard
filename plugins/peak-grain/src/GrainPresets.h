#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ee::grain
{

/** A flat file-backed preset store: one XML file per preset under the user's
    application-data directory, `Peak/Peak Grain/Presets`. The processor owns
    one; the face's preset bar reads `names()` / `currentIndex()` and calls the
    verbs. Message-thread only.

    Note: a preset is the APVTS tree alone (`state.copyState()`), so the
    remembered Sync-slot positions the processor keeps outside the tree are not
    carried in a preset - they reseed from the parameters on load. */
class PresetStore
{
public:
    explicit PresetStore (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse)
    {
        directory().createDirectory();
        rescan();
    }

    juce::StringArray names() const { return presetNames; }
    int currentIndex() const { return current; }

    void select (int index)
    {
        if (! juce::isPositiveAndBelow (index, files.size()))
            return;

        if (auto xml = juce::XmlDocument::parse (files.getReference (index)))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            current = index;
        }
    }

    /** Step to the preset `delta` places away and load it, clamped to the ends.
        With nothing selected yet, `next` lands on the first and `prev` the
        last. */
    void step (int delta)
    {
        if (files.isEmpty())
            return;

        const int from = current >= 0 ? current : (delta >= 0 ? -1 : files.size());
        select (juce::jlimit (0, files.size() - 1, from + delta));
    }

    /** Overwrite the selected preset in place, or make a new one when nothing is
        selected. */
    void save()
    {
        if (juce::isPositiveAndBelow (current, files.size()))
            writeTo (files.getReference (current));
        else
            saveAsNew();
    }

    /** Store the current state as a new preset with an auto-incremented name
        ("New Preset", "New Preset 2", ...) and select it. */
    void saveAsNew()
    {
        const auto name = uniqueName ("New Preset");
        writeTo (directory().getChildFile (name + ".xml"));
        rescan();
        current = presetNames.indexOf (name);
    }

private:
    static juce::File directory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Peak")
            .getChildFile ("Peak Grain")
            .getChildFile ("Presets");
    }

    void writeTo (const juce::File& file) const
    {
        if (auto xml = state.copyState().createXml())
            xml->writeTo (file);
    }

    juce::String uniqueName (const juce::String& base) const
    {
        if (! presetNames.contains (base))
            return base;

        for (int n = 2;; ++n)
        {
            const auto candidate = base + " " + juce::String (n);
            if (! presetNames.contains (candidate))
                return candidate;
        }
    }

    void rescan()
    {
        files = directory().findChildFiles (juce::File::findFiles, false, "*.xml");

        struct NameOrder
        {
            int compareElements (const juce::File& a, const juce::File& b) const
            {
                return a.getFileName().compareIgnoreCase (b.getFileName());
            }
        } order;
        files.sort (order);

        presetNames.clear();
        for (const auto& f : files)
            presetNames.add (f.getFileNameWithoutExtension());

        if (! juce::isPositiveAndBelow (current, files.size()))
            current = -1;
    }

    juce::AudioProcessorValueTreeState& state;
    juce::Array<juce::File> files;
    juce::StringArray presetNames;
    int current = -1;
};

} // namespace ee::grain
