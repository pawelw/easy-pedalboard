#pragma once

// The macOS half of ee_face_shots: everything that has to talk to WebKit and
// Core Graphics directly. Kept behind a plain C++ interface so FaceShots.cpp
// never includes Cocoa next to JUCE.

#include <functional>
#include <string>
#include <vector>

namespace ee::shots
{
/** Brings the process forward as an ordinary app, so a window this console
    binary puts on screen is actually drawn - WebKit stops painting a view whose
    window is ordered out or fully occluded. */
void becomeForegroundApp();

/** Runs the Cocoa event loop for `milliseconds` - JUCE's messages, WebKit's
    replies and window drawing all arrive through it. What
    MessageManager::runDispatchLoopUntil does, without needing
    JUCE_MODAL_LOOPS_PERMITTED in the plugin library this links against. */
void pump (int milliseconds);

/** The WKWebView somewhere under `nsView` (a ComponentPeer's native handle), or
    nullptr. Its background is switched off on the way out, so wherever the page
    itself paints nothing the snapshot carries alpha instead of white. */
void* findWebView (void* nsView);

/** Runs `script` in the page. `done` gets the result as a string (a JSON string
    if the script returns one) or, on a JS exception, an empty result and the
    error text. Asynchronous: the caller spins the message loop until it fires. */
void evaluate (void* webView,
               const std::string& script,
               std::function<void (const std::string& result, const std::string& error)> done);

struct Rect
{
    double x = 0, y = 0, w = 0, h = 0; // CSS pixels, page coordinates
};

struct Crop
{
    std::string path;  // where the PNG goes
    Rect rect;         // what to keep; w <= 0 means the whole view
    double margin = 0; // CSS pixels kept around it, for a drop shadow
};

/** Snapshots the whole web view at `pixelsPerCssPixel` and writes each crop as a
    PNG. `done` gets an error message, or
    an empty string. */
void snapshot (void* webView,
               double pixelsPerCssPixel,
               std::vector<Crop> crops,
               std::function<void (const std::string& error)> done);
} // namespace ee::shots
