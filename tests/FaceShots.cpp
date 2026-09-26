// ee_face_shots_<Target> - renders a shipping product's real face to PNG, with a
// transparent background, for the website.
//
// Not the ee_ui_snapshot route (a throwaway processor painting a native face
// into an Image): the six products sold are WebView faces, and a WKWebView does
// not paint through juce::Graphics at all. So this does what a host does -
// the real processor, its real editor in a real window, audio running through
// it in real time from a second thread - and then asks WebKit itself for a
// snapshot of the page. Every printed value is the processor's own text, and
// the tuner, the EQ's spectrum, the delay scope and the comp meter show what
// they show because signal is actually going through.
//
// What to shoot lives in scripts/face-shots.json, keyed by target name: which
// preset to load, parameters to pin, and a list of shots, each an optional
// click on the page plus optional isolated crops (a dialog on its own). Every
// shot is taken once per theme. scripts/face-shots.sh builds all six and runs
// them; see that file for the everyday invocation.
//
// Knobs a shot would otherwise catch at rest - at their default, or pinned to
// either end - are turned to a position seeded from the parameter id, so the
// face looks played and a rerun produces the same picture.
//
// One binary per product, for the duplicate-createPluginFilter() reason in
// tests/CMakeLists.txt.

#include "PluginProcessor.h"

#include "FaceShotsMac.h"
#include "ee/plugin/SafeParse.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <optional>

#ifndef EE_SHOTS_GRAIN
#define EE_SHOTS_GRAIN 0
#endif

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;

//==============================================================================
/** Something to play into the pedal: a guitar-ish arpeggio for the ordinary
    shots, and one held note for the tuner, re-plucked before it dies so the
    reading never drops out. Additive plucked strings, not samples, so there is
    nothing to ship and every run is the same. */
class TestGuitar
{
public:
    /** 0 is the arpeggio; anything else is the tuner's note, in Hz. */
    void setTunerNote (double hz) { tunerHz.store (hz); }

    float next()
    {
        const double held = tunerHz.load();

        if (held != currentHeld)
        {
            currentHeld = held;
            voices = {};
            samplesToNext = 0;
        }

        if (--samplesToNext <= 0)
        {
            if (currentHeld > 0.0)
            {
                pluck (currentHeld, 5.0);
                samplesToNext = (int)(kSampleRate * 1.6);
            }
            else
            {
                // An open Em9-ish figure, low to high and back.
                static constexpr double notes[] = { 82.41, 123.47, 185.0, 246.94, 293.66, 369.99, 293.66, 185.0 };
                pluck (notes[step++ % std::size (notes)], 1.4);
                samplesToNext = (int)(kSampleRate * 0.28);
            }
        }

        double out = 0.0;

        for (auto& v : voices)
        {
            if (v.amp < 1.0e-5)
                continue;

            for (int k = 0; k < kHarmonics; ++k)
            {
                out += v.amp * v.partialAmp[(size_t)k] * std::sin (v.phase[(size_t)k]);
                v.phase[(size_t)k] += v.inc[(size_t)k];
                v.partialAmp[(size_t)k] *= v.partialDecay[(size_t)k];
            }

            v.amp *= v.decay;
        }

        return (float)(out * 0.12);
    }

private:
    static constexpr int kHarmonics = 8;
    static constexpr int kVoices = 6;

    struct Voice
    {
        double amp = 0.0, decay = 1.0;
        std::array<double, kHarmonics> phase {}, inc {}, partialAmp {}, partialDecay {};
    };

    void pluck (double hz, double seconds)
    {
        auto& v = voices[(size_t)(nextVoice++ % kVoices)];
        v.amp = 1.0;
        v.decay = std::exp (-1.0 / (seconds * kSampleRate));

        for (int k = 0; k < kHarmonics; ++k)
        {
            const double n = k + 1;
            v.phase[(size_t)k] = 0.0;
            v.inc[(size_t)k] = juce::MathConstants<double>::twoPi * hz * n / kSampleRate;
            v.partialAmp[(size_t)k] = 1.0 / n;
            // Upper partials die first, the way a string's do.
            v.partialDecay[(size_t)k] = std::exp (-(n - 1.0) * 0.6 / kSampleRate);
        }
    }

