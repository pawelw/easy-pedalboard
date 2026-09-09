#include "PeakArtifactWebEditor.h"

#include "Params.h"
#include "PluginProcessor.h"
#include "ee/plugin/PresetBridge.h"
#include "ee/plugin/WebFace.h"

#if JUCE_ANDROID
const juce::String PeakArtifactWebEditor::devServerAddress = "http://10.0.2.2:3003/";
#else
const juce::String PeakArtifactWebEditor::devServerAddress = "http://localhost:3003/";
#endif

bool PeakArtifactWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == PeakArtifactWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool PeakArtifactWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
{
    if (triedFallback)
        return true;

    triedFallback = true;
    goToURL (getResourceProviderRoot());
    return false;
}

PeakArtifactWebEditor::PeakArtifactWebEditor (PeakArtifactProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), relays (p.apvts),
      webView (ee::plugin::presetBridge (
          relays.apply (
              juce::WebBrowserComponent::Options {}
                  .withNativeIntegrationEnabled()
                  // Suppress WKWebView's own right-click menu the ordinary web
                  // way - it has no dedicated JUCE option - so a knob drag that
                  // starts with the right button does not pop it up.
                  .withUserScript ("document.addEventListener('contextmenu', function (e) { e.preventDefault(); });")
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
                  // only start/end/skew/interval, not the format string. The
                  // Filter Time knob's reading depends on the Sync switch, which
                  // the web view has no tempo to resolve - so the processor
                  // answers that one.
                  .withNativeFunction ("formatKnobValue",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           const auto queried = args[0].toString();
                                           juce::String text;

                                           if (queried == ee::artifact::id::fltTime)
                                               text = processorRef.timeReadout();
                                           else if (auto* param = processorRef.apvts.getParameter (queried))
                                               text = param->getCurrentValueAsText();

                                           complete (text);
                                       })
                  .withResourceProvider ([this] (const auto& url) { return getResource (url); },
                                         juce::URL { devServerAddress }.getOrigin())),
          p.presets,
          EE_PRESET_SOURCE_DIR))
{
    relays.attach (p.apvts);

    addAndMakeVisible (webView);
    webView.goToURL (kUseDevServer ? devServerAddress : juce::WebBrowserComponent::getResourceProviderRoot());

    // A starting size for the moment before the page reports its own real
    // rendered size (installAutoResize).
    setSize (380, 600);
    setResizable (false, false);

    startTimerHz (45); // the rate Peak Wah's scope feed runs at
}

PeakArtifactWebEditor::~PeakArtifactWebEditor()
{
    stopTimer();
}

void PeakArtifactWebEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

void PeakArtifactWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("modL", processorRef.lfoModLUi.load (std::memory_order_relaxed));
    payload->setProperty ("modR", processorRef.lfoModRUi.load (std::memory_order_relaxed));
    webView.emitEventIfBrowserIsVisible ("filterMod", juce::var (payload));
}

std::optional<juce::WebBrowserComponent::Resource> PeakArtifactWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (PEAKARTIFACT_JSUI_DIR), url);
}
