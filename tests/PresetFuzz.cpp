// Feeds a pedal's state loaders input they were never meant to see.
//
// Two doors let bytes a person (or a disk) controls into a running plugin:
//
//   session   setStateInformation - what a host hands back when a project opens.
//             A project file that was truncated by a full disk, or written by a
//             different build, arrives here.
//   preset    PresetStore::load on a user preset - an XML file in
//             ~/Library/BitBit/<Product>/Presets that the user can edit, move,
//             half-copy or corrupt by hand.
//
// Whatever arrives, the plugin must not throw, must not crash, must leave every
// parameter finite and inside its range, must still make finite audio, and - for
// a preset the store refuses - must leave the state exactly as it found it. A
// plugin that throws on a bad file takes the host down with it, and a host has
// no way to blame the file.
//
// The corpus is generated, not stored: valid states from the real processor
// (default + factory presets) mutated in three ways - bytes (truncation,
// flips, splices, invalid UTF-8, NULs), structure (parameter values swapped for
// NaN / inf / overflow / text, children dropped, duplicated or invented,
// properties overwritten - which reaches the JSON BitBit Grains parses), and
// size (very deep nesting, very wide, very large). Deterministic for a given
// --seed, so a failure reproduces.
//
// One binary per product, like ee_param_golden: every pedal's PluginProcessor.cpp
// defines createPluginFilter(), so two of them cannot share a link.
//
// The preset door writes a file called "zz-fuzz-preset.xml" into the product's
// real user-preset folder - PresetStore names it off the product and there is no
// redirecting it - and removes it on the way out, and at the start of the next
// run if this one died first. Nothing else in that folder is touched.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ee/plugin/PresetStore.h"
#include "ee/plugin/StateVersion.h"

namespace
{
using Processor = EE_FUZZ_PROCESSOR;

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr const char* kFuzzPresetName = "zz-fuzz-preset";

int failures = 0;
int cases = 0;
std::mt19937_64 rng;

// ---------------------------------------------------------------------------
juce::File lastInputFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile (juce::String ("ee_preset_fuzz_") + EE_FUZZ_NAME + "_last_input.bin");
}

/** Written before each case runs, so when the process dies there is a file to
    reproduce from. Big inputs are described instead of stored. */
void rememberInput (const juce::String& label, const juce::MemoryBlock& bytes)
{
    auto file = lastInputFile();
    file.deleteFile();

    if (auto out = file.createOutputStream())
    {
        if (bytes.getSize() < 4u * 1024u * 1024u)
            out->write (bytes.getData(), bytes.getSize());
        else
            out->writeText ("(" + juce::String ((juce::int64) bytes.getSize()) + " bytes, too large to keep) " + label,
                            false, false, nullptr);
    }
}

void fail (const juce::String& label, const juce::String& why)
{
    ++failures;

    if (failures <= 40)
        std::printf ("  FAIL  [%s] %s\n", label.toRawUTF8(), why.toRawUTF8());
}

// ---------------------------------------------------------------------------
juce::MemoryBlock toBlock (const juce::String& text)
{
    juce::MemoryBlock block;
    block.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
    return block;
}

/** The framing getXmlFromBinary expects: a magic word, a length, then the text. */
juce::MemoryBlock sessionFrame (const juce::MemoryBlock& text, int declaredSize = -1, uint32_t magic = 0x21324356u)
{
    juce::MemoryBlock block;
    const uint32_t size = declaredSize >= 0 ? (uint32_t) declaredSize : (uint32_t) text.getSize();
    block.append (&magic, 4);
    block.append (&size, 4);
    block.append (text.getData(), text.getSize());
    block.append ("\0", 1);
    return block;
}

// ---------------------------------------------------------------------------
struct Harness
{
    Processor processor;
    juce::MemoryBlock baseline;                 // a valid session, restored between cases
    std::vector<juce::String> seeds;            // valid state XML, as text
    juce::File presetFile;
    juce::AudioBuffer<float> audio { 2, kBlock };
    juce::MidiBuffer midi;

