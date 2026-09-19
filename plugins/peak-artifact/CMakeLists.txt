peak_add_plugin(PeakArtifact
    CODE       Bart
    PRODUCT    "BitBit Artifact"
    BUNDLE     com.bitbitaudio.artifact
    CATEGORIES "Fx Distortion"
    SOURCES    src/PeakArtifactWebEditor.cpp
    LIBS       juce::juce_dsp juce::juce_gui_extra
    WEBVIEW
)

# The React face reads straight off disk in dev (jsui/dist, rebuilt by `npm
# run build`) rather than through BinaryData - see plugins/peak-delay/jsui.
target_compile_definitions(PeakArtifact PRIVATE
    PEAKARTIFACT_JSUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsui")
