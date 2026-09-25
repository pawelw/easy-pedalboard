#include "BitBitAlpineWebEditor.h"

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

/** juce::String (v, 3) has no width cap, so a stage peak that has run away to
    something in the 1e20+ range (finite, but not audio - see AlpineWatchdog's
    incident reports) prints dozens of digits and runs straight into the next
    column with no separator, reading as one garbled number instead of two.
    Scientific notation once the fixed form would not stay inside the column
    keeps every field legible and the table aligned. */
juce::String formatPeak (float v)
{
    if (std::abs (v) < 1.0e7f)
        return juce::String (v, 3);

    return juce::String (v, 3, true);
}
} // namespace

#if JUCE_ANDROID
const juce::String BitBitAlpineWebEditor::devServerAddress = "http://10.0.2.2:3002/";
#else
const juce::String BitBitAlpineWebEditor::devServerAddress = "http://localhost:3002/";
#endif

bool BitBitAlpineWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == BitBitAlpineWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

bool BitBitAlpineWebEditor::SinglePageBrowser::pageLoadHadNetworkError (const juce::String&)
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

BitBitAlpineWebEditor::BitBitAlpineWebEditor (BitBitAlpineProcessor& p)
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
                  // WebView engine actually rendering it. installResizableFace
                  // only ever calls this once, with the page's unscaled size -
                  // that measurement becomes the window's design size, so it is
                  // what the resize range and locked aspect ratio are set from.
                  .withNativeFunction (
                      "reportContentSize",
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
                              setResizeLimits (
                                  juce::roundToInt (baseWidth * kMinZoom), juce::roundToInt (baseHeight * kMinZoom),
                                  juce::roundToInt (baseWidth * kMaxZoom), juce::roundToInt (baseHeight * kMaxZoom));

                              if (auto* c = getConstrainer())
                                  c->setFixedAspectRatio ((double)baseWidth / (double)baseHeight);

                              resizeGrip = std::make_unique<ee::plugin::CornerResizer> (*this, *getConstrainer());
                              addAndMakeVisible (*resizeGrip);
                          }
#if EE_ALPINE_WATCHDOG
                          setSize (w + (watchdogPanel != nullptr ? AlpineWatchdogPanel::preferredWidth : 0), h);
#else
                                           setSize (w, h);
#endif
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
                                           else if (queried == id::modFilterTime)
                                               text = processorRef.filterTimeReadout();
                                           else if (queried == id::modTremRate)
                                               text = processorRef.tremoloRateReadout();
                                           else if (auto* param = processorRef.apvts.getParameter (queried))
                                               text = param->getCurrentValueAsText();

                                           complete (text);
                                       })
                  // The TapScope's time axis. Numbers, not the formatted
                  // readouts above: the scope places taps at multiples of the
                  // delay time, and what a knob position means in milliseconds
                  // depends on the Sync pill and the host tempo, neither of
                  // which the web view knows.
                  // Which binary is actually running, so a rebuild can be told
                  // apart from a stale one still loaded in an open project or
                  // a DAW's own plugin cache - see useJuceBuildInfo. __DATE__ /
                  // __TIME__ are this translation unit's own compile time, so
                  // the stamp only moves when this file (or a header it
                  // depends on - PluginProcessor.h, and through it every DSP
                  // header) is actually recompiled.
                  .withNativeFunction (
                      "getBuildInfo",
                      [] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
                      { complete (juce::String (__DATE__) + " " + __TIME__); })
                  // The header's tuner - see tunerState(). Opening it is what
                  // starts the processor capturing; the mute only ever acts
                  // while it is open.
                  .withNativeFunction ("tunerSetOpen",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           const bool open = args.size() > 0 && static_cast<bool> (args[0]);
                                           if (open && ! processorRef.tuner.isActive())
                                           {
                                               tunerAnalyser.reset();
                                               lastTunerFrameMs = juce::Time::getMillisecondCounterHiRes();
                                           }
                                           processorRef.tuner.setActive (open);
                                           complete (tunerState());
                                       })
                  // The pre-EQ dialog's spectrum: capture runs only while it
                  // is open, and the timer below sends a frame per tick.
                  .withNativeFunction ("eqSpectrumSetOpen",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           const bool open = args.size() > 0 && static_cast<bool> (args[0]);
                                           if (open && ! processorRef.eqSpectrum.isActive())
                                           {
                                               eqAnalyser.reset();
                                               lastSpectrumFrameMs = juce::Time::getMillisecondCounterHiRes();
                                           }
                                           processorRef.eqSpectrum.setActive (open);
                                           complete (open);
                                       })
                  .withNativeFunction ("tunerSetMute",
                                       [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                       {
                                           processorRef.tunerMute.store (args.size() > 0 && static_cast<bool> (args[0]),
                                                                         std::memory_order_relaxed);
                                           complete (tunerState());
                                       })
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
    // is exact - the host panel measures 1154 (a 14px frame and a 1px border
    // either side of a row of 180 + 8 + 180 + 8 + 560 + 8 + 180) plus the
    // page's 4px either side - and the height is a close guess, so the host
    // sees at most a small vertical correction rather than a window that
    // visibly jumps.
    setSize (1162, 590);
    setResizable (false, false);

    startTimerHz (45); // the rate every other face's live feed runs at

#if EE_ALPINE_WATCHDOG
    watchdogLog = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                      .getChildFile ("Library/Logs/BitBit/BitBitAlpine-watchdog.log");
    watchdogLog.getParentDirectory().createDirectory();

    // Same walk of getParameters() the recorder does, so index i here is the
    // same parameter as paramValue[i] in a frozen incident.
    for (auto* ap : processorRef.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (ap))
            watchdogParamIds.add (withId->paramID);
        else
            watchdogParamIds.add ("#" + juce::String (watchdogParamIds.size()));
    }

    watchdogPanel = std::make_unique<AlpineWatchdogPanel> (watchdogLog);
    addAndMakeVisible (*watchdogPanel);
    setSize (getWidth() + AlpineWatchdogPanel::preferredWidth, getHeight());