    std::atomic<double> tunerHz { 0.0 };
    double currentHeld = 0.0;
    std::array<Voice, kVoices> voices {};
    int nextVoice = 0;
    int samplesToNext = 0;
    size_t step = 0;
};

//==============================================================================
/** A transport that is always playing at 120 bpm: the synced readouts and the
    tempo displays have something to show. */
class RunningPlayHead : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        const auto samples = position.load();
        const double seconds = (double)samples / kSampleRate;

        PositionInfo info;
        info.setBpm (120.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setIsPlaying (true);
        info.setTimeInSamples (samples);
        info.setTimeInSeconds (seconds);
        info.setPpqPosition (seconds * 2.0);
        info.setPpqPositionOfLastBarStart (std::floor (seconds * 2.0 / 4.0) * 4.0);
        return info;
    }

    std::atomic<juce::int64> position { 0 };
};

/** The audio thread: processBlock at the rate a host would call it, so every
    meter and analyser on the face is fed at its real speed. */
class AudioPump : public juce::Thread
{
public:
    AudioPump (juce::AudioProcessor& p, RunningPlayHead& ph)
        : juce::Thread ("face-shots audio"), processor (p), playHead (ph)
    {
    }

    TestGuitar guitar;

    void run() override
    {
        const int channels =
            juce::jmax (2, processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (channels, kBlockSize);
        juce::MidiBuffer midi;

        const double blockMs = 1000.0 * kBlockSize / kSampleRate;
        double deadline = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            buffer.clear();
            for (int i = 0; i < kBlockSize; ++i)
            {
                const float s = guitar.next();
                for (int ch = 0; ch < juce::jmin (2, channels); ++ch)
                    buffer.setSample (ch, i, s);
            }

            processor.processBlock (buffer, midi);
            midi.clear();
            playHead.position += kBlockSize;

            deadline += blockMs;
            const double wait = deadline - juce::Time::getMillisecondCounterHiRes();
            if (wait > 1.0)
                juce::Thread::sleep ((int)wait);
            else if (wait < -250.0)
                deadline = juce::Time::getMillisecondCounterHiRes(); // fell behind: don't try to catch up
        }
    }

private:
    juce::AudioProcessor& processor;
    RunningPlayHead& playHead;
};

//==============================================================================
void fail (const juce::String& message)
{
    std::fprintf (stderr, "ee_face_shots: %s\n", message.toRawUTF8());
    std::exit (1);
}

void setParameter (juce::RangedAudioParameter& p, float normalised)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    p.endChangeGesture();
}

/** `"set"` in the config: a number is in the parameter's own units (a choice's
    index), a bool is a bool, and a string is a choice name or the parameter's
    own text. */
void applySetting (juce::AudioProcessor& processor, const juce::String& id, const juce::var& value)
{
    juce::RangedAudioParameter* param = nullptr;
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
            ranged != nullptr && ranged->getParameterID() == id)
            param = ranged;

    if (param == nullptr)
        fail ("\"set\" names a parameter this product does not have: " + id);

    float normalised = 0.0f;

    if (value.isBool())
        normalised = (bool)value ? 1.0f : 0.0f;
    else if (value.isString())
    {
        const auto text = value.toString();
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            int index = -1;
            for (int i = 0; i < choice->choices.size(); ++i)
                if (choice->choices[i].equalsIgnoreCase (text))
                    index = i;

            if (index < 0)
                fail (id + " has no choice \"" + text + "\" (it has: " + choice->choices.joinIntoString (", ") + ")");

            normalised = param->convertTo0to1 ((float)index);
        }
        else
            normalised = param->getValueForText (text);
    }
    else
        normalised = param->convertTo0to1 ((float)(double)value);

    setParameter (*param, normalised);
}

