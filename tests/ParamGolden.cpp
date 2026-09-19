// The frozen parameter contract, one pedal per binary.
//
// A parameter id is what a saved session and every preset file is keyed on, and
// a plugin code is what a host looks the plugin up by. Rename either and the
// user's project silently loads a different sound, or no plugin at all - and
// nothing in this tree noticed until now, because both are just strings passed
// to a constructor.
//
// So: instantiate the real processor, write down everything a host or a preset
// can see, and diff it against a file in the repository. The file is the
// contract. Changing it is allowed - adding a parameter is how the product
// grows - but it has to happen in a commit that says so, which is the whole
// point. Run with --update to rewrite it.
//
// Not captured, because JUCE does not keep it: nothing. getVersionHint() is on
// AudioProcessorParameter itself, so the AU ordering hint is in here too.
//
// Read from the *processor*, not from the layout function, so the values are
// what a host actually sees after APVTS has constructed the parameters - which
// is where ee::plugin::snapToRange moves a default onto a representable point.
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace
{
/** %.9g - enough digits to round-trip a float, and no trailing zero noise. A
    default that differs in the last bit is a real difference here: auval reads
    defaults back and compares them. */
juce::String num (float value)
{
    return juce::String::formatted ("%.9g", (double)value);
}

juce::String quoted (const juce::String& s)
{
    return "\"" + s.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\"";
}

void appendIdentity (juce::StringArray& out)
{
    out.add ("identity");
    out.add ("  product      " + quoted (juce::String (EE_GOLDEN_PRODUCT)));
    out.add ("  plugin-code  " + juce::String (EE_GOLDEN_CODE));
    out.add ("  manufacturer " + juce::String (EE_GOLDEN_MANUFACTURER));
    out.add ("  bundle-id    " + juce::String (EE_GOLDEN_BUNDLE));
    out.add ("  categories   " + quoted (juce::String (EE_GOLDEN_CATEGORIES)));
    out.add ("");
}

void appendParameter (juce::StringArray& out, int index, const juce::AudioProcessorParameter& param)
{
    const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (&param);
    const auto id = hosted != nullptr ? hosted->getParameterID() : juce::String();
    out.add (juce::String (index) + " " + (id.isEmpty() ? juce::String ("<no-id>") : id));

    out.add ("    name       " + quoted (param.getName (256)));

    if (param.getLabel().isNotEmpty())
        out.add ("    label      " + quoted (param.getLabel()));

    out.add ("    default    " + num (param.getDefaultValue()));
    out.add ("    version    " + juce::String (param.getVersionHint()));

    // getNumSteps() is AudioProcessor::getDefaultNumParameterSteps() for a
    // continuous parameter - a host-facing number, so it belongs in the freeze.
    out.add ("    steps      " + juce::String (param.getNumSteps()) +
             (param.isDiscrete() ? " discrete" : " continuous") + (param.isBoolean() ? " boolean" : "") +
             (param.isAutomatable() ? " automatable" : " not-automatable") + (param.isMetaParameter() ? " meta" : ""));

    if (auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (&param))
    {
        const auto& r = ranged->getNormalisableRange();
        out.add ("    range      " + num (r.start) + " " + num (r.end) + " interval " + num (r.interval) + " skew " +
                 num (r.skew) + (r.symmetricSkew ? " symmetric" : ""));
    }

    // For a choice parameter this list *is* the engine enum order, which the
    // house rule says may only be appended to: a preset stores the index, so
    // inserting a choice re-points every preset saved after it.
    const auto choices = param.getAllValueStrings();
    if (! choices.isEmpty())
    {
        juce::StringArray q;
        for (const auto& c : choices)
            q.add (quoted (c));

        out.add ("    choices    " + juce::String (choices.size()) + ": " + q.joinIntoString (" "));
    }
}

juce::String render (juce::AudioProcessor& processor)
{
    juce::StringArray out;

    out.add ("# Frozen parameter contract - see tests/ParamGolden.cpp.");
    out.add ("# Every id, range, default and choice order below is what saved sessions and");
    out.add ("# presets are keyed on. Regenerate with --update, in a commit that says why.");
    out.add ("");

    appendIdentity (out);

    const auto& params = processor.getParameters();
    out.add ("parameters " + juce::String (params.size()));

    for (int i = 0; i < params.size(); ++i)
        appendParameter (out, i, *params.getUnchecked (i));

    out.add ("");
    return out.joinIntoString ("\n");
}

/** The first differing line, on one line - dev-check.sh keeps only the lines
    that start with FAIL, so a failure has to say everything in one of them. */
juce::String firstDifference (const juce::String& expected, const juce::String& actual)
{
    juce::StringArray was, now;
    was.addLines (expected);
    now.addLines (actual);

    for (int i = 0; i < juce::jmax (was.size(), now.size()); ++i)
    {
        const auto a = i < was.size() ? was[i].trim() : juce::String ("<end of file>");
        const auto b = i < now.size() ? now[i].trim() : juce::String ("<end of file>");

        if (a != b)
            return "line " + juce::String (i + 1) + ", committed [" + a + "], built [" + b + "]";
    }

    return "no differing line - trailing whitespace";
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const bool update = argc > 1 && juce::String (argv[1]) == "--update";

    EE_GOLDEN_PROCESSOR processor;
    const auto actual = render (processor);

    const juce::File goldenFile { EE_GOLDEN_FILE };
    const auto name = goldenFile.getFileNameWithoutExtension();

    if (update)
    {
        goldenFile.getParentDirectory().createDirectory();

        // Explicit "\n": replaceWithText defaults to CRLF on every platform.
        goldenFile.replaceWithText (actual, false, false, "\n");
        std::printf ("  written  %s\n", goldenFile.getFullPathName().toRawUTF8());
        return 0;
    }

    if (! goldenFile.existsAsFile())
    {
        std::printf ("  FAIL  %s has no contract file - run this binary with --update\n", name.toRawUTF8());
        return 1;
    }

    // Line endings are the one difference that is never anybody's intent - a
    // Windows checkout of this file is still the same contract.
    const auto expected = goldenFile.loadFileAsString().replace ("\r\n", "\n");

    if (expected == actual)
    {
        std::printf ("  ok    %s matches its frozen contract\n", name.toRawUTF8());
        return 0;
    }

    std::printf ("  FAIL  %s no longer matches its frozen contract: %s - rerun with --update if deliberate\n",
                 name.toRawUTF8(), firstDifference (expected, actual).toRawUTF8());
    return 1;
}
