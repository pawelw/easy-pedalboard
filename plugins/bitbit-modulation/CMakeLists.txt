bitbit_add_plugin(BitBitModulation
    CODE       Bmod
    PRODUCT    "BitBit Modulation"
    BUNDLE     com.bitbitaudio.modulation
    CATEGORIES "Fx Modulation"
    SOURCES    src/BitBitModulationWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra juce::juce_audio_formats ee_tape_assets
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/bitbit-delay/jsui.
target_compile_definitions(BitBitModulation PRIVATE
    BITBITMODULATION_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")