/** Turns every knob caught at rest to a position seeded from its id: at its
    default, or at either end of its travel. Leaves alone what the config pinned,
    the level trims (they are faders, and a trim at unity is what a player
    leaves it at) and BitBit Alpine's pre-EQ, which has its own shot. */
void turnRestingKnobs (juce::AudioProcessor& processor, const juce::StringArray& pinned)
{
    for (auto* p : processor.getParameters())
    {
        auto* knob = dynamic_cast<juce::AudioParameterFloat*> (p);
        if (knob == nullptr)
            continue;

        const auto id = knob->getParameterID();
        const auto leaf = id.fromLastOccurrenceOf (".", false, false);

        if (pinned.contains (id) || id.startsWith ("eq.") || leaf == "ingain" || leaf == "outgain" || leaf == "level")
            continue;

        const auto& asParameter = static_cast<const juce::AudioProcessorParameter&> (*knob);
        const float now = asParameter.getValue();
        const float def = asParameter.getDefaultValue();

        if (std::abs (now - def) > 0.02f && now > 0.02f && now < 0.98f)
            continue;

        juce::Random random (id.hashCode64());
        float to = 0.0f;
        do
            to = 0.2f + 0.6f * random.nextFloat();
        while (std::abs (to - def) < 0.12f);

        setParameter (*knob, to);
    }
}

//==============================================================================
/** Installed into the page once it has loaded. Everything the shots do to the
    DOM goes through here, so the C++ only ever sends one-line calls. */
const char* kPageHelper = R"JS(
(() => {
  if (window.__eeShots) return "ok";

  const style = document.createElement("style");
  style.id = "ee-shots";
  style.textContent = `
    html, body, #root, #root > div { background: transparent !important; }
    .pa-build-stamp, #pui-resize-grip { display: none !important; }
    * { caret-color: transparent !important; }`;
  document.head.appendChild(style);

  const isolation = document.createElement("style");
  document.head.appendChild(isolation);

  const card = () => document.querySelector(".pui-card");
  const rect = (el) => {
    const r = el.getBoundingClientRect();
    return { x: r.left, y: r.top, w: r.width, h: r.height };
  };

  window.__eeShots = {
    // Room around the card for its drop shadow, which runs ~64px past it on
    // the dark theme - the page itself leaves 4. The native side grows the
    // window by `margin` each way; the face measured itself once, at load, so
    // all this has to undo is installResizableFace's stretch-to-window scale.
    frame(margin) {
      style.textContent += `
        html, body { overflow: visible !important; }
        body { transform: translate(${margin}px, ${margin}px) !important; }`;
      return "ok";
    },

    // The faces start on onyx and the switch flips to light; the provider
    // stamps data-pui-theme on its wrapper for anything but light.
    theme: () => (document.querySelector("[data-pui-theme]") ? "dark" : "light"),

    setTheme(want) {
      if (this.theme() === want) return want;
      const button = document.querySelector('[aria-label="Switch theme"]');
      if (!button) throw new Error("this face has no theme switch");
      button.click();
      return want;
    },

    click(selector) {
      const el = document.querySelector(selector);
      if (!el) throw new Error("nothing matches " + selector);
      el.click();
      return "ok";
    },

    clickText(text) {
      const want = text.trim().toLowerCase();
      const el = [...document.querySelectorAll("button, [role=tab]")].find(
        (b) => b.textContent.trim().toLowerCase() === want);
      if (!el) throw new Error("no button reads " + text);
      el.click();
      return "ok";
    },

    // A dialog's scrim covers the whole window, which on a transparent shot is
    // a dark rectangle around the pedal. Pulled in to the card's own outline
    // it dims the face and nothing else.
    clipScrims() {
      const c = card();
      if (!c) return "ok";
      const cr = c.getBoundingClientRect();
      const radius = getComputedStyle(c).borderRadius;
      for (const s of document.querySelectorAll('[class*="__scrim"]')) {
        const parent = s.offsetParent || document.body;
        const pr = parent.getBoundingClientRect();
        const scale = parent.offsetWidth ? pr.width / parent.offsetWidth : 1;
        Object.assign(s.style, {
          inset: "auto",
          left: (cr.left - pr.left) / scale + "px",
          top: (cr.top - pr.top) / scale + "px",
          width: cr.width / scale + "px",
          height: cr.height / scale + "px",
          borderRadius: radius,
        });
      }
      return "ok";
    },

    // Hides everything but `selector` (and what is inside it), for a crop of
    // a dialog on its own; "" puts the page back.
    rect(selector) {
      const el = document.querySelector(selector);
      if (!el) throw new Error("nothing matches " + selector);
      return JSON.stringify(rect(el));
    },

    isolate(selector) {
      isolation.textContent = selector
        ? `body * { visibility: hidden !important; }
           ${selector}, ${selector} * { visibility: visible !important; }`
        : "";
      if (!selector) return "null";
      const el = document.querySelector(selector);
      if (!el) throw new Error("nothing matches " + selector);
      return JSON.stringify(rect(el));
    },
  };
  return "ok";
})()
)JS";

