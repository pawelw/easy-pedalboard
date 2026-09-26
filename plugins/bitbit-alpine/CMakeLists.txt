bitbit_add_plugin(BitBitAlpine
    CODE       Balp
    PRODUCT    "BitBit Alpine"
    BUNDLE     com.bitbitaudio.alpine
    CATEGORIES "Fx Delay Modulation Reverb"
    SOURCES    src/BitBitAlpineWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra juce::juce_audio_formats ee_tape_assets
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/bitbit-delay/jsui.
target_compile_definitions(BitBitAlpine PRIVATE
    BITBITALPINE_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")

# The corner build stamp (see EE_BUILD_STAMP in the top-level CMakeLists.txt):
# a developer's way to tell a rebuilt plugin from a stale one, not something a
# customer should see, so the release preset leaves it out.
if(EE_BUILD_STAMP)
    target_compile_definitions(BitBitAlpine PRIVATE EE_BUILD_STAMP=1)
else()
    target_compile_definitions(BitBitAlpine PRIVATE EE_BUILD_STAMP=0)
endif()

# Dev-only output-safety flight recorder + side panel (the safety net itself,
# sanitizeOutput, is always compiled). Never ship one.
if(EE_ALPINE_WATCHDOG)
    target_sources(BitBitAlpine PRIVATE src/AlpineWatchdogPanel.cpp)
    target_compile_definitions(BitBitAlpine PUBLIC EE_ALPINE_WATCHDOG=1)
    message(STATUS "BitBit Alpine: output watchdog panel enabled - development build")
endif()
