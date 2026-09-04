# ee_dsp and ee_ui were INTERFACE libraries: their .cpp were pushed onto every
# consumer and compiled there, so PedalEditor.cpp (1500 lines) built 14 times and
# the DSP sources 18. STATIC compiles each translation unit once; consumers link
# the archive.
#
# The old note here objected that JUCE config macros are per-target. They are -
# but only for JUCE's own module code and the plugin client. Nothing under
# shared/ pulls a JucePlugin_* macro or a generated JuceHeader; these sources
# include the module umbrella headers only (checked), so our code is safe to
# compile once regardless of which pedal ends up linking it.
#
# The JUCE module unity files themselves still ride along into each consumer via
# the PUBLIC links below, exactly as before - that half of the duplication is a
# separate change. What this buys is our own code compiled once, and an
# incremental edit to a shared file relinking the consumers instead of
# recompiling them.

add_library(ee_dsp STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/FdnReverb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/SpringReverb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/TapeDelay.cpp)
target_include_directories(ee_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# DaisySP is a private implementation detail of FdnReverb.cpp - its header is
# never pulled into ee/dsp/FdnReverb.h - but the link stays PUBLIC so a consumer
# that pulls FdnReverb.o out of the archive still resolves its symbols.
target_link_libraries(ee_dsp
    PUBLIC juce::juce_audio_basics DaisySP chowdsp_wdf
    PRIVATE juce::juce_recommended_config_flags)
# Linked into plugin bundles (MODULE libraries), so the archive must be PIC.
set_target_properties(ee_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ee_ui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PedalTheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PedalLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Knob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/FaderStrip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MiniToggle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SlideToggle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DigitalKnob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DigitalSwitch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DigitalToggle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DigitalScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/WaveDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/FilterScope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/GrainScope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PresetBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PedalEditor.cpp)
target_include_directories(ee_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ee_ui
    PUBLIC juce::juce_audio_utils juce::juce_gui_basics ee_assets melatonin_blur
    PRIVATE juce::juce_recommended_config_flags)
set_target_properties(ee_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ee_shared INTERFACE)
target_link_libraries(ee_shared INTERFACE ee_dsp ee_ui)
