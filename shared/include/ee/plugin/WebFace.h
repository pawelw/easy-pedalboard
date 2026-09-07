#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstring>
#include <optional>
#include <vector>

namespace ee::plugin
{

/** Serving a WebView pedal face's page off disk.

    A face built with EE_JSUI_DEV_SERVER=OFF - which is every build that is not
    someone's hot-reload loop - reads its page out of the pedal's jsui/dist
    through juce::WebBrowserComponent's resource provider. That is the mode an
    installed plugin runs in, so it must not depend on anything the user has to
    remember to start.

    The one way it can still fail is jsui/dist not being there at all, because
    nobody has run `npm run build` in that pedal's jsui since checkout. Returning
    nullopt for index.html in that case leaves the host showing an empty window
    with nothing to go on, which is indistinguishable from a crashed editor -
    so missingPage() is served instead and says which command to run.
*/
namespace webface
{

inline const char* mimeForExtension (const juce::String& extension)
{
    if (extension == "html")  return "text/html";
    if (extension == "js")    return "text/javascript";
    if (extension == "css")   return "text/css";
    if (extension == "json")  return "application/json";
    if (extension == "svg")   return "image/svg+xml";
    if (extension == "png")   return "image/png";
    if (extension == "woff2") return "font/woff2";
    if (extension == "woff")  return "font/woff";
    return "application/octet-stream";
}

inline juce::WebBrowserComponent::Resource asResource (const juce::String& text,
                                                       const juce::String& mimeType)
{
    const auto utf8 = text.toRawUTF8();
    const auto size = std::strlen (utf8);

    std::vector<std::byte> bytes (size);
    std::memcpy (bytes.data(), utf8, size);

    return { std::move (bytes), mimeType };
}

/** Shown in place of a face whose jsui/dist has never been built. Deliberately
    plain and self-contained - it cannot itself depend on anything in dist. */
inline juce::WebBrowserComponent::Resource missingPage (const juce::String& jsuiDir)
{
    const juce::String html =
        "<!doctype html><meta charset=\"utf-8\">"
        "<style>body{margin:0;display:flex;align-items:center;justify-content:center;"
        "height:100vh;background:#171d20;color:#b9d3d9;"
        "font:14px/1.6 -apple-system,Helvetica Neue,sans-serif;text-align:center}"
        "code{display:block;margin-top:10px;color:#d8f088;font-size:12px}</style>"
        "<div><b>This pedal's face has not been built.</b><code>npm run build --prefix "
        + jsuiDir + "</code></div>";

    return asResource (html, "text/html");
}

/** One file out of `distDir`, or the notice above when the page itself is
    missing. `url` is the resource provider's, so "/" means index.html. */
inline std::optional<juce::WebBrowserComponent::Resource>
    serveFromDist (const juce::File& jsuiDir, const juce::String& url)
{
    const auto requested = url == "/" ? juce::String { "index.html" }
                                      : url.fromFirstOccurrenceOf ("/", false, false);

    const auto file = jsuiDir.getChildFile ("dist").getChildFile (requested);

    if (! file.existsAsFile())
        return requested == "index.html"
                   ? std::optional { missingPage (jsuiDir.getFullPathName()) }
                   : std::nullopt;

    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
        return std::nullopt;

    std::vector<std::byte> bytes (block.getSize());
    std::memcpy (bytes.data(), block.getData(), block.getSize());

    return juce::WebBrowserComponent::Resource {
        std::move (bytes), juce::String (mimeForExtension (file.getFileExtension().substring (1)))
    };
}

} // namespace webface
} // namespace ee::plugin
