peak_add_plugin(PeakWah
    CODE       Pwah
    PRODUCT    "Peak Wah"
    BUNDLE     com.synthpeak.peakwah
    CATEGORIES "Fx Filter"
    SOURCES    src/PeakWahWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - there is no packaging step to
# ship yet, this is a spike. See jsui/README.md.
target_compile_definitions(PeakWah PRIVATE
    PEAKWAH_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")
