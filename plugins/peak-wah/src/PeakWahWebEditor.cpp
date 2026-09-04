#include "PeakWahWebEditor.h"

#include "PluginProcessor.h"

namespace
{
juce::String kParamOn = "on";
juce::String kParamSync = "sync";
juce::String kParamFreq = "freq";
juce::String kParamTime = "time";
juce::String kParamType = "ftype";

const char* mimeForExtension (const juce::String& extension)
{
    if (extension == "html") return "text/html";
    if (extension == "js") return "text/javascript";
    if (extension == "css") return "text/css";
    if (extension == "json") return "application/json";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "png") return "image/png";
    return "application/octet-stream";
}
} // namespace

#if JUCE_ANDROID
const juce::String PeakWahWebEditor::devServerAddress = "http://10.0.2.2:3000/";
#else
const juce::String PeakWahWebEditor::devServerAddress = "http://localhost:3000/";
#endif

bool PeakWahWebEditor::SinglePageBrowser::pageAboutToLoad (const juce::String& newURL)
{
    return newURL == PeakWahWebEditor::devServerAddress || newURL == getResourceProviderRoot();
}

PeakWahWebEditor::PeakWahWebEditor (PeakWahProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processorRef (p),
      webView (juce::WebBrowserComponent::Options {}
                   .withNativeIntegrationEnabled()
                   .withOptionsFrom (rangeRelay)
                   .withOptionsFrom (freqRelay)
                   .withOptionsFrom (qRelay)
                   .withOptionsFrom (mixRelay)
                   .withOptionsFrom (decayRelay)
                   .withOptionsFrom (shapeRelay)
                   .withOptionsFrom (timeRelay)
                   .withOptionsFrom (typeRelay)
                   .withOptionsFrom (stereoRelay)
                   .withOptionsFrom (syncRelay)
                   .withOptionsFrom (onRelay)
                   .withOptionsFrom (controlParameterIndexReceiver)
                   .withNativeFunction ("formatKnobValue",
                                        [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                        {
                                            const auto id = args[0].toString();
                                            juce::String text;

                                            if (id == kParamFreq)
                                                text = processorRef.freqReadout();
                                            else if (id == kParamTime)
                                                text = processorRef.timeReadout();
                                            else if (id == kParamType)
                                                text = processorRef.typeReadout();
                                            else if (auto* param = processorRef.apvts.getParameter (id))
                                                text = param->getCurrentValueAsText();

                                            complete (text);
                                        })
                   .withResourceProvider ([this] (const auto& url) { return getResource (url); },
                                           juce::URL { devServerAddress }.getOrigin())),
      rangeAttachment (*p.apvts.getParameter ("range"), rangeRelay, p.apvts.undoManager),
      freqAttachment (*p.apvts.getParameter (kParamFreq), freqRelay, p.apvts.undoManager),
      qAttachment (*p.apvts.getParameter ("q"), qRelay, p.apvts.undoManager),
      mixAttachment (*p.apvts.getParameter ("mix"), mixRelay, p.apvts.undoManager),
      decayAttachment (*p.apvts.getParameter ("decay"), decayRelay, p.apvts.undoManager),
      shapeAttachment (*p.apvts.getParameter ("shape"), shapeRelay, p.apvts.undoManager),
      timeAttachment (*p.apvts.getParameter (kParamTime), timeRelay, p.apvts.undoManager),
      typeAttachment (*p.apvts.getParameter (kParamType), typeRelay, p.apvts.undoManager),
      stereoAttachment (*p.apvts.getParameter ("stereo"), stereoRelay, p.apvts.undoManager),
      syncAttachment (*p.apvts.getParameter (kParamSync), syncRelay, p.apvts.undoManager),
      onAttachment (*p.apvts.getParameter (kParamOn), onRelay, p.apvts.undoManager)
{
    addAndMakeVisible (webView);
    webView.goToURL (kUseDevServer ? devServerAddress : juce::WebBrowserComponent::getResourceProviderRoot());
    setSize (566, 360);
    setResizable (false, false);

    startTimerHz (45); // matches the old ee::ui::FilterScope's own repaint rate
}

PeakWahWebEditor::~PeakWahWebEditor()
{
    stopTimer();
}

void PeakWahWebEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

void PeakWahWebEditor::timerCallback()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("modL", processorRef.lfoModLUi.load (std::memory_order_relaxed));
    payload->setProperty ("modR", processorRef.lfoModRUi.load (std::memory_order_relaxed));
    webView.emitEventIfBrowserIsVisible ("filterMod", juce::var (payload));
}

std::optional<juce::WebBrowserComponent::Resource> PeakWahWebEditor::getResource (const juce::String& url)
{
    const auto requested = url == "/" ? juce::String { "index.html" } : url.fromFirstOccurrenceOf ("/", false, false);

    const juce::File distDir = juce::File (PEAKWAH_JSUI_DIR).getChildFile ("dist");
    const auto file = distDir.getChildFile (requested);

    if (! file.existsAsFile())
        return std::nullopt;

    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
        return std::nullopt;

    std::vector<std::byte> bytes (block.getSize());
    std::memcpy (bytes.data(), block.getData(), block.getSize());

    return juce::WebBrowserComponent::Resource { std::move (bytes),
                                                 juce::String (mimeForExtension (file.getFileExtension().substring (1))) };
}