/** One face on screen: the editor in its own window, and the web view in it. */
class FaceSession
{
public:
    explicit FaceSession (juce::AudioProcessor& p) : processor (p)
    {
        editor.reset (processor.createEditorAndMakeActive());
        if (editor == nullptr)
            fail ("the processor made no editor");

        editor->setTopLeftPosition (80, 80);
        editor->addToDesktop (0);
        editor->setVisible (true);
        editor->setAlwaysOnTop (true);

        for (int i = 0; i < 200 && webView == nullptr; ++i)
        {
            ee::shots::pump (25);
            if (auto* peer = editor->getPeer())
                webView = ee::shots::findWebView (peer->getNativeHandle());
        }

        if (webView == nullptr)
            fail ("the editor has no WKWebView - is this a WebView face?");

        // Loaded means: React has put the card up, the fonts it draws with have
        // arrived, and the page has reported its size back to the editor.
        const bool loaded = waitFor (
            "String(!!(window.__JUCE__ && document.querySelector('.pui-card') && document.fonts.status === 'loaded'))",
            15000);
        if (! loaded)
            fail ("the page never finished loading - has `npm run build` been run in this pedal's jsui/?");

        run (kPageHelper);

        run ("__eeShots.frame(" + std::to_string (kShadowRoom) + ")");
        editor->setSize (editor->getWidth() + 2 * kShadowRoom, editor->getHeight() + 2 * kShadowRoom);
        ee::shots::pump (200);
    }

    /** CSS pixels of room the window gets around the card - see frame(). */
    static constexpr int kShadowRoom = 80;

    /** What a shot keeps around the element it is of: the dark theme's panel
        shadow (0 20px 44px) is spent by 64px below it. Fixed rather than
        trimmed to the alpha, so a face's dark and light PNGs are the same size
        and line up pixel for pixel. */
    static constexpr double kShadowMargin = 64.0;

    ~FaceSession()
    {
        if (editor != nullptr)
        {
            // What a host's wrapper does before it deletes an editor; without
            // it the processor still holds this one as active and makes no
            // second.
            processor.editorBeingDeleted (editor.get());
            editor->removeFromDesktop();
            editor.reset();
        }
        ee::shots::pump (100);
    }

