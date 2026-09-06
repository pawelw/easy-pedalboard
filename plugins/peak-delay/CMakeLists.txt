peak_add_plugin(PeakDelay
    CODE       Pdly
    PRODUCT    "Peak Delay"
    BUNDLE     com.synthpeak.peakdelay
    CATEGORIES "Fx Delay"
    SOURCES    src/PeakDelayWebEditor.cpp
    LIBS       juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see jsui/README.md.
target_compile_definitions(PeakDelay PRIVATE
    PEAKDELAY_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")

if(EE_TAPE_TUNER)
    target_sources(PeakDelay PRIVATE src/TapeTunerPanel.cpp)
    target_compile_definitions(PeakDelay PUBLIC EE_TAPE_TUNER=1)
    message(STATUS "Peak Delay: tape tuning panel enabled - development build")
endif()