#endif
}

BitBitAlpineWebEditor::~BitBitAlpineWebEditor()
{
    stopTimer();

    // A tuner left open when the window closes would keep capturing, and keep
    // the output muted, with nothing on screen to say so or turn it off.
    processorRef.tuner.setActive (false);
    processorRef.eqSpectrum.setActive (false);

    // Still the active editor here, so this is the last moment a change made
    // in it counts as the user's - see GlobalEq.
    processorRef.globalEq.flush();
}

juce::var BitBitAlpineWebEditor::tunerState() const
{
    auto* state = new juce::DynamicObject();
    state->setProperty ("open", processorRef.tuner.isActive());
    state->setProperty ("mute", processorRef.tunerMute.load (std::memory_order_relaxed));
    return juce::var (state);
}

void BitBitAlpineWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("level", processorRef.inputMeter.getLevel());
    payload->setProperty ("strikes", processorRef.inputMeter.getStrikes());
    payload->setProperty ("bpm", processorRef.hostBpm());
    webView.emitEventIfBrowserIsVisible ("delayMeter", juce::var (payload));

    // The Modulation module's Filter response scope rides on this - the same
    // event name BitBit Wah's editor feeds its own scope under.
    auto* filterPayload = new juce::DynamicObject();
    filterPayload->setProperty ("modL", processorRef.filterModL.load (std::memory_order_relaxed));
    filterPayload->setProperty ("modR", processorRef.filterModR.load (std::memory_order_relaxed));
    webView.emitEventIfBrowserIsVisible ("filterMod", juce::var (filterPayload));

    // The Artifact module's Comp display, only while Comp is its engine - the
    // same "compMeter" event BitBit Artifact's editor sends.
    {
        const auto raw = [this] (const char* pid) { return processorRef.apvts.getRawParameterValue (pid)->load(); };
        const bool comp = static_cast<int> (raw (id::artEngine)) == ee::fx::ArtifactModule::Comp;

        const auto compPayload = compMeter.poll (processorRef.artifactModule().compressor(), comp && isShowing(),
                                                 ee::dsp::comp::inputThresholdDbFor (raw (id::artCompSensitivity) * 0.01f));

        if (! compPayload.isVoid())
            webView.emitEventIfBrowserIsVisible ("compMeter", compPayload);
    }

    if (processorRef.tuner.isActive())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dt = juce::jlimit (0.001, 0.25, (now - lastTunerFrameMs) * 0.001);
        lastTunerFrameMs = now;

        const auto r = tunerAnalyser.update (processorRef.tuner, dt);

        auto* tunerPayload = new juce::DynamicObject();
        tunerPayload->setProperty ("signal", r.signal);
        tunerPayload->setProperty ("holding", r.holding);
        tunerPayload->setProperty ("note", juce::String (ee::dsp::TunerReading::noteName (r.midiNote)));
        tunerPayload->setProperty ("octave", ee::dsp::TunerReading::octave (r.midiNote));
        tunerPayload->setProperty ("cents", r.cents);
        tunerPayload->setProperty ("hz", r.hz);
        tunerPayload->setProperty ("mute", processorRef.tunerMute.load (std::memory_order_relaxed));
        webView.emitEventIfBrowserIsVisible ("tuner", juce::var (tunerPayload));
    }

    if (processorRef.eqSpectrum.isActive())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dt = juce::jlimit (0.0, 0.25, (now - lastSpectrumFrameMs) * 0.001);
        lastSpectrumFrameMs = now;

        const auto& frame = eqAnalyser.update (processorRef.eqSpectrum, dt);

        juce::Array<juce::var> db;
        db.ensureStorageAllocated (static_cast<int> (frame.size()));
        for (float v : frame)
            db.add (std::round (v * 10.0f) * 0.1f);

        auto* spectrumPayload = new juce::DynamicObject();
        spectrumPayload->setProperty ("db", db);
        webView.emitEventIfBrowserIsVisible ("eqSpectrum", juce::var (spectrumPayload));
    }

