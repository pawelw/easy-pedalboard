#include "PeakModulationWebEditor.h"

#include "Params.h"
#include "PluginProcessor.h"
#include "ee/plugin/PresetBridge.h"
#include "ee/plugin/WebFace.h"

// Port 3004 - Wah is 3000, Delay 3001, Alpine 3002, Artifact 3003, Reverb 3005.
#if JUCE_ANDROID
const juce::String PeakModulationWebEditor::devServerAddress = "http://10.0.2.2:3004/";
#else
const juce::String PeakModulationWebEditor::devServerAddress = "http://localhost:3004/";
#endif

bool PeakModulationWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == PeakModulationWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool PeakModulationWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
{
    if (triedFallback)
        return true;

    triedFallback = true;
    goToURL (getResourceProviderRoot());
    return false;
}

PeakModulationWebEditor::PeakModulationWebEditor (PeakModulationProcessor& p)
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
                  // only start/end/skew/interval, not the format string - and
                  // because the Filter Time and Tremolo Rate readouts depend on
                  // a Sync switch the host text cannot see.
                  .withNativeFunction ("formatKnobValue",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           using namespace ee::modulation;

                                           const auto queried = args[0].toString();
                                           juce::String text;

                                           if (queried == id::filterTime)
                                               text = processorRef.filterTimeReadout();
                                           else if (queried == id::tremRate)
                                               text = processorRef.tremoloRateReadout();
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

    startTimerHz (45); // the rate every other face's live feed runs at
}

PeakModulationWebEditor::~PeakModulationWebEditor()
{
    stopTimer();
}

void PeakModulationWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("modL", processorRef.filterModL.load (std::memory_order_relaxed));
    payload->setProperty ("modR", processorRef.filterModR.load (std::memory_order_relaxed));
    webView.emitEventIfBrowserIsVisible ("filterMod", juce::var (payload));
}

void PeakModulationWebEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> PeakModulationWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (PEAKMODULATION_JSUI_DIR), url);
}
