#include "BitBitModulationWebEditor.h"

#include "Params.h"
#include "PluginProcessor.h"
#include "ee/plugin/PresetBridge.h"
#include "ee/plugin/WebFace.h"

// Port 3004 - Wah is 3000, Delay 3001, Alpine 3002, Artifact 3003, Reverb 3005.
#if JUCE_ANDROID
const juce::String BitBitModulationWebEditor::devServerAddress = "http://10.0.2.2:3004/";
#else
const juce::String BitBitModulationWebEditor::devServerAddress = "http://localhost:3004/";
#endif

bool BitBitModulationWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == BitBitModulationWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool BitBitModulationWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
{
    if (triedFallback)
        return true;

    triedFallback = true;
    goToURL (getResourceProviderRoot());
    return false;
}

BitBitModulationWebEditor::BitBitModulationWebEditor (BitBitModulationProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), relays (p.apvts),
      webView (ee::plugin::presetBridge (
          relays.apply (
              juce::WebBrowserComponent::Options {}
                  .withNativeIntegrationEnabled()
                  // Suppress WKWebView's own right-click menu the ordinary web
                  // way - it has no dedicated JUCE option - so a knob drag that
                  // starts with the right button does not pop it up.
                  .withUserScript ("document.addEventListener('contextmenu', function (e) { e.preventDefault(); });")
                  // See installResizableFace: the page measures its own real
                  // rendered size once and reports it here, which becomes
                  // this window's design size - what the resize range and
                  // locked aspect ratio are set from, same as BitBitAlpine's.
                  .withNativeFunction ("reportContentSize",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           const int w = juce::jmax (100, static_cast<int> (args[0]));
                                           const int h = juce::jmax (100, static_cast<int> (args[1]));

                                           if (baseWidth <= 0)
                                           {
                                               baseWidth = w;
                                               baseHeight = h;

                                               setResizable (true, false);
                                               setResizeLimits (juce::roundToInt (baseWidth * kMinZoom),
                                                                 juce::roundToInt (baseHeight * kMinZoom),
                                                                 juce::roundToInt (baseWidth * kMaxZoom),
                                                                 juce::roundToInt (baseHeight * kMaxZoom));

                                               if (auto* c = getConstrainer())
                                                   c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

                                               resizeGrip =
                                                   std::make_unique<ee::plugin::CornerResizer> (*this, *getConstrainer());
                                               addAndMakeVisible (*resizeGrip);
                                           }

                                           setSize (w, h);
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

BitBitModulationWebEditor::~BitBitModulationWebEditor()
{
    stopTimer();
}

void BitBitModulationWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("modL", processorRef.filterModL.load (std::memory_order_relaxed));
    payload->setProperty ("modR", processorRef.filterModR.load (std::memory_order_relaxed));
    webView.emitEventIfBrowserIsVisible ("filterMod", juce::var (payload));
}

void BitBitModulationWebEditor::resized()
{
    webView.setBounds (getLocalBounds());

    if (resizeGrip != nullptr)
    {
        resizeGrip->setBounds (0, getHeight() - ee::plugin::CornerResizer::kSize, ee::plugin::CornerResizer::kSize,
                                ee::plugin::CornerResizer::kSize);
        resizeGrip->toFront (false);
    }
}

std::optional<juce::WebBrowserComponent::Resource> BitBitModulationWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (BITBITMODULATION_JSUI_DIR), url);
}
