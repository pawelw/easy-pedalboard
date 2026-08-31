# INTERFACE rather than STATIC on purpose: JUCE module targets propagate their
# sources to consumers, and JUCE config macros are per-target. Compiling this
# code into each plugin target is the only way to keep both correct.

add_library(ee_dsp INTERFACE)
target_include_directories(ee_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(ee_dsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/FdnReverb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/TapeDelay.cpp)
# DaisySP is a private implementation detail of FdnReverb.cpp - its header is
# never pulled into ee/dsp/FdnReverb.h - but INTERFACE sources compile into each
# consumer, so the link and its include dirs have to propagate too.
target_link_libraries(ee_dsp INTERFACE juce::juce_audio_basics DaisySP chowdsp_wdf)

add_library(ee_ui INTERFACE)
target_include_directories(ee_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(ee_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PedalTheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PedalLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Knob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/FaderStrip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MiniToggle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SlideToggle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/WaveDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/FilterScope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PedalEditor.cpp)
target_link_libraries(ee_ui INTERFACE juce::juce_audio_utils juce::juce_gui_basics ee_assets)

add_library(ee_shared INTERFACE)
target_link_libraries(ee_shared INTERFACE ee_dsp ee_ui)
