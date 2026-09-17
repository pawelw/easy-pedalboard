#include "PeakGrainWebEditor.h"

#include "PluginProcessor.h"
#include "ee/plugin/PresetBridge.h"
#include "ee/plugin/WebFace.h"

namespace
{
constexpr const char* kParamSize = "size";
constexpr const char* kParamDensity = "density";
constexpr const char* kParamWindow = "window";
constexpr const char* kParamLeftTime = "ltime";
constexpr const char* kParamRightTime = "rtime";
constexpr const char* kParamLfoRate = "lforate";
} // namespace

// Port 3006 - Wah is 3000, Delay 3001, Alpine 3002, Artifact 3003, Modulation
// 3004, Reverb 3005.
#if JUCE_ANDROID
const juce::String PeakGrainWebEditor::devServerAddress = "http://10.0.2.2:3006/";
#else
const juce::String PeakGrainWebEditor::devServerAddress = "http://localhost:3006/";
#endif

bool PeakGrainWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == PeakGrainWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool PeakGrainWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
{
    if (triedFallback)
        return true;

    triedFallback = true;
    goToURL (getResourceProviderRoot());
    return false;
}

PeakGrainWebEditor::PeakGrainWebEditor (PeakGrainProcessor& p)
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
                                           const int width = juce::jmax (100, static_cast<int> (args[0]));
                                           const int height = juce::jmax (100, static_cast<int> (args[1]));
#if EE_GRAIN_TUNER
                                           const int panelWidth =
                                               tunerPanel != nullptr ? GrainTunerPanel::preferredWidth : 0;
#else
                                           const int panelWidth = 0;
#endif
                                           setSize (width + panelWidth, height);
                                           complete (true);
                                       })
                  // A knob's printed value. Size/Density/Window/the two delay
                  // times are tempo-synced - their text depends on a Sync
                  // switch and the host tempo, neither of which the
                  // parameter's own stringFromValue (fixed at construction)
                  // can see - so those five are special-cased onto the
                  // processor's live readouts; everything else falls through
                  // to the parameter's own text.
                  .withNativeFunction ("formatKnobValue",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           const auto id = args[0].toString();
                                           juce::String text;

                                           if (id == kParamSize)
                                               text = processorRef.sizeReadout();
                                           else if (id == kParamDensity)
                                               text = processorRef.densityReadout();
                                           else if (id == kParamWindow)
                                               text = processorRef.windowReadout();
                                           else if (id == kParamLeftTime)
                                               text = processorRef.leftTimeReadout();
                                           else if (id == kParamRightTime)
                                               text = processorRef.rightTimeReadout();
                                           else if (id == kParamLfoRate)
                                               text = processorRef.lfoRateReadout();
                                           else if (auto* param = processorRef.apvts.getParameter (id))
                                               text = param->getCurrentValueAsText();

                                           complete (text);
                                       })
                  // The Mod tab's breakpoint shape: not a parameter (see
                  // PluginProcessor.cpp's kLfoBreakpointsProp), so it needs
                  // its own pair of native functions rather than riding on
                  // RelaySet - one to read the current shape on mount, one
                  // for every edit the JS editor commits.
                  .withNativeFunction ("lfoGetBreakpoints",
                                       [this] (const juce::Array<juce::var>&,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       { complete (processorRef.lfoBreakpointsAsJson()); })
                  .withNativeFunction ("lfoSetBreakpoints",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           processorRef.setLfoBreakpointsFromJson (
                                               args.size() >= 1 ? args[0].toString() : juce::String());
                                           complete (true);
                                       })
                  // The drag-and-drop modulation routing: same shape as the
                  // breakpoints pair above, and for the same reason - not a
                  // parameter, so RelaySet has nothing to bind here.
                  .withNativeFunction ("lfoRoutingGet",
                                       [this] (const juce::Array<juce::var>&,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       { complete (processorRef.lfoRoutingAsJson()); })
                  .withNativeFunction ("lfoRoutingSet",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           processorRef.setLfoRoutingFromJson (
                                               args.size() >= 1 ? args[0].toString() : juce::String());
                                           complete (true);
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
    setSize (588, 620);
    setResizable (false, false);

    startTimerHz (10); // just fast enough that a live tempo change isn't stale for long

#if EE_GRAIN_TUNER
    // Flip to true to bring the tuning panel back without reconfiguring CMake.
    constexpr bool showTuner = false;

    if (showTuner)
    {
        // The tuner used to also push verbLowCutHz straight to the reverb
        // engine; Low Cut is a real face knob now (processBlock owns it every
        // block), so the tuner's own apply callback only touches the rest of
        // the voicing.
        tunerPanel = std::make_unique<GrainTunerPanel> (processorRef.tuning(), [this] (const ee::dsp::GrainerTuning& t)
                                                        { processorRef.setTuning (t); });
        addAndMakeVisible (*tunerPanel);
        setSize (getWidth() + GrainTunerPanel::preferredWidth, getHeight());
    }
#endif
}

PeakGrainWebEditor::~PeakGrainWebEditor()
{
    stopTimer();
}

void PeakGrainWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("bpm", processorRef.hostBpm());
    webView.emitEventIfBrowserIsVisible ("grainTempo", juce::var (payload));

    webView.emitEventIfBrowserIsVisible ("lfoPhase", processorRef.lfoPhase01());

    const int generation = processorRef.lfoStateGeneration();
    if (generation != lastLfoGeneration)
    {
        lastLfoGeneration = generation;
        webView.emitEventIfBrowserIsVisible ("lfoBreakpoints", processorRef.lfoBreakpointsAsJson());
        webView.emitEventIfBrowserIsVisible ("lfoRouting", processorRef.lfoRoutingAsJson());
    }
}

void PeakGrainWebEditor::resized()
{
#if EE_GRAIN_TUNER
    if (tunerPanel != nullptr)
    {
        auto bounds = getLocalBounds();
        tunerPanel->setBounds (bounds.removeFromRight (GrainTunerPanel::preferredWidth));
        webView.setBounds (bounds);
        return;
    }
#endif
    webView.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> PeakGrainWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (PEAKGRAIN_JSUI_DIR), url);
}
