#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <memory>

namespace ee::plugin
{

/** Parsing for input that somebody else wrote: a host's saved session, a preset
    file a user has been editing, the JSON a face sends down.

    JUCE's XML and JSON parsers are recursive - one native frame or two per level
    of nesting - so a file that is nothing but a few hundred thousand opening
    tags does not fail to parse, it overflows the stack, and the stack it
    overflows is the host's. ASan reproduces it from a 70 kB file (ee_preset_fuzz).
    A legitimate state is a root, its parameters and a property or two, so depth
    beyond a small constant is not a state, and is refused before the parser sees
    it. The scans below are one linear pass with no allocation.

    They may over-count (a `<` inside a comment) and so refuse something odd;
    they never under-count something the real parser would recurse into, which is
    the direction that matters.

    Size is capped too: nothing here writes more than a few hundred kB, and a
    multi-megabyte "state" is a corrupt file, not a project. */
inline constexpr int kMaxNestingDepth = 64;
inline constexpr size_t kMaxStateBytes = 32u * 1024u * 1024u;

inline bool xmlNestingWithin (const char* text, size_t size, int maxDepth = kMaxNestingDepth) noexcept
{
    int depth = 0;
    bool inTag = false;
    char quote = 0;

    for (size_t i = 0; i < size; ++i)
    {
        const char c = text[i];

        if (inTag)
        {
            if (quote != 0)
            {
                if (c == quote)
                    quote = 0;
            }
            else if (c == '"' || c == '\'')
            {
                quote = c;
            }
            else if (c == '>')
            {
                inTag = false;

                if (i > 0 && text[i - 1] == '/' && depth > 0)
                    --depth;
            }
        }
        else if (c == '<')
        {
            const char next = i + 1 < size ? text[i + 1] : '\0';

            if (next == '/')
            {
                if (depth > 0)
                    --depth;
            }
            else if (next != '?' && next != '!')
            {
                if (++depth > maxDepth)
                    return false;
            }

            inTag = true;
            quote = 0;
        }
    }

    return true;
}

inline bool jsonNestingWithin (const juce::String& json, int maxDepth = kMaxNestingDepth) noexcept
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (auto p = json.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;

        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
        }
        else if (c == '"')
        {
            inString = true;
        }
        else if (c == '[' || c == '{')
        {
            if (++depth > maxDepth)
                return false;
        }
        else if ((c == ']' || c == '}') && depth > 0)
        {
            --depth;
        }
    }

    return true;
}

/** `juce::XmlDocument::parse` on text, or null when it is too deep or too big. */
inline std::unique_ptr<juce::XmlElement> parseXmlText (const juce::String& text)
{
    if (text.getNumBytesAsUTF8() > kMaxStateBytes || ! xmlNestingWithin (text.toRawUTF8(), text.getNumBytesAsUTF8()))
        return nullptr;

    return juce::XmlDocument::parse (text);
}

inline std::unique_ptr<juce::XmlElement> parseXmlFile (const juce::File& file)
{
    if (! file.existsAsFile() || (size_t) file.getSize() > kMaxStateBytes)
        return nullptr;

    return parseXmlText (file.loadFileAsString());
}

/** `juce::JSON::parse`, or a void var when the text nests too deeply or is huge. */
inline juce::var parseJson (const juce::String& json)
{
    if (json.getNumBytesAsUTF8() > kMaxStateBytes || ! jsonNestingWithin (json))
        return {};

    return juce::JSON::parse (json);
}

} // namespace ee::plugin
