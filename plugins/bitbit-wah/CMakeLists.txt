bitbit_add_plugin(BitBitWah
    CODE       Bwah
    PRODUCT    "BitBit Wah"
    BUNDLE     com.bitbitaudio.wah
    CATEGORIES "Fx Filter"
    SOURCES    src/BitBitWahWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - there is no packaging step to
# ship yet, this is a spike. See jsui/README.md.
target_compile_definitions(BitBitWah PRIVATE
    BITBITWAH_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")
