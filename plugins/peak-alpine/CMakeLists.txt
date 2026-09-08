peak_add_plugin(PeakAlpine
    CODE       Palp
    PRODUCT    "Peak Alpine"
    BUNDLE     com.synthpeak.peakalpine
    CATEGORIES "Fx Delay Modulation Reverb"
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/peak-delay/jsui.
target_compile_definitions(PeakAlpine PRIVATE
    PEAKALPINE_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")
