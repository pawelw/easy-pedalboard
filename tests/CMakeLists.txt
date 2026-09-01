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
    PRIVATE ee_ui ee_tape_assets
    PUBLIC juce::juce_recommended_config_flags)

if(EE_TAPE_TUNER)
    target_sources(ee_ui_snapshot PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/peak-delay/src/TapeTunerPanel.cpp)
    target_include_directories(ee_ui_snapshot
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-delay/src)
    target_compile_definitions(ee_ui_snapshot PUBLIC EE_TAPE_TUNER=1)
endif()

if(EE_SHIMMER_TUNER)
    target_sources(ee_ui_snapshot PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/peak-reverb/src/ShimmerTunerPanel.cpp)
    target_include_directories(ee_ui_snapshot
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-reverb/src)
    target_compile_definitions(ee_ui_snapshot PUBLIC EE_SHIMMER_TUNER=1)
endif()

if(TARGET PeakReverb)
    # Renders a dry file through the real processor so its output can be scored
    # against a reference recording.
    juce_add_console_app(ee_reverb_match PRODUCT_NAME "ee_reverb_match")

    target_sources(ee_reverb_match PRIVATE ReverbMatch.cpp)

    target_compile_definitions(ee_reverb_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_reverb_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-reverb/src)

    target_link_libraries(ee_reverb_match
        PRIVATE PeakReverb ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

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

if(TARGET PeakEq)
    # Renders audio through the whole EQ processor, band filters and bypass mix.
    juce_add_console_app(ee_eq_match PRODUCT_NAME "ee_eq_match")

    target_sources(ee_eq_match PRIVATE EqMatch.cpp)

    target_compile_definitions(ee_eq_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_eq_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-eq/src)

    target_link_libraries(ee_eq_match
        PRIVATE PeakEq ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET PeakDelay)
    # Renders a file through the whole delay processor, chain and all.
    juce_add_console_app(ee_delay_match PRODUCT_NAME "ee_delay_match")

    target_sources(ee_delay_match PRIVATE DelayMatch.cpp)

    target_compile_definitions(ee_delay_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_delay_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-delay/src)

    target_link_libraries(ee_delay_match
        PRIVATE PeakDelay ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET PeakTremPan)
    # Sweeps the Trem & Pan processor over every parameter combo and adverse input,
    # watching for a non-finite or runaway output.
    juce_add_console_app(ee_trempan_stress PRODUCT_NAME "ee_trempan_stress")

    target_sources(ee_trempan_stress PRIVATE TremPanStress.cpp)

    target_compile_definitions(ee_trempan_stress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_trempan_stress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-trem-pan/src)

    target_link_libraries(ee_trempan_stress
        PRIVATE PeakTremPan ee_shared
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Sweeps the FDN reverb over extreme settings and adverse input, watching for a
# non-finite or runaway tail.
juce_add_console_app(ee_reverb_stress PRODUCT_NAME "ee_reverb_stress")

target_sources(ee_reverb_stress PRIVATE ReverbStress.cpp)

target_compile_definitions(ee_reverb_stress PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_reverb_stress
    PRIVATE ee_dsp
    PUBLIC juce::juce_recommended_config_flags)

# Sweeps the tape machine over every knob combination and adverse input,
# watching for a non-finite or runaway output.
juce_add_console_app(ee_tape_stress PRODUCT_NAME "ee_tape_stress")

target_sources(ee_tape_stress PRIVATE TapeStress.cpp)

target_compile_definitions(ee_tape_stress PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_tape_stress
    PRIVATE ee_dsp
    PUBLIC juce::juce_recommended_config_flags)

# Renders a file through the whole tape machine, for voicing it against a
# reference recording. Every knob is a command-line argument.
juce_add_console_app(ee_tape_render PRODUCT_NAME "ee_tape_render")

target_sources(ee_tape_render PRIVATE TapeMachineRender.cpp)

target_compile_definitions(ee_tape_render PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_tape_render
    PRIVATE ee_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags)

if(TARGET PeakSpring)
    # Renders a dry file through the whole spring processor, for voicing it
    # against a reference recording.
    juce_add_console_app(ee_spring_match PRODUCT_NAME "ee_spring_match")

    target_sources(ee_spring_match PRIVATE SpringMatch.cpp)

    target_compile_definitions(ee_spring_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_spring_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-spring/src)

    target_link_libraries(ee_spring_match
        PRIVATE PeakSpring ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Drives silence-then-note through the auto-wah over a spread of settings,
# looking for a step in the output at the onset - the click that a phase reset
# or a hard capacitance change in the tank leaves behind.
juce_add_console_app(ee_wah_stress PRODUCT_NAME "ee_wah_stress")

target_sources(ee_wah_stress PRIVATE WahStress.cpp)

target_compile_definitions(ee_wah_stress PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_wah_stress
    PRIVATE ee_dsp
    PUBLIC juce::juce_recommended_config_flags)