    Harness()
    {
        processor.setPlayConfigDetails (2, 2, kSampleRate, kBlock);
        processor.prepareToPlay (kSampleRate, kBlock);
        presetFile = processor.presets.userDirectory().getChildFile (juce::String (kFuzzPresetName) + ".xml");
        presetFile.deleteFile();

        // Seeds: the default state and up to three factory presets, each as the
        // state the plugin itself would write.
        processor.getStateInformation (baseline);
        seeds.push_back (currentStateXml());

        const auto factory = processor.presets.factoryNames();
        for (int i = 0; i < factory.size() && i < 3; ++i)
            if (processor.presets.load (ee::plugin::PresetStore::Kind::factory, factory[i]))
                seeds.push_back (currentStateXml());

        restore();
    }

    ~Harness() { presetFile.deleteFile(); }

    juce::String currentStateXml()
    {
        juce::MemoryBlock block;
        processor.getStateInformation (block);

        if (auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize()))
            return xml->toString();

        return {};
    }

    void restore() { processor.setStateInformation (baseline.getData(), (int) baseline.getSize()); }

    std::vector<float> snapshot()
    {
        std::vector<float> values;
        for (auto* p : processor.getParameters())
            values.push_back (p->getValue());
        return values;
    }

    /** What must hold after *any* input, accepted or refused. */
    void checkInvariants (const juce::String& label)
    {
        for (auto* p : processor.getParameters())
        {
            const float v = p->getValue();

            if (! std::isfinite (v) || v < 0.0f || v > 1.0f)
            {
                fail (label, "parameter \"" + p->getName (64) + "\" is " + juce::String (v) + " (must be finite, 0..1)");
                return;
            }

            // Text for a hostile value is what an automation lane shows.
            (void) p->getText (v, 32);
        }

        for (int block = 0; block < 2; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    audio.setSample (ch, i, 0.1f * std::sin (0.05f * (float) (i + block * kBlock)));

            processor.processBlock (audio, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    if (! std::isfinite (audio.getSample (ch, i)))
                    {
                        fail (label, "processBlock produced a non-finite sample after loading");
                        return;
                    }
        }
    }

    // -----------------------------------------------------------------------
    void throughSession (const juce::String& label, const juce::MemoryBlock& bytes)
    {
        ++cases;
        restore();
        rememberInput (label, bytes);

        try
        {
            processor.setStateInformation (bytes.getData(), (int) bytes.getSize());
        }
        catch (const std::exception& e)
        {
            fail (label, juce::String ("session: exception: ") + e.what());
            return;
        }
        catch (...)
        {
            fail (label, "session: exception");
            return;
        }

        checkInvariants ("session " + label);
    }

    void throughPreset (const juce::String& label, const juce::MemoryBlock& bytes)
    {
        ++cases;
        restore();
        rememberInput (label, bytes);

        presetFile.replaceWithData (bytes.getData(), bytes.getSize());
        processor.presets.rescan();

        const auto before = snapshot();
        bool accepted = false;

        try
        {
            accepted = processor.presets.load (ee::plugin::PresetStore::Kind::user, kFuzzPresetName);
        }
        catch (const std::exception& e)
        {
            fail (label, juce::String ("preset: exception: ") + e.what());
            return;
        }
        catch (...)
        {
            fail (label, "preset: exception");
            return;
        }

        if (! accepted && snapshot() != before)
            fail (label, "preset was refused but the parameters moved anyway");

        checkInvariants ("preset " + label);
    }

    /** Both doors, same bytes. The session door gets them framed the way a host
        would, and raw, since the framing is itself an input. */
    void throughBoth (const juce::String& label, const juce::MemoryBlock& text)
    {
        throughPreset (label, text);
        throughSession (label + " (framed)", sessionFrame (text));
    }
};

// ---------------------------------------------------------------------------
const char* const kNastyValues[] = {
    "", " ", "nan", "NaN", "-nan", "inf", "-inf", "Infinity", "1e999", "-1e999", "1e-999", "1e38", "3.5e38", "-3.5e38",
    "0x7fffffff", "0xffffffff", "99999999999999999999", "-99999999999999999999", "2147483648", "-2147483649", "abc", "1,5",
    " 0.5 ", "0.5.5", "--1", "+", ".", "e", "true", "false", "null", "undefined", "{}", "[]", "[[[[[[[[[[[[[[[[[[[[",
    "{\"a\":", "[0,1,2", "[{\"t\":0,\"v\":nan}]", "[{\"t\":1e999,\"v\":-1e999}]", "\\u0000", "\xF0\x9F\x8E\xB8", "%s%s%s%n",
    "../../../etc/passwd", "<", "&", "&amp;", "&#x0;", "&#xFFFFFFFF;", "\"", "'"
};

