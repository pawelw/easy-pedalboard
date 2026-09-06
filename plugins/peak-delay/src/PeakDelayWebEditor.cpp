#include "PeakDelayWebEditor.h"

#include "PluginProcessor.h"

namespace
{
constexpr const char* kParamLeftTime = "ltime";
constexpr const char* kParamRightTime = "rtime";

// Not real APVTS parameter ids - synthetic ones formatKnobValue recognises,
// for the onyx Readout's small "always ms" figure (see
// PluginProcessor.h's timeMsReadout()) alongside the toggle-aware main
// value at kParamLeftTime/kParamRightTime.
constexpr const char* kParamLeftTimeMs = "ltimeMs";
constexpr const char* kParamRightTimeMs = "rtimeMs";

const char* mimeForExtension (const juce::String& extension)
{
    if (extension == "html")
        return "text/html";
    if (extension == "js")
        return "text/javascript";
    if (extension == "css")
        return "text/css";
    if (extension == "json")
        return "application/json";
    if (extension == "svg")
        return "image/svg+xml";
    if (extension == "png")
        return "image/png";
    return "application/octet-stream";
}
} // namespace

#if JUCE_ANDROID
const juce::String PeakDelayWebEditor::devServerAddress = "http://10.0.2.2:3001/";
#else
const juce::String PeakDelayWebEditor::devServerAddress = "http://localhost:3001/";
#endif

bool PeakDelayWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == PeakDelayWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