    /** Runs `script` and waits for its answer; a JS exception ends the tool. */
    std::string run (const std::string& script)
    {
        std::optional<std::string> result;
        std::string error;

        ee::shots::evaluate (webView, script,
                             [&] (const std::string& r, const std::string& e)
                             {
                                 error = e;
                                 result = r;
                             });

        for (int waited = 0; ! result.has_value(); waited += 10)
        {
            if (waited > 10000)
                fail ("the page did not answer: " + juce::String (script).substring (0, 80));
            ee::shots::pump (10);
        }

        if (! error.empty())
            fail ("in the page: " + juce::String (error) +
                  "\n  while running: " + juce::String (script).substring (0, 120));

        return *result;
    }

    bool waitFor (const std::string& condition, int timeoutMs)
    {
        for (int waited = 0; waited < timeoutMs; waited += 100)
        {
            if (run (condition) == "true")
                return true;
            ee::shots::pump (100);
        }
        return false;
    }

    void snapshot (double scale, std::vector<ee::shots::Crop> crops)
    {
        std::optional<std::string> error;
        ee::shots::snapshot (webView, scale, std::move (crops), [&] (const std::string& e) { error = e; });

        while (! error.has_value())
            ee::shots::pump (10);

        if (! error->empty())
            fail (*error);
    }

private:
    juce::AudioProcessor& processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    void* webView = nullptr;
};

std::string jsString (const juce::String& s)
{
    return juce::JSON::toString (s).toStdString();
}

ee::shots::Rect rectFromJson (const std::string& json)
{
    const auto v = juce::JSON::parse (juce::String (json));
    return { (double)v["x"], (double)v["y"], (double)v["w"], (double)v["h"] };
}

struct Options
{
    juce::File config;
    juce::File outDir;
    double scale = 2.0;
    juce::StringArray themes { "dark", "light" };
    juce::StringArray onlyShots;
};

void usage()
{
    std::printf ("usage: ee_face_shots_%s [--config file.json] [--out dir] [--scale N]\n"
                 "                        [--themes dark,light] [--only shot,shot]\n",
                 EE_SHOTS_TARGET);
}
} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    Options options;
    options.config = juce::File (EE_SHOTS_CONFIG);
    options.outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("face-shots");

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto value = [&]
        {
            if (i + 1 >= argc)
            {
                usage();
                std::exit (2);
            }
            return juce::String (argv[++i]);
        };

        if (arg == "--config")
            options.config = juce::File::getCurrentWorkingDirectory().getChildFile (value());
        else if (arg == "--out")
            options.outDir = juce::File::getCurrentWorkingDirectory().getChildFile (value());
        else if (arg == "--scale")
            options.scale = value().getDoubleValue();
        else if (arg == "--themes")
            options.themes = juce::StringArray::fromTokens (value(), ",", "");
        else if (arg == "--only")
            options.onlyShots = juce::StringArray::fromTokens (value(), ",", "");
        else
        {
            usage();
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }

    if (options.scale <= 0.0)
        fail ("--scale must be positive");

    juce::ScopedJuceInitialiser_GUI gui;
    ee::shots::becomeForegroundApp();

    const auto configRoot = ee::plugin::parseJson (options.config.loadFileAsString());
    const auto config = configRoot[EE_SHOTS_TARGET];
    if (! config.isObject())
        fail (options.config.getFullPathName() + " has no entry for " + EE_SHOTS_TARGET);

    const auto fileStem = config["file"].toString();
    if (fileStem.isEmpty())
        fail (juce::String (EE_SHOTS_TARGET) + ": \"file\" (the output name stem) is required");

    if (! options.outDir.createDirectory())
        fail ("cannot create " + options.outDir.getFullPathName());

    // --- the processor, set up the way the shots want it ---------------------
    EE_SHOTS_PROCESSOR processor;
    RunningPlayHead playHead;
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    if (const auto preset = config["preset"].toString(); preset.isNotEmpty())
        if (! processor.presets.load (ee::plugin::PresetStore::Kind::factory, preset))
            fail ("no factory preset called \"" + preset +
                  "\" (there are: " + processor.presets.factoryNames().joinIntoString (", ") + ")");

    juce::StringArray pinned;
    if (auto* settings = config["set"].getDynamicObject())
        for (const auto& [name, value] : settings->getProperties())
        {
            applySetting (processor, name.toString(), value);
            pinned.add (name.toString());
        }

    turnRestingKnobs (processor, pinned);

