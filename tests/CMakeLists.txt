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
