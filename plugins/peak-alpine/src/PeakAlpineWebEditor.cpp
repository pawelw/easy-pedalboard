#include "PeakAlpineWebEditor.h"

#include "Params.h"
#include "PluginProcessor.h"
#include "ee/plugin/PresetBridge.h"
#include "ee/plugin/WebFace.h"

namespace
{
using namespace ee::alpine;

// Not real APVTS parameter ids - synthetic ones formatKnobValue recognises, for
// the Delay module's Readout, which shows a constant millisecond figure beside
// the toggle-aware value. The face builds them by appending "Ms" to the *scoped*
// id, so they arrive here already carrying the module prefix.
const juce::String kLeftTimeMs = juce::String (id::dlyLeftTime) + "Ms";
const juce::String kRightTimeMs = juce::String (id::dlyRightTime) + "Ms";
} // namespace

#if JUCE_ANDROID
const juce::String PeakAlpineWebEditor::devServerAddress = "http://10.0.2.2:3002/";
#else
const juce::String PeakAlpineWebEditor::devServerAddress = "http://localhost:3002/";
#endif

bool PeakAlpineWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == PeakAlpineWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool PeakAlpineWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
{
    // Only a dev-server build gets here, and only when the server is not
    // running. Fall back to whatever was last built into jsui/dist rather than
    // leaving the host showing WKWebView's "cannot connect" page.
    if (triedFallback)
        return true;

    triedFallback = true;
    goToURL (getResourceProviderRoot());
    return false;
}

PeakAlpineWebEditor::PeakAlpineWebEditor (PeakAlpineProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), relays (p.apvts),
      // Every parameter's relay in one call, then the preset bar's five native
      // functions in another. What is left written out here is only what is
      // genuinely this plugin's: the three functions the face asks questions
      // through, and where the page comes from.
      webView (ee::plugin::presetBridge (
          relays.apply (
              juce::WebBrowserComponent::Options {}
                  .withNativeIntegrationEnabled()
                  // WKWebView's own right-click context menu has no dedicated
                  // JUCE option to turn off, so this suppresses it the ordinary
                  // web way instead: a knob click-drag that starts with a right
                  // button, or a trackpad two-finger tap mid-drag, was popping
                  // it up over the control being turned.
                  .withUserScript ("document.addEventListener('contextmenu', function (e) { e.preventDefault(); });")
                  // See installAutoResize: the page measures its own real
                  // rendered size and reports it here, rather than this editor
                  // opening at a size guessed from a browser that isn't the
                  // WebView engine actually rendering it.
                  .withNativeFunction ("reportContentSize",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           setSize (juce::jmax (100, static_cast<int> (args[0])),
                                                    juce::jmax (100, static_cast<int> (args[1])));
                                           complete (true);
                                       })
                  // A knob's printed value. A native function rather than the
                  // parameter's own stringFromValue because JUCE's relays carry
                  // only start/end/skew/interval, not the format string.
                  .withNativeFunction ("formatKnobValue",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           const auto queried = args[0].toString();
                                           juce::String text;

                                           if (queried == id::dlyLeftTime || queried == id::dlyRightTime)
                                               text = processorRef.timeReadout (queried.toRawUTF8());
                                           else if (queried == kLeftTimeMs)
                                               text = processorRef.timeMsReadout (id::dlyLeftTime);
                                           else if (queried == kRightTimeMs)
                                               text = processorRef.timeMsReadout (id::dlyRightTime);
                                           else if (auto* param = processorRef.apvts.getParameter (queried))
                                               text = param->getCurrentValueAsText();

                                           complete (text);
                                       })
                  // The TapScope's time axis. Numbers, not the formatted
                  // readouts above: the scope places taps at multiples of the
                  // delay time, and what a knob position means in milliseconds
                  // depends on the Sync pill and the host tempo, neither of
                  // which the web view knows.
                  .withNativeFunction ("getDelayTimesMs",
                                       [this] (const juce::Array<juce::var>&,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           juce::Array<juce::var> times;
                                           times.add (processorRef.timeMs (id::dlyLeftTime));
                                           times.add (processorRef.timeMs (id::dlyRightTime));

                                           complete (times);
                                       })
                  .withResourceProvider ([this] (const auto& url) { return getResource (url); },
                                         juce::URL { devServerAddress }.getOrigin())),
          p.presets,
          EE_PRESET_SOURCE_DIR))
{
    // After the browser exists: an attachment pushes its parameter's current
    // value the moment it is made. See RelaySet.
    relays.attach (p.apvts);

    addAndMakeVisible (webView);
    webView.goToURL (kUseDevServer ? devServerAddress : juce::WebBrowserComponent::getResourceProviderRoot());

    // A starting size for the moment before the page reports its own. The width
    // is exact - the host panel measures 966 (a 14px frame and a 1px border
    // either side of a row of 180 + 8 + 560 + 8 + 180) plus the page's 4px
    // either side - and the height is a close guess, so the host sees at most a
    // small vertical correction rather than a window that visibly jumps.
    setSize (974, 590);
    setResizable (false, false);

    startTimerHz (45); // the rate every other face's live feed runs at
}

PeakAlpineWebEditor::~PeakAlpineWebEditor()
{
    stopTimer();
}

void PeakAlpineWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("level", processorRef.inputMeter.getLevel());
    payload->setProperty ("strikes", processorRef.inputMeter.getStrikes());
    payload->setProperty ("bpm", processorRef.hostBpm());
    webView.emitEventIfBrowserIsVisible ("delayMeter", juce::var (payload));
}

void PeakAlpineWebEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> PeakAlpineWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (PEAKALPINE_JSUI_DIR), url);
}