#if EE_SHOTS_GRAIN
    // The Mod tab's shape and its routes live in the state tree, not in
    // parameters, so they are set through the processor's own doors.
    if (config["lfoPoints"].isArray())
    {
        auto* shape = new juce::DynamicObject();
        shape->setProperty ("version", 1);
        shape->setProperty ("points", config["lfoPoints"]);
        processor.setLfoBreakpointsFromJson (juce::JSON::toString (juce::var (shape), true));
    }
    if (config["lfoRouting"].isArray())
        processor.setLfoRoutingFromJson (juce::JSON::toString (config["lfoRouting"], true));
#endif

    AudioPump pump (processor, playHead);
    pump.startThread (juce::Thread::Priority::high);

    // Long enough for delay lines, reverb tails and the grain cloud to fill.
    ee::shots::pump (1500);

    int written = 0;
    const auto shots = config["shots"];

    for (const auto& theme : options.themes)
    {
        for (int s = 0; s < shots.size(); ++s)
        {
            const auto shot = shots[s];
            const auto name = shot["name"].toString();

            if (! options.onlyShots.isEmpty() && ! options.onlyShots.contains (name))
                continue;

            const double tunerHz = shot.hasProperty ("tunerHz") ? (double)shot["tunerHz"] : 0.0;
            pump.guitar.setTunerNote (tunerHz);

            // A fresh editor per shot, so nothing one shot opened is still
            // open in the next.
            FaceSession face (processor);
            face.run ("__eeShots.setTheme(" + jsString (theme) + ")");
            ee::shots::pump (400);

            if (auto* clicks = shot["click"].getArray())
                for (const auto& selector : *clicks)
                {
                    face.run ("__eeShots.click(" + jsString (selector.toString()) + ")");
                    ee::shots::pump (300);
                }

            if (auto* clicks = shot["clickText"].getArray())
                for (const auto& text : *clicks)
                {
                    face.run ("__eeShots.clickText(" + jsString (text.toString()) + ")");
                    ee::shots::pump (300);
                }

            face.run ("__eeShots.clipScrims()");
            ee::shots::pump (shot.hasProperty ("wait") ? (int)shot["wait"] : 1200);

            const auto stem = fileStem + "-" + name;
            const auto file = options.outDir.getChildFile (stem + "-" + theme + ".png");
            face.snapshot (options.scale,
                           { { file.getFullPathName().toStdString(),
                               rectFromJson (face.run ("__eeShots.rect('.pui-card')")), FaceSession::kShadowMargin } });
            std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
            ++written;

            // "isolate": { "dialog": ".pui-tuner__box" } - the named element on
            // its own, everything else on the page hidden.
            if (auto* isolated = shot["isolate"].getDynamicObject())
                for (const auto& [suffix, selector] : isolated->getProperties())
                {
                    const auto area =
                        rectFromJson (face.run ("__eeShots.isolate(" + jsString (selector.toString()) + ")"));
                    ee::shots::pump (150);

                    const auto part =
                        options.outDir.getChildFile (stem + "-" + suffix.toString() + "-" + theme + ".png");
                    face.snapshot (options.scale,
                                   { { part.getFullPathName().toStdString(), area, FaceSession::kShadowMargin } });
                    face.run ("__eeShots.isolate('')");
                    std::printf ("wrote %s\n", part.getFullPathName().toRawUTF8());
                    ++written;
                }
        }
    }

    pump.stopThread (2000);
    processor.releaseResources();

    if (written == 0)
        fail ("no shots matched");

    return 0;
}
