juce_add_console_app(ee_dsp_tests PRODUCT_NAME "ee_dsp_tests")

target_sources(ee_dsp_tests PRIVATE DspTests.cpp)

target_compile_definitions(ee_dsp_tests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_dsp_tests
    PRIVATE ee_dsp
    PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(ee_ui_snapshot PRODUCT_NAME "ee_ui_snapshot")

target_sources(ee_ui_snapshot PRIVATE UiSnapshot.cpp)

target_compile_definitions(ee_ui_snapshot PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_ui_snapshot
    PRIVATE ee_ui
    PUBLIC juce::juce_recommended_config_flags)

# Renders a dry file through the real processor so its output can be scored
# against a reference recording.
juce_add_console_app(ee_reverb_match PRODUCT_NAME "ee_reverb_match")

target_sources(ee_reverb_match PRIVATE ReverbMatch.cpp)

target_compile_definitions(ee_reverb_match PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_include_directories(ee_reverb_match
    PRIVATE ${CMAKE_SOURCE_DIR}/plugins/easy-reverb/src)

target_link_libraries(ee_reverb_match
    PRIVATE EasyReverb ee_shared juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags)

# Renders a file through the tape colour stage alone, for voicing it against a
# reference recording.
juce_add_console_app(ee_tape_match PRODUCT_NAME "ee_tape_match")

target_sources(ee_tape_match PRIVATE TapeMatch.cpp)

target_compile_definitions(ee_tape_match PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_tape_match
    PRIVATE ee_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags)

# Renders a file through the whole delay processor, chain and all.
juce_add_console_app(ee_delay_match PRODUCT_NAME "ee_delay_match")

target_sources(ee_delay_match PRIVATE DelayMatch.cpp)

target_compile_definitions(ee_delay_match PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_include_directories(ee_delay_match
    PRIVATE ${CMAKE_SOURCE_DIR}/plugins/easy-delay/src)

target_link_libraries(ee_delay_match
    PRIVATE EasyDelay ee_shared juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags)
