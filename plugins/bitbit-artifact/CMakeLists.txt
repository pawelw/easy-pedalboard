bitbit_add_plugin(BitBitArtifact
    CODE       Bart
    PRODUCT    "BitBit Artifact"
    BUNDLE     com.bitbitaudio.artifact
    CATEGORIES "Fx Distortion"
    SOURCES    src/BitBitArtifactWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/bitbit-delay/jsui.
target_compile_definitions(BitBitArtifact PRIVATE
    BITBITARTIFACT_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")