PeakDelayWebEditor::PeakDelayWebEditor (PeakDelayProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p),
      webView (juce::WebBrowserComponent::Options {}
                   .withNativeIntegrationEnabled()
                   // WKWebView's own right-click context menu ("Reload" is
                   // its only useful item here - there's no navigation
                   // history for Back/Forward) has no dedicated JUCE option
                   // to turn off, so this suppresses it the ordinary web way
                   // instead: a knob click-drag that starts with a right
                   // button, or a trackpad two-finger tap mid-drag, was
                   // popping it up over the control being turned.
                   .withUserScript ("document.addEventListener('contextmenu', function (e) { e.preventDefault(); });")
                   .withOptionsFrom (leftTimeRelay)
                   .withOptionsFrom (rightTimeRelay)
                   .withOptionsFrom (feedbackRelay)
                   .withOptionsFrom (mixRelay)
                   .withOptionsFrom (modRelay)
                   .withOptionsFrom (tapeRelay)
                   .withOptionsFrom (syncRelay)
                   .withOptionsFrom (timeUnitRelay)
                   .withOptionsFrom (onRelay)
                   .withOptionsFrom (controlParameterIndexReceiver)
                   // See jsui/src/autoSize.js: the page measures its own real
                   // rendered size and reports it here, rather than this
                   // editor opening at a size guessed from a browser that
                   // isn't the WebView engine actually rendering it.
                   .withNativeFunction ("reportContentSize",
                                        [this] (const juce::Array<juce::var>& args,
                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                        {
                                            const int width = juce::jmax (100, static_cast<int> (args[0]));
                                            const int height = juce::jmax (100, static_cast<int> (args[1]));
#if EE_TAPE_TUNER
                                            const int panelWidth =
                                                tunerPanel != nullptr ? TapeTunerPanel::preferredWidth : 0;
#else
                                            const int panelWidth = 0;
#endif
                                            setSize (width + panelWidth, height);
                                            complete (true);
                                        })
                   .withNativeFunction ("formatKnobValue",
                                        [this] (const juce::Array<juce::var>& args,
                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                        {
                                            const auto id = args[0].toString();
                                            juce::String text;

                                            if (id == kParamLeftTime)
                                                text = processorRef.leftTimeReadout();
                                            else if (id == kParamRightTime)
                                                text = processorRef.rightTimeReadout();
                                            else if (id == kParamLeftTimeMs)
                                                text = processorRef.leftTimeMsReadout();
                                            else if (id == kParamRightTimeMs)
                                                text = processorRef.rightTimeMsReadout();
                                            else if (auto* param = processorRef.apvts.getParameter (id))
                                                text = param->getCurrentValueAsText();

                                            complete (text);
                                        })
                   // The TapScope's time axis. Numbers, not the formatted
                   // readouts above: the scope places taps at multiples of
                   // the delay time, and what a knob position means in
                   // milliseconds depends on the Sync pill and the host
                   // tempo, neither of which the web view knows.
                   .withNativeFunction ("getDelayTimesMs",
                                        [this] (const juce::Array<juce::var>&,
                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                        {
                                            juce::Array<juce::var> times;
                                            times.add (processorRef.leftTimeMs());
                                            times.add (processorRef.rightTimeMs());

                                            complete (times);
                                        })
                   .withResourceProvider ([this] (const auto& url) { return getResource (url); },
                                          juce::URL { devServerAddress }.getOrigin())),
      leftTimeAttachment (*p.apvts.getParameter (kParamLeftTime), leftTimeRelay, p.apvts.undoManager),
      rightTimeAttachment (*p.apvts.getParameter (kParamRightTime), rightTimeRelay, p.apvts.undoManager),
      feedbackAttachment (*p.apvts.getParameter ("fb"), feedbackRelay, p.apvts.undoManager),
      mixAttachment (*p.apvts.getParameter ("mix"), mixRelay, p.apvts.undoManager),
      modAttachment (*p.apvts.getParameter ("mod"), modRelay, p.apvts.undoManager),
      tapeAttachment (*p.apvts.getParameter ("tape"), tapeRelay, p.apvts.undoManager),
      syncAttachment (*p.apvts.getParameter ("sync"), syncRelay, p.apvts.undoManager),
      timeUnitAttachment (*p.apvts.getParameter ("timeunit"), timeUnitRelay, p.apvts.undoManager),
      onAttachment (*p.apvts.getParameter ("on"), onRelay, p.apvts.undoManager)
{
    addAndMakeVisible (webView);
    webView.goToURL (kUseDevServer ? devServerAddress : juce::WebBrowserComponent::getResourceProviderRoot());
    // Just a starting size for the brief moment before the page's own
    // ResizeObserver reports its real rendered size - see jsui/src/autoSize.js.
    // Close to the real thing on purpose: the width is exact (the card is a
    // fixed 626 plus .page's 4px each side), the height only a guess, so the
    // host sees at most a small vertical correction rather than a window that
    // visibly jumps. It must not be *relied* on - a face whose card can't fit
    // in the starting window used to deadlock here, which is what Card.css's
    // `flex: none` now prevents.
    setSize (634, 440);
    setResizable (false, false);

#if EE_TAPE_TUNER
    // Flip to true to bring the tuning panel back without reconfiguring CMake.
    constexpr bool showTuner = false;

    if (showTuner)
    {
        tunerPanel = std::make_unique<TapeTunerPanel> (processorRef.tapeTuning(), [this] (const ee::dsp::TapeTuning& t)
                                                       { processorRef.setTapeTuning (t); });
        addAndMakeVisible (*tunerPanel);
        setSize (getWidth() + TapeTunerPanel::preferredWidth, getHeight());
    }
#endif
}

PeakDelayWebEditor::~PeakDelayWebEditor() = default;

void PeakDelayWebEditor::resized()
{
#if EE_TAPE_TUNER
    if (tunerPanel != nullptr)
    {
        auto bounds = getLocalBounds();
        tunerPanel->setBounds (bounds.removeFromRight (TapeTunerPanel::preferredWidth));
        webView.setBounds (bounds);
        return;
    }
#endif
    webView.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> PeakDelayWebEditor::getResource (const juce::String& url)
{
    const auto requested = url == "/" ? juce::String { "index.html" } : url.fromFirstOccurrenceOf ("/", false, false);

    const juce::File distDir = juce::File (PEAKDELAY_JSUI_DIR).getChildFile ("dist");
    const auto file = distDir.getChildFile (requested);

    if (! file.existsAsFile())
        return std::nullopt;

    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
        return std::nullopt;

    std::vector<std::byte> bytes (block.getSize());
    std::memcpy (bytes.data(), block.getData(), block.getSize());

    return juce::WebBrowserComponent::Resource { std::move (bytes), juce::String (mimeForExtension (
                                                                        file.getFileExtension().substring (1))) };
}
