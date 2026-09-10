peak_add_plugin(PeakAlpine
    CODE       Palp
    PRODUCT    "Peak Alpine"
    BUNDLE     com.synthpeak.peakalpine
    CATEGORIES "Fx Delay Modulation Reverb"
    SOURCES    src/PeakAlpineWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra juce::juce_audio_formats ee_tape_assets
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/peak-delay/jsui.
target_compile_definitions(PeakAlpine PRIVATE
    PEAKALPINE_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")

# Dev-only output-safety flight recorder + side panel (the safety net itself,
# sanitizeOutput, is always compiled). Never ship one.
if(EE_ALPINE_WATCHDOG)
    target_sources(PeakAlpine PRIVATE src/AlpineWatchdogPanel.cpp)
    target_compile_definitions(PeakAlpine PUBLIC EE_ALPINE_WATCHDOG=1)
    message(STATUS "Peak Alpine: output watchdog panel enabled - development build")
endif()
