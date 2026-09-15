peak_add_plugin(PeakGrain
    CODE       Pgrn
    PRODUCT    "Peak Grain"
    BUNDLE     com.synthpeak.peakgrain
    CATEGORIES "Fx Delay"
    SOURCES    src/PeakGrainWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/peak-delay/jsui.
target_compile_definitions(PeakGrain PRIVATE
    PEAKGRAIN_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")

if(EE_GRAIN_TUNER)
    target_sources(PeakGrain PRIVATE src/GrainTunerPanel.cpp)
    target_compile_definitions(PeakGrain PUBLIC EE_GRAIN_TUNER=1)
    message(STATUS "Peak Grain: grain tuning panel enabled - development build")
endif()
if(EE_GRAIN_TRACE)
    target_compile_definitions(PeakGrain PUBLIC EE_GRAIN_TRACE=1)
    message(STATUS "Peak Grain: runaway trace log enabled - development build")
endif()