juce::String nastyValue()
{
    constexpr size_t count = sizeof (kNastyValues) / sizeof (kNastyValues[0]);
    return juce::String::fromUTF8 (kNastyValues[rng() % count]);
}

// --- byte-level ---------------------------------------------------------------
juce::MemoryBlock mutateBytes (const juce::MemoryBlock& source, int kind)
{
    juce::MemoryBlock out (source);
    const size_t n = out.getSize();

    if (n == 0)
        return out;

    auto* bytes = static_cast<uint8_t*> (out.getData());

    switch (kind)
    {
        case 0: // truncate
            out.setSize (rng() % n);
            break;
        case 1: // flip a few bytes
            for (int i = 0, flips = 1 + (int) (rng() % 8); i < flips; ++i)
                bytes[rng() % n] = (uint8_t) rng();
            break;
        case 2: // delete a range
        {
            const size_t a = rng() % n, len = 1 + rng() % std::min<size_t> (n - a, 200);
            out.removeSection (a, len);
            break;
        }
        case 3: // insert random bytes
        {
            juce::MemoryBlock junk;
            junk.setSize (1 + rng() % 64);
            for (size_t i = 0; i < junk.getSize(); ++i)
                junk[i] = (char) rng();
            out.insert (junk.getData(), junk.getSize(), rng() % n);
            break;
        }
        case 4: // duplicate a range
        {
            const size_t a = rng() % n, len = 1 + rng() % std::min<size_t> (n - a, 400);
            juce::MemoryBlock piece (bytes + a, len);
            out.insert (piece.getData(), piece.getSize(), rng() % n);
            break;
        }
        case 5: // NUL a byte
            bytes[rng() % n] = 0;
            break;
        default: // invalid UTF-8
        {
            static const uint8_t bad[][3] = { { 0xFF, 0xFE, 0xFD }, { 0xC0, 0x80, 0x00 }, { 0xED, 0xA0, 0x80 }, { 0xF8, 0x88, 0x80 } };
            const auto& b = bad[rng() % 4];
            out.insert (b, 3, rng() % n);
            break;
        }
    }

    return out;
}

