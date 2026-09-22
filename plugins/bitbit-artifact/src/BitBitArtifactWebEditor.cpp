#include "BitBitArtifactWebEditor.h"

#include "Params.h"
#include "PluginProcessor.h"
#include "ee/plugin/PresetBridge.h"
#include "ee/plugin/WebFace.h"

#if JUCE_ANDROID
const juce::String BitBitArtifactWebEditor::devServerAddress = "http://10.0.2.2:3003/";
#else
const juce::String BitBitArtifactWebEditor::devServerAddress = "http://localhost:3003/";
#endif

bool BitBitArtifactWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == BitBitArtifactWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool BitBitArtifactWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
{
    if (triedFallback)
        return true;

    triedFallback = true;
    goToURL (getResourceProviderRoot());
    return false;
}

BitBitArtifactWebEditor::BitBitArtifactWebEditor (BitBitArtifactProcessor& p)
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
                  // only start/end/skew/interval, not the format string.
                  .withNativeFunction ("formatKnobValue",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           const auto queried = args[0].toString();
                                           juce::String text;

                                           if (auto* param = processorRef.apvts.getParameter (queried))
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
}

BitBitArtifactWebEditor::~BitBitArtifactWebEditor() = default;

void BitBitArtifactWebEditor::resized()
{
    webView.setBounds (getLocalBounds());

    if (resizeGrip != nullptr)
    {
        resizeGrip->setBounds (0, getHeight() - ee::plugin::CornerResizer::kSize, ee::plugin::CornerResizer::kSize,
                                ee::plugin::CornerResizer::kSize);
        resizeGrip->toFront (false);
    }
}

std::optional<juce::WebBrowserComponent::Resource> BitBitArtifactWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (BITBITARTIFACT_JSUI_DIR), url);
}