#if EE_ALPINE_WATCHDOG
    pollWatchdog();
#endif
}

void BitBitAlpineWebEditor::resized()
{
#if EE_ALPINE_WATCHDOG
    if (watchdogPanel != nullptr)
    {
        auto bounds = getLocalBounds();
        watchdogPanel->setBounds (bounds.removeFromRight (AlpineWatchdogPanel::preferredWidth));
        webView.setBounds (bounds);
    }
    else
#endif
    {
        webView.setBounds (getLocalBounds());
    }

    if (resizeGrip != nullptr)
    {
        resizeGrip->setBounds (0, getHeight() - ee::plugin::CornerResizer::kSize, ee::plugin::CornerResizer::kSize,
                                ee::plugin::CornerResizer::kSize);
        resizeGrip->toFront (false);
    }
}

std::optional<juce::WebBrowserComponent::Resource> BitBitAlpineWebEditor::getResource (const juce::String& url)
{
    return ee::plugin::webface::serveFromDist (juce::File (BITBITALPINE_JSUI_DIR), url);
}

#if EE_ALPINE_WATCHDOG
void BitBitAlpineWebEditor::pollWatchdog()
{
    const auto refreshStatus = [this]
    {
        if (watchdogPanel != nullptr)
            watchdogPanel->setCounters (watchdogIncidents, processorRef.safetyTrips.load (std::memory_order_relaxed),
                                        processorRef.safetyResets.load (std::memory_order_relaxed),
                                        processorRef.lastOutputPeak.load (std::memory_order_relaxed));
    };

    AlpineWatchdog::Incident inc;
    if (! processorRef.watchdog.readIncident (lastWatchdogSeq, inc))
    {
        refreshStatus();
        return;
    }

    ++watchdogIncidents;

    const juce::String report = formatIncident (inc);
    watchdogLog.appendText (report + juce::newLine + juce::newLine, false, false, "\n");

    if (watchdogPanel != nullptr)
        watchdogPanel->showReport (report);

    refreshStatus();
}

juce::String BitBitAlpineWebEditor::formatIncident (const AlpineWatchdog::Incident& inc) const
{
    juce::String t;

    t << "=== BitBit Alpine watchdog incident ===" << juce::newLine;
    t << "when         " << juce::Time (inc.timeMs).toString (true, true, true, true) << juce::newLine;
    t << "sample rate  " << juce::String (inc.sampleRate, 0) << " Hz    block size  " << inc.blockSize << juce::newLine;
    t << "transport    " << (inc.playing ? "playing" : "stopped") << "    bpm  " << juce::String (inc.bpm, 2)
      << juce::newLine;
    t << "at block     " << static_cast<juce::int64> (inc.atBlock) << juce::newLine << juce::newLine;

    t << "last blocks up to the trip (peak level at each stage output, oldest first):" << juce::newLine;
    t << juce::String ("block").paddedLeft (' ', 9);
    for (int s = 0; s < AlpineWatchdog::numStages; ++s)
        t << juce::String (AlpineWatchdog::stageName (s)).paddedLeft (' ', 10);
    t << juce::String ("out").paddedLeft (' ', 10) << "   flags" << juce::newLine;

    const int show = juce::jmin (48, inc.frameCount);
    int idx = (inc.ringHead - show + AlpineWatchdog::kRingFrames) % AlpineWatchdog::kRingFrames;

    for (int n = 0; n < show; ++n)
    {
        const auto& f = inc.ring[static_cast<size_t> (idx)];
        idx = (idx + 1) % AlpineWatchdog::kRingFrames;

        t << juce::String (static_cast<juce::int64> (f.block)).paddedLeft (' ', 9);
        for (int s = 0; s < AlpineWatchdog::numStages; ++s)
            t << formatPeak (f.stagePeak[static_cast<size_t> (s)]).paddedLeft (' ', 10);
        t << formatPeak (f.outPeak).paddedLeft (' ', 10) << "   ";

        if ((f.flags & 1) != 0)
            t << "NON-FINITE ";
        if ((f.flags & 2) != 0)
            t << "CLAMPED ";
        if ((f.flags & 4) != 0)
            t << "MODULE-RESET ";
        t << juce::newLine;
    }

    t << juce::newLine << "parameters at the trip:" << juce::newLine;
    for (int i = 0; i < inc.numParams && i < watchdogParamIds.size(); ++i)
    {
        const auto& id = watchdogParamIds[i];
        juce::String value;
        if (auto* p = processorRef.apvts.getParameter (id))
            value = p->getText (inc.paramValue[static_cast<size_t> (i)], 0);

        t << "  " << id.paddedRight (' ', 16) << " " << value.paddedRight (' ', 10) << "  ("
          << juce::String (inc.paramValue[static_cast<size_t> (i)], 4) << ")" << juce::newLine;
    }

    return t;
}
#endif