// --- structure-level ----------------------------------------------------------
juce::String mutateTree (const juce::String& seedXml, int kind)
{
    auto xml = juce::XmlDocument::parse (seedXml);

    if (xml == nullptr)
        return seedXml;

    const int children = xml->getNumChildElements();

    switch (kind)
    {
        case 0: // a parameter's value becomes something hostile
            if (children > 0)
                xml->getChildElement ((int) (rng() % (uint64_t) children))->setAttribute ("value", nastyValue());
            break;
        case 1: // ...and its id
            if (children > 0)
                xml->getChildElement ((int) (rng() % (uint64_t) children))->setAttribute ("id", nastyValue());
            break;
        case 2: // drop children
            for (int i = 0, drops = 1 + (int) (rng() % 6); i < drops && xml->getNumChildElements() > 0; ++i)
                xml->removeChildElement (xml->getChildElement ((int) (rng() % (uint64_t) xml->getNumChildElements())), true);
            break;
        case 3: // duplicate a child, with a different value
            if (children > 0)
            {
                auto* copy = new juce::XmlElement (*xml->getChildElement ((int) (rng() % (uint64_t) children)));
                copy->setAttribute ("value", nastyValue());
                xml->addChildElement (copy);
            }
            break;
        case 4: // an invented child
        {
            auto* extra = new juce::XmlElement (rng() % 2 ? "PARAM" : "SOMETHING");
            extra->setAttribute ("id", nastyValue());
            extra->setAttribute ("value", nastyValue());
            xml->addChildElement (extra);
            break;
        }
        case 5: // every attribute on the root, hostile
            for (int i = 0; i < xml->getNumAttributes(); ++i)
                xml->setAttribute (xml->getAttributeName (i), nastyValue());
            break;
        case 6: // a new root property
            xml->setAttribute (nastyValue().retainCharacters ("abcdefghijklmnopqrstuvwxyz").paddedRight ('x', 3), nastyValue());
            break;
        case 7: // every parameter value at once
            for (auto* child : xml->getChildIterator())
                child->setAttribute ("value", nastyValue());
            break;
        case 8: // wrong root
            xml->setTagName (nastyValue().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ").paddedRight ('X', 3));
            break;
        case 9: // properties that are deeply nested JSON: reaches whatever parses one
        {
            const auto depth = 1 + (int) (rng() % 20000);
            const auto open = rng() % 2 ? "[" : "{\"a\":";
            const auto deep = juce::String::repeatedString (open, depth);
            xml->setAttribute ("lfoBreakpoints", deep);
            xml->setAttribute ("lfoRouting", deep);
            break;
        }
        default: // the parameters, all out of range in the same direction
        {
            const char* v = rng() % 2 ? "1e30" : "-1e30";
            for (auto* child : xml->getChildIterator())
                child->setAttribute ("value", v);
            break;
        }
    }

    return xml->toString();
}

// --- fixed size cases ---------------------------------------------------------
juce::String deepNesting (int depth, const juce::String& rootTag)
{
    juce::String s;
    s.preallocateBytes ((size_t) depth * 8 + 64);
    s << "<" << rootTag << ">";
    for (int i = 0; i < depth; ++i)
        s << "<a>";
    for (int i = 0; i < depth; ++i)
        s << "</a>";
    s << "</" << rootTag << ">";
    return s;
}

void runFixedCases (Harness& h)
{
    const auto root = h.processor.apvts.state.getType().toString();

    std::printf ("  fixed inputs: empty, tiny, framing, size and depth\n");

    // Empty and tiny.
    for (const char* text : { "", " ", "\n", "<", "<>", "</>", "<x", "<x/>", "<?xml", "<?xml version=\"1.0\"?>", "\0", "null", "{}" })
        h.throughBoth (juce::String ("tiny '") + text + "'", toBlock (text));

    h.throughBoth ("the right root, nothing in it", toBlock ("<" + root + "/>"));
    h.throughBoth ("the right root, empty and open", toBlock ("<" + root + ">"));
    h.throughBoth ("a different root entirely", toBlock ("<NotThePlugin><PARAM id=\"mix\" value=\"1\"/></NotThePlugin>"));

    // Session framing, which the preset door never sees.
    const auto valid = toBlock (h.seeds.front());

    for (size_t n = 0; n < 12; ++n)
        h.throughSession ("truncated frame " + juce::String ((int) n), juce::MemoryBlock (sessionFrame (valid).getData(), n));

    h.throughSession ("frame claims 2 GB", sessionFrame (valid, 0x7fffffff));
    h.throughSession ("frame claims 4 GB", sessionFrame (valid, -1 + 0x7fffffff));
    h.throughSession ("frame claims 0 bytes", sessionFrame (valid, 0));
    h.throughSession ("frame with the wrong magic", sessionFrame (valid, -1, 0xdeadbeefu));
    h.throughSession ("frame with no terminator", [&]
                      {
                          auto b = sessionFrame (valid);
                          b.setSize (b.getSize() - 1);
                          return b;
                      }());

    {
        juce::MemoryBlock zeros;
        zeros.setSize (100000, true);
        h.throughSession ("100 kB of zeros", zeros);
        h.throughSession ("a lone NUL", juce::MemoryBlock ("\0", 1));
    }

    for (int i = 0; i < 40; ++i)
    {
        juce::MemoryBlock noise;
        noise.setSize (1 + rng() % 4096);
        for (size_t b = 0; b < noise.getSize(); ++b)
            noise[b] = (char) rng();
        h.throughSession ("random bytes #" + juce::String (i), noise);
        h.throughPreset ("random bytes #" + juce::String (i), noise);
    }

    // Size. A parser recursing per element is the one thing here that can take
    // the host's stack down rather than fail a check.
    for (int depth : { 100, 1000, 10000, 100000 })
        h.throughBoth ("nested " + juce::String (depth) + " deep", toBlock (deepNesting (depth, root)));

    {
        juce::String wide ("<" + root + ">");
        for (int i = 0; i < 20000; ++i)
            wide << "<PARAM id=\"p" << i << "\" value=\"0.5\"/>";
        wide << "</" << root << ">";
        h.throughBoth ("20000 unknown children", toBlock (wide));
    }

    {
        juce::String big ("<" + root + " junk=\"" + juce::String::repeatedString ("x", 8 * 1024 * 1024) + "\"/>");
        h.throughBoth ("an 8 MB attribute", toBlock (big));
    }

    {
        juce::String blank ("<" + root + ">" + juce::String::repeatedString (" ", 16 * 1024 * 1024) + "</" + root + ">");
        h.throughBoth ("16 MB of whitespace inside the root", toBlock (blank));
    }

    {
        // A numeric attribute that is a hundred thousand digits long.
        auto xml = juce::XmlDocument::parse (h.seeds.front());
        if (xml != nullptr && xml->getNumChildElements() > 0)
        {
            xml->getChildElement (0)->setAttribute ("value", juce::String::repeatedString ("9", 100000));
            h.throughBoth ("a 100000-digit parameter value", toBlock (xml->toString()));
        }
    }
}

void runGeneratedCases (Harness& h, int iterations)
{
    std::printf ("  %d generated inputs per door, bytes / structure\n", iterations);

    for (int i = 0; i < iterations; ++i)
    {
        const auto& seed = h.seeds[rng() % h.seeds.size()];

        if (rng() % 2 == 0)
        {
            const int kind = (int) (rng() % 7);
            const auto label = "bytes/" + juce::String (kind) + " #" + juce::String (i);
            const auto mutated = mutateBytes (toBlock (seed), kind);
            h.throughPreset (label, mutated);
            h.throughSession (label + " (framed)", sessionFrame (mutated));
        }
        else
        {
            const int kind = (int) (rng() % 11);
            const auto label = "tree/" + juce::String (kind) + " #" + juce::String (i);
            h.throughBoth (label, toBlock (mutateTree (seed, kind)));
        }
    }

    // Two mutations deep, since one rarely gets past the first check.
    for (int i = 0; i < iterations / 4; ++i)
    {
        const auto once = mutateTree (h.seeds[rng() % h.seeds.size()], (int) (rng() % 11));
        const auto twice = mutateBytes (toBlock (mutateTree (once, (int) (rng() % 11))), (int) (rng() % 7));
        const auto label = "compound #" + juce::String (i);
        h.throughPreset (label, twice);
        h.throughSession (label + " (framed)", sessionFrame (twice));
    }
}

// The truncation of a real state at every byte is the most likely corruption
// there is - a write that stopped - so it is done exhaustively, not sampled.
void runEveryTruncation (Harness& h)
{
    const auto text = toBlock (h.seeds.front());
    const size_t step = std::max<size_t> (1, text.getSize() / 400);

    std::printf ("  the default state cut short at every %d bytes (%d cuts)\n",
                 (int) step, (int) (text.getSize() / step));

    for (size_t cut = 0; cut < text.getSize(); cut += step)
    {
        const juce::MemoryBlock piece (text.getData(), cut);
        const auto label = "cut at " + juce::String ((int) cut);
        h.throughPreset (label, piece);
        h.throughSession (label + " (framed)", sessionFrame (piece));
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    uint64_t seed = 0x5eed0001;
    int iterations = 600;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--seed" && i + 1 < argc)
            seed = (uint64_t) juce::String (argv[++i]).getLargeIntValue();
        else if (arg == "--iterations" && i + 1 < argc)
            iterations = juce::String (argv[++i]).getIntValue();
    }

    rng.seed (seed);

    std::printf ("Preset / session fuzzing: %s   seed %llu, %d iterations\n", EE_FUZZ_NAME,
                 (unsigned long long) seed, iterations);
    std::printf ("  if this dies, the input it died on is %s\n", lastInputFile().getFullPathName().toRawUTF8());

    {
        Harness h;

        // A stale file from a run that died mid-case.
        h.presetFile.deleteFile();

        runFixedCases (h);
        runEveryTruncation (h);
        runGeneratedCases (h, iterations);
    }

    lastInputFile().deleteFile();

    std::printf ("\n%d inputs through both doors. ", cases);

    if (failures == 0)
    {
        std::printf ("OK - nothing threw, nothing crashed, and every state that got in was sane\n");
        return 0;
    }

    std::printf ("PRESET FUZZ FAILED (%d failure%s)\n", failures, failures == 1 ? "" : "s");
    return 1;
}
