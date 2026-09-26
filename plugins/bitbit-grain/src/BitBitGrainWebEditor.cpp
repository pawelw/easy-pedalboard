#include "BitBitGrainWebEditor.h"

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
const juce::String BitBitGrainWebEditor::devServerAddress = "http://10.0.2.2:3006/";
#else
const juce::String BitBitGrainWebEditor::devServerAddress = "http://localhost:3006/";
#endif

bool BitBitGrainWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == BitBitGrainWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool BitBitGrainWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
{
    if (triedFallback)
        return true;

    triedFallback = true;
    goToURL (getResourceProviderRoot());
    return false;
}

BitBitGrainWebEditor::BitBitGrainWebEditor (BitBitGrainProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), relays (p.apvts),
      webView (ee::plugin::presetBridge (
          relays.apply (
              resizeGrip.bridge (juce::WebBrowserComponent::Options {})
                  .withNativeIntegrationEnabled()
                  // Suppress WKWebView's own right-click menu the ordinary web
                  // way - it has no dedicated JUCE option - so a knob drag that
                  // starts with the right button does not pop it up.
                  .withUserScript ("document.addEventListener('contextmenu', function (e) { e.preventDefault(); });")
                  // See installResizableFace: the page measures its own real
                  // rendered size once and reports it here, which becomes
                  // this window's design size - what the resize range and
                  // locked aspect ratio are set from, same as BitBitAlpine's
                  // (whose own watchdog panel is the same dev-only shape as
                  // this pedal's tuner one, added the same way below).
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
                                           if (baseWidth <= 0)
                                           {
                                               baseWidth = width;
                                               baseHeight = height;

                                               setResizable (true, false);
                                               setResizeLimits (juce::roundToInt (baseWidth * kMinZoom),
                                                                juce::roundToInt (baseHeight * kMinZoom),
                                                                juce::roundToInt (baseWidth * kMaxZoom),
                                                                juce::roundToInt (baseHeight * kMaxZoom));

                                               if (auto* c = getConstrainer())
                                                   c->setFixedAspectRatio ((double)baseWidth / (double)baseHeight);
                                           }

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
                                           processorRef.setLfoBreakpointsFromJson (args.size() >= 1 ? args[0].toString()
                                                                                                    : juce::String());
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
                                           processorRef.setLfoRoutingFromJson (args.size() >= 1 ? args[0].toString()
                                                                                                : juce::String());
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

    startTimerHz (30); // 10 Hz would do for the tempo readouts; the cosmos panel wants the grain feed livelier

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

BitBitGrainWebEditor::~BitBitGrainWebEditor()
{
    stopTimer();
}

void BitBitGrainWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("bpm", processorRef.hostBpm());
    webView.emitEventIfBrowserIsVisible ("grainTempo", juce::var (payload));

    webView.emitEventIfBrowserIsVisible ("lfoPhase", processorRef.lfoPhase01());

    // The cosmos panel's feed: every grain born since the last tick, plus the
    // output level. Sent at the timer's rate, so the page smooths between. Only
    // while the editor is actually on screen: a hidden or backgrounded window
    // would otherwise be handed a stream nothing is drawing, and show it all at
    // once when it comes back.
    if (! isShowing())
    {
        lastGrainEvent = processorRef.grainEngine().grainEventCount();
    }
    else
    {
        const auto& engine = processorRef.grainEngine();
        const uint32_t count = engine.grainEventCount();

        // Fell a whole ring behind (editor hidden, host stalled): skip to the
        // oldest event still intact rather than replaying overwritten ones.
        if (count - lastGrainEvent > ee::dsp::Grainer::kGrainEventRing)
            lastGrainEvent = count - ee::dsp::Grainer::kGrainEventRing;

        juce::Array<juce::var> grains;
        for (; lastGrainEvent != count && grains.size() < 48; ++lastGrainEvent)
        {
            const auto e = engine.grainEventAt (lastGrainEvent);
            juce::Array<juce::var> row;
            row.add (e.octaves);
            row.add (e.pan);
            row.add (e.level);
            row.add (e.seconds);
            row.add (e.backwards ? 1 : 0);
            row.add (e.spread);
            grains.add (juce::var (row));
        }

        auto* cosmos = new juce::DynamicObject();
        cosmos->setProperty ("grains", grains);
        cosmos->setProperty ("level", processorRef.outputLevel());
        cosmos->setProperty ("dry", processorRef.dryOutputLevel());
        webView.emitEventIfBrowserIsVisible ("grainCosmos", juce::var (cosmos));
    }

    const int generation = processorRef.lfoStateGeneration();
    if (generation != lastLfoGeneration)
    {
        lastLfoGeneration = generation;
        webView.emitEventIfBrowserIsVisible ("lfoBreakpoints", processorRef.lfoBreakpointsAsJson());
        webView.emitEventIfBrowserIsVisible ("lfoRouting", processorRef.lfoRoutingAsJson());
    }
}

void BitBitGrainWebEditor::resized()
{
    auto bounds = getLocalBounds();

#if EE_GRAIN_TUNER
    if (tunerPanel != nullptr)
        tunerPanel->setBounds (bounds.removeFromRight (GrainTunerPanel::preferredWidth));
#endif

    webView.setBounds (bounds);
}

std::optional<juce::WebBrowserComponent::Resource> BitBitGrainWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (BITBITGRAIN_JSUI_DIR), url);
}
