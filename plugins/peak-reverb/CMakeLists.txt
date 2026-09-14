peak_add_plugin(PeakReverb
    CODE       Prvb
    PRODUCT    "Peak Reverb"
    BUNDLE     com.synthpeak.peakreverb
    CATEGORIES "Fx Reverb"
    SOURCES    src/PeakReverbWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/peak-delay/jsui.
target_compile_definitions(PeakReverb PRIVATE
    PEAKREVERB_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")
