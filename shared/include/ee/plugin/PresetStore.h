#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ee::plugin
{

/** The generated `BinaryData`-style accessors for a pedal's factory bank.
    Default-constructed means "this pedal ships none", which is a perfectly good
    state - the user bank still works.

    Outside PresetStore rather than nested in it, so PresetStore can take one by
    default argument: a nested type's default member initialisers are not usable
    in the enclosing class's own declarations. */
struct FactoryBank
{
    const char* const* originalFilenames = nullptr;
    int count = 0;
    const char* (*getNamedResource) (const char*, int&) = nullptr;
    const char* const* namedResourceList = nullptr;
};

/** A pedal's presets: a read-only factory bank shipped inside the binary, and a
    read/write user bank on disk. One store per processor; message-thread only.

    A preset is the APVTS tree and nothing else (`state.copyState()`), so
    anything a processor keeps outside the tree is not carried in one and
    reseeds from the parameters on load. That is the same contract Peak Grain's
    own store has always had - this is that store generalised, with a factory
    bank added and the name chosen by the caller rather than auto-numbered.

    **Factory presets** are files in the pedal's own `presets/` folder,
    committed to the repo and compiled into the plugin by `peak_add_plugin`
    (see cmake/AddPeakPlugin.cmake). They ship with the binary, so they are
    there in a DAW on a machine that has never run this build. Nothing at
    runtime can add to them: a new one is a file in the source tree and a
    rebuild, which is what makes "the presets that came with the plugin" a
    thing the release actually pins.

    **User presets** are XML files under the user's application-data directory,
    one per preset, named by the file. Free to come and go while the plugin
    runs.

    The store is deliberately ignorant of *where* factory data comes from - it
    is handed the four symbols a `juce_add_binary_data` target generates, so it
    does not have to include a header that only exists inside one plugin
    target. `EE_FACTORY_PRESETS` in a pedal's PluginProcessor.cpp packs them.
*/
class PresetStore
{
public:
    enum class Kind
    {
        factory,
        user
    };

    /** `productName` is the folder the user bank lives in, so it should be the
        pedal's product name ("Peak Delay") rather than its target name. */
    PresetStore (juce::AudioProcessorValueTreeState& stateToUse, juce::String productName,
                 FactoryBank factoryBank = {})
        : state (stateToUse), product (std::move (productName)), factory (factoryBank)
    {
        userDirectory().createDirectory();
        rescan();
    }

    //==============================================================================
    juce::StringArray factoryNames() const { return factoryList; }
    juce::StringArray userNames() const { return userList; }

    /** What is loaded right now, or an empty name when nothing has been loaded
        this session. Deliberately not "what the parameters currently match":
        the face shows the preset you picked, and a knob moved afterwards does
        not silently deselect it. */
    Kind currentKind() const { return selectedKind; }
    juce::String currentName() const { return selectedName; }

    //==============================================================================
    /** Replaces the processor's state with the named preset. False when there
        is no such preset, which is the honest answer for a user preset deleted
        from the folder while the plugin was open. */
    bool load (Kind kind, const juce::String& name)
    {
        const auto xml = kind == Kind::factory ? readFactory (name) : readUser (name);

        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return false;

        state.replaceState (juce::ValueTree::fromXml (*xml));

        selectedKind = kind;
        selectedName = name;
        return true;
    }

    /** The prev / next arrows. Walks the factory bank and then the user one as
        one list, so stepping off the end of the shipped presets carries on into
        yours rather than stopping. Wraps. */
    void step (int delta)
    {
        const auto all = flattened();

        if (all.empty())
            return;

        // `int`, not the vector's own size_t: the wrap below adds a negative
        // number, and against an unsigned count that converts to something
        // enormous and lands the arrows on an arbitrary preset instead of the
        // last one.
        const int count = (int) all.size();
        const int from = indexOfCurrent (all);

        // Nothing selected yet: `next` lands on the first entry and `prev` on
        // the last, rather than both landing on the same arbitrary one.
        const int next = from < 0 ? (delta >= 0 ? 0 : count - 1) : ((from + delta) % count + count) % count;

        load (all[(size_t) next].kind, all[(size_t) next].name);
    }

    //==============================================================================
    /** Writes the current state to the user bank under `name`, overwriting a
        user preset of that name and selecting it. The error text is meant to be
        shown to whoever typed the name. */
    juce::Result saveUser (const juce::String& name)
    {
        const auto clean = sanitise (name);

        if (clean.isEmpty())
            return juce::Result::fail ("Give the preset a name.");

        const auto file = userDirectory().getChildFile (clean + presetExtension);

        if (! write (file))
            return juce::Result::fail ("Could not write to " + file.getParentDirectory().getFullPathName());

        rescan();
        selectedKind = Kind::user;
        selectedName = clean;
        return juce::Result::ok();
    }

    bool deleteUser (const juce::String& name)
    {
        const auto file = userDirectory().getChildFile (sanitise (name) + presetExtension);

        if (! file.existsAsFile() || ! file.deleteFile())
            return false;

        if (selectedKind == Kind::user && selectedName == name)
            selectedName = {};

        rescan();
        return true;
    }

    /** Author-only, and only in a build configured with `EE_PRESET_AUTHOR=ON`:
        writes the current state into the pedal's `presets/` folder **in the
        source tree**, where it can be reviewed, committed and compiled into the
        next release.

        It deliberately does not touch the running factory bank. The bank in
        this process came out of the binary and cannot grow at runtime, and
        pretending otherwise would give the author a preset that works on their
        machine until the next build and on nobody else's ever. The list only
        picks the new preset up once it has actually been built in - which is
        the point at which it is real. */
    juce::Result saveFactorySource (const juce::String& name, const juce::File& sourceDirectory)
    {
        const auto clean = sanitise (name);

        if (clean.isEmpty())
            return juce::Result::fail ("Give the preset a name.");

        if (sourceDirectory == juce::File())
            return juce::Result::fail ("This build has no preset source folder configured.");

        if (! sourceDirectory.createDirectory())
            return juce::Result::fail ("Could not create " + sourceDirectory.getFullPathName());

        const auto file = sourceDirectory.getChildFile (clean + presetExtension);

        if (! write (file))
            return juce::Result::fail ("Could not write to " + sourceDirectory.getFullPathName());

        return juce::Result::ok();
    }

    //==============================================================================
    juce::File userDirectory() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Peak")
            .getChildFile (product)
            .getChildFile ("Presets");
    }

    /** Re-reads the user folder. Worth calling when the face reopens: another
        instance of the same plugin may have saved something since. */
    void rescan()
    {
        auto files = userDirectory().findChildFiles (juce::File::findFiles, false, "*" + juce::String (presetExtension));

        struct NameOrder
        {
            int compareElements (const juce::File& a, const juce::File& b) const
            {
                return a.getFileName().compareIgnoreCase (b.getFileName());
            }
        } order;
        files.sort (order);

        userList.clear();
        for (const auto& f : files)
            userList.add (f.getFileNameWithoutExtension());

        rescanFactory();
    }

private:
    static constexpr const char* presetExtension = ".xml";

    struct Entry
    {
        Kind kind;
        juce::String name;
    };

    /** Names are typed by a person and then used as a filename, so they are
        stripped of anything that would make one mean a different place on disk
        rather than a different preset. */
    static juce::String sanitise (const juce::String& name)
    {
        return name.removeCharacters ("/\\:*?\"<>|").trim();
    }

    bool write (const juce::File& file) const
    {
        const auto xml = state.copyState().createXml();
        return xml != nullptr && xml->writeTo (file);
    }

    void rescanFactory()
    {
        factoryList.clear();

        if (factory.originalFilenames == nullptr)
            return;

        // Order is the order the files were handed to juce_add_binary_data,
        // which cmake globs alphabetically - so a pedal can pin "Init" first
        // by naming it that way, and otherwise gets a stable A-Z bank.
        for (int i = 0; i < factory.count; ++i)
            factoryList.add (juce::File (factory.originalFilenames[i]).getFileNameWithoutExtension());
    }

    std::unique_ptr<juce::XmlElement> readFactory (const juce::String& name) const
    {
        const int index = factoryList.indexOf (name);

        if (index < 0 || factory.getNamedResource == nullptr || factory.namedResourceList == nullptr)
            return nullptr;

        int size = 0;
        const char* data = factory.getNamedResource (factory.namedResourceList[index], size);

        if (data == nullptr || size <= 0)
            return nullptr;

        return juce::XmlDocument::parse (juce::String::fromUTF8 (data, size));
    }

    std::unique_ptr<juce::XmlElement> readUser (const juce::String& name) const
    {
        return juce::XmlDocument::parse (userDirectory().getChildFile (sanitise (name) + presetExtension));
    }

    std::vector<Entry> flattened() const
    {
        std::vector<Entry> all;
        all.reserve ((size_t) (factoryList.size() + userList.size()));

        for (const auto& n : factoryList)
            all.push_back ({ Kind::factory, n });

        for (const auto& n : userList)
            all.push_back ({ Kind::user, n });

        return all;
    }

    int indexOfCurrent (const std::vector<Entry>& all) const
    {
        if (selectedName.isEmpty())
            return -1;

        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].kind == selectedKind && all[i].name == selectedName)
                return (int) i;

        return -1;
    }

    juce::AudioProcessorValueTreeState& state;
    juce::String product;
    FactoryBank factory;

    juce::StringArray factoryList;
    juce::StringArray userList;

    Kind selectedKind = Kind::factory;
    juce::String selectedName;
};

} // namespace ee::plugin

/** Packs a pedal's generated binary-data namespace into a FactoryBank.
    `peak_add_plugin` defines EE_FACTORY_PRESETS_HEADER and the namespace name
    when the pedal has a `presets/` folder, so the pedal's own processor writes:

        #if EE_HAS_FACTORY_PRESETS
        #include EE_FACTORY_PRESETS_HEADER
        #endif
        ...
        ee::plugin::PresetStore presets { apvts, "Peak Delay", EE_FACTORY_PRESETS };

    and compiles either way. */
#if EE_HAS_FACTORY_PRESETS
#define EE_FACTORY_PRESETS                                                                                             \
    ee::plugin::FactoryBank                                                                                            \
    {                                                                                                                  \
        FactoryPresets::originalFilenames, FactoryPresets::namedResourceListSize, FactoryPresets::getNamedResource,     \
            FactoryPresets::namedResourceList                                                                          \
    }
#else
#define EE_FACTORY_PRESETS ee::plugin::FactoryBank {}
#endif
