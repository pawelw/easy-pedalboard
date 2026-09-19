bitbit_add_plugin(BitBitGrain
    CODE       Bgrn
    PRODUCT    "BitBit Grains"
    BUNDLE     com.bitbitaudio.grains
    CATEGORIES "Fx Delay"
    SOURCES    src/BitBitGrainWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/bitbit-delay/jsui.
target_compile_definitions(BitBitGrain PRIVATE
    BITBITGRAIN_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")

if(EE_GRAIN_TUNER)
    target_sources(BitBitGrain PRIVATE src/GrainTunerPanel.cpp)
    target_compile_definitions(BitBitGrain PUBLIC EE_GRAIN_TUNER=1)
    message(STATUS "BitBit Grain: grain tuning panel enabled - development build")
endif()
if(EE_GRAIN_TRACE)
    target_compile_definitions(BitBitGrain PUBLIC EE_GRAIN_TRACE=1)
    message(STATUS "BitBit Grain: runaway trace log enabled - development build")
endif()
