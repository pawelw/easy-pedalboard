// ee::plugin::PresetStore against a real pedal. Peak Delay is the one wired to
// it, and the store is shared machinery every other pedal is meant to adopt -
// so what is checked here is the store's contract, not the delay's.
//
// The user bank is redirected nowhere: PresetStore names its own folder off the
// product name, so this writes into the real "Peak Preset Tests" folder under
// the user's application-data directory and cleans it up afterwards. Nothing it
// touches belongs to an installed pedal.
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ee/plugin/PresetStore.h"

namespace
{
int failures = 0;

void check (bool condition, const juce::String& what)
{
    std::printf ("  %s  %s\n", condition ? "ok  " : "FAIL", what.toRawUTF8());

    if (! condition)
        ++failures;
}

float valueOf (juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* raw = state.getRawParameterValue (id);
    return raw != nullptr ? raw->load() : std::numeric_limits<float>::quiet_NaN();
}

void setValue (juce::AudioProcessorValueTreeState& state, const char* id, float denormalised)
{
    if (auto* param = state.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (denormalised));
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    PeakDelayProcessor processor;
    auto& presets = processor.presets;

    std::printf ("Factory bank, compiled in from plugins/peak-delay/presets:\n");

    const auto factory = presets.factoryNames();
    std::printf ("  %d preset(s): %s\n", factory.size(), factory.joinIntoString (", ").toRawUTF8());

    check (factory.size() >= 4, "the shipped presets are in the binary");
    check (factory.contains ("Init"), "\"Init\" is one of them");
    check (factory.contains ("Ping Pong Quarters"), "so is \"Ping Pong Quarters\" - a name with spaces survives");

    // Every factory preset has to load and has to carry every parameter.
    // APVTS::replaceState re-appends a parameter the tree is missing and fills
    // it from whatever that parameter currently holds, so a preset that omitted
    // one would silently inherit it from the preset loaded before - which is
    // exactly what this pair of loads would catch.
    std::printf ("\nEvery factory preset loads, and carries every parameter:\n");

    setValue (processor.apvts, "dtype", 2.0f);
    setValue (processor.apvts, "phaser", 77.0f);
    setValue (processor.apvts, "hicut", 3000.0f);

    check (presets.load (ee::plugin::PresetStore::Kind::factory, "Init"), "\"Init\" loads");
    check (std::abs (valueOf (processor.apvts, "dtype") - 0.0f) < 1.0e-4f, "...and takes dtype back to Normal");
    check (std::abs (valueOf (processor.apvts, "phaser") - 0.0f) < 1.0e-3f, "...and Phaser back to 0");
    check (std::abs (valueOf (processor.apvts, "hicut") - 20000.0f) < 1.0f, "...and High Cut back to wide open");

    check (presets.load (ee::plugin::PresetStore::Kind::factory, "Ping Pong Quarters"), "\"Ping Pong Quarters\" loads");
    check (std::abs (valueOf (processor.apvts, "dtype") - 2.0f) < 1.0e-4f, "...and selects Ping Pong");

    check (presets.load (ee::plugin::PresetStore::Kind::factory, "Dotted Wide"), "\"Dotted Wide\" loads");
    check (std::abs (valueOf (processor.apvts, "dtype") - 1.0f) < 1.0e-4f, "...and selects Wide");

    check (! presets.load (ee::plugin::PresetStore::Kind::factory, "Nothing By This Name"),
           "a name that is not in the bank fails rather than half-loading");

    check (presets.currentName() == "Dotted Wide", "...and leaves the selection where it was");

    // The user bank. A second store on the same processor, under a product name
    // no installed pedal uses, so the folder this creates is this test's own.
    std::printf ("\nUser bank round trip:\n");

    ee::plugin::PresetStore userBank { processor.apvts, "Peak Preset Tests" };
    const auto folder = userBank.userDirectory();
    folder.deleteRecursively();
    folder.createDirectory();
    userBank.rescan();

    check (userBank.userNames().isEmpty(), "a fresh bank is empty");

    setValue (processor.apvts, "fb", 71.5f);
    setValue (processor.apvts, "dtype", 1.0f);

    check (userBank.saveUser ("My Take").wasOk(), "a preset saves");
    check (userBank.userNames().contains ("My Take"), "...and shows up in the list");
    check (userBank.currentName() == "My Take", "...and becomes the selection");
    check (folder.getChildFile ("My Take.xml").existsAsFile(), "...as a file named after it");

    setValue (processor.apvts, "fb", 10.0f);
    setValue (processor.apvts, "dtype", 0.0f);

    check (userBank.load (ee::plugin::PresetStore::Kind::user, "My Take"), "it loads back");
    check (std::abs (valueOf (processor.apvts, "fb") - 71.5f) < 0.05f, "...restoring Feedback");
    check (std::abs (valueOf (processor.apvts, "dtype") - 1.0f) < 1.0e-4f, "...and the delay type");

    check (userBank.saveUser ("My Take").wasOk(), "saving the same name again overwrites");
    check (userBank.userNames().size() == 1, "...rather than making a second preset");

    // A name is typed by a person and then used as a filename. Anything that
    // would make one mean a different place on disk has to come out.
    check (userBank.saveUser ("  Spaced  ").wasOk(), "a padded name saves");
    check (userBank.userNames().contains ("Spaced"), "...trimmed");
    check (userBank.saveUser ("a/../../b").wasOk(), "a name with path separators saves");
    check (folder.getChildFile ("a....b.xml").existsAsFile(), "...flattened into the bank's own folder");
    check (! userBank.saveUser ("   ").wasOk(), "a name that is only whitespace is refused");

    std::printf ("\nStepping walks the factory bank and then the user one:\n");

    ee::plugin::PresetStore stepper { processor.apvts, "Peak Preset Tests", EE_FACTORY_PRESETS };
    stepper.load (ee::plugin::PresetStore::Kind::factory, stepper.factoryNames()[0]);

    juce::StringArray walked { stepper.currentName() };
    for (int i = 0; i < stepper.factoryNames().size() + stepper.userNames().size() - 1; ++i)
    {
        stepper.step (1);
        walked.add (stepper.currentName());
    }

    std::printf ("  %s\n", walked.joinIntoString (" -> ").toRawUTF8());

    juce::StringArray expected = stepper.factoryNames();
    expected.addArray (stepper.userNames());
    check (walked == expected, "next runs off the end of the factory bank into the user one");

    stepper.step (1);
    check (stepper.currentName() == expected[0], "...and wraps back to the first");

    // Backwards off the front is the case an unsigned modulo gets wrong: the
    // negative index converts to something enormous and the arrow lands on an
    // arbitrary preset rather than the last one.
    stepper.step (-1);
    check (stepper.currentName() == expected[expected.size() - 1], "prev off the front wraps to the last");

    stepper.step (-1);
    check (stepper.currentName() == expected[expected.size() - 2], "...and keeps walking back from there");

    // The author-only save. The button that reaches it is compiled out unless
    // EE_PRESET_AUTHOR is on and the bridge refuses the call besides, but the
    // store's own method is always here and is what actually writes the file
    // that gets committed - so it is checked whatever this build is.
    std::printf ("\nThe author-only write into a pedal's presets/ folder:\n");

    const auto sourceDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("ee-preset-author-test");
    sourceDir.deleteRecursively();

    check (userBank.saveFactorySource ("Shipped Thing", sourceDir).wasOk(), "it writes into a folder that did not exist");
    check (sourceDir.getChildFile ("Shipped Thing.xml").existsAsFile(), "...as the file cmake would glob");
    check (! userBank.userNames().contains ("Shipped Thing"), "...and does not touch the user bank");
    check (! userBank.factoryNames().contains ("Shipped Thing"),
           "...nor the running factory bank, which only a rebuild can grow");
    check (! userBank.saveFactorySource ("Shipped Thing", juce::File()).wasOk(),
           "a build with no preset source folder refuses rather than writing somewhere odd");

    sourceDir.deleteRecursively();

    std::printf ("\nDeleting:\n");
    check (userBank.deleteUser ("Spaced"), "a user preset deletes");
    check (! userBank.userNames().contains ("Spaced"), "...and leaves the list");
    check (! userBank.deleteUser ("Spaced"), "deleting it twice fails rather than pretending");

    folder.deleteRecursively();

    std::printf ("\n%s\n", failures == 0 ? "ALL PRESET TESTS PASSED"
                                         : ("PRESET TESTS FAILED (" + juce::String (failures) + ")").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
