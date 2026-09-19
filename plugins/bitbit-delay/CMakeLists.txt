bitbit_add_plugin(BitBitDelay
    CODE       Bdly
    PRODUCT    "BitBit Delay"
    BUNDLE     com.bitbitaudio.delay
    CATEGORIES "Fx Delay"
    SOURCES    src/BitBitDelayWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see jsui/README.md.
target_compile_definitions(BitBitDelay PRIVATE
    BITBITDELAY_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")

if(EE_TAPE_TUNER)
    target_sources(BitBitDelay PRIVATE src/TapeTunerPanel.cpp)
    target_compile_definitions(BitBitDelay PUBLIC EE_TAPE_TUNER=1)
    message(STATUS "BitBit Delay: tape tuning panel enabled - development build")
endif()
