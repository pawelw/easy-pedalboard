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

if(TARGET PeakDelay)
    # The A/B for a change to Peak Delay that is meant to change nothing: a
    # battery of settings through the whole processor, one checksum per pass.
    # ee_delay_match above is the other thing - a voicing renderer for a real
    # file. See RegressHarness.h.
    juce_add_console_app(ee_delay_regress PRODUCT_NAME "ee_delay_regress")

    target_sources(ee_delay_regress PRIVATE DelayRegress.cpp)

    target_compile_definitions(ee_delay_regress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_delay_regress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-delay/src)

    target_link_libraries(ee_delay_regress
        PRIVATE PeakDelay ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET PeakDelay)
    # ee::plugin::PresetStore against a real pedal - the factory bank compiled
    # in from plugins/peak-delay/presets, and a user bank in a folder of its
    # own. Shared machinery every pedal is meant to adopt, so it is worth a
    # suite rather than a manual click through one face.
    juce_add_console_app(ee_preset_tests PRODUCT_NAME "ee_preset_tests")

    target_sources(ee_preset_tests PRIVATE PresetTests.cpp)

    target_compile_definitions(ee_preset_tests PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_preset_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-delay/src)

    target_link_libraries(ee_preset_tests
        PRIVATE PeakDelay ee_shared
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

if(TARGET PeakTremPan)
    # Renders a fixed battery of settings through the whole Trem & Pan processor
    # and prints a checksum per pass. Not a pass/fail suite: it is the A/B for a
    # change that is meant to change nothing, so its output is compared against
    # itself from before the change. See RegressHarness.h for why *_regress and
    # *_match are two different families.
    juce_add_console_app(ee_trempan_regress PRODUCT_NAME "ee_trempan_regress")

    target_sources(ee_trempan_regress PRIVATE TremPanRegress.cpp)

    target_compile_definitions(ee_trempan_regress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_trempan_regress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-trem-pan/src)

    target_link_libraries(ee_trempan_regress
        PRIVATE PeakTremPan ee_shared juce::juce_audio_formats
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

# Drives the grain cloud into the reverb it feeds, over the knob range and
# against adverse input. The grainer alone is feed-forward and cannot latch a
# non-finite; the network behind it can, so they are stressed joined up.
juce_add_console_app(ee_grain_stress PRODUCT_NAME "ee_grain_stress")

target_sources(ee_grain_stress PRIVATE GrainStress.cpp)

target_compile_definitions(ee_grain_stress PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_grain_stress
    PRIVATE ee_dsp
    PUBLIC juce::juce_recommended_config_flags)

if(TARGET PeakGrain)
    # Drives the real processor the way a host does, so the wiring around the
    # engine is under test and not just the engine.
    juce_add_console_app(ee_grain_host PRODUCT_NAME "ee_grain_host")

    target_sources(ee_grain_host PRIVATE GrainHost.cpp)

    target_compile_definitions(ee_grain_host PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_grain_host
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-grain/src)

    target_link_libraries(ee_grain_host
        PRIVATE PeakGrain ee_shared juce::juce_graphics
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Loads an installed Audio Unit and runs signal through it. Tests the path a
# host actually takes - component scan, bus negotiation, the AU wrapper's own
# buffer handling - which a test that instantiates the processor class directly
# never touches.
juce_add_console_app(ee_au_host PRODUCT_NAME "ee_au_host")

target_sources(ee_au_host PRIVATE AuHost.cpp)

target_compile_definitions(ee_au_host PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_AU=1
    # So the run loop can be pumped between blocks: without it a plugin's
    # message-thread work (meters, timers, diagnostics) never runs here.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_au_host
    PRIVATE juce::juce_audio_processors juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)

if(TARGET PeakSpring)
    # The A/B for a change to the spring tank that is meant to leave Peak Spring
    # exactly as it is - Tension and Low Cut are additive, and this is what says
    # so. See RegressHarness.h.
    juce_add_console_app(ee_spring_regress PRODUCT_NAME "ee_spring_regress")

    target_sources(ee_spring_regress PRIVATE SpringRegress.cpp)

    target_compile_definitions(ee_spring_regress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_spring_regress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-spring/src)

    target_link_libraries(ee_spring_regress
        PRIVATE PeakSpring ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET PeakArtifact)
    # Baseline for the Bit Crush engine, which was a pass-through placeholder
    # until it was voiced. New code, so nothing to diff yet - it is the A/B for
    # the next change that means to leave the crusher alone, and it also asserts
    # that Jitter renders reproducibly. See RegressHarness.h.
    juce_add_console_app(ee_bitcrush_regress PRODUCT_NAME "ee_bitcrush_regress")

    target_sources(ee_bitcrush_regress PRIVATE BitCrushRegress.cpp)

    target_compile_definitions(ee_bitcrush_regress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_bitcrush_regress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-artifact/src)

    target_link_libraries(ee_bitcrush_regress
        PRIVATE PeakArtifact ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

# Peak Alpine's switchable modules (Modulation, Reverb, Artifact). Not a
# *_regress tool - this is new code, so there is nothing to diff against; it
# asserts the contract MultiEngineModule promises, and hunts the thing a module
# built out of other people's engines is most likely to get wrong: a click on
# the engine switch.
juce_add_console_app(ee_module_stress PRODUCT_NAME "ee_module_stress")

target_sources(ee_module_stress PRIVATE ModuleStress.cpp)

target_compile_definitions(ee_module_stress PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_module_stress
    PRIVATE ee_dsp juce::juce_audio_processors juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

if(TARGET PeakAlpine)
    # Drives the real Peak Alpine processor like a host: does it make sound, do
    # the three power toggles reach the audio, is a bypassed plugin unity, and
    # is every engine pair finite. Prints a checksum per case, so it is also the
    # baseline for the next refactor of it.
    juce_add_console_app(ee_alpine_host PRODUCT_NAME "ee_alpine_host")

    target_sources(ee_alpine_host PRIVATE AlpineHost.cpp)

    target_compile_definitions(ee_alpine_host PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_alpine_host
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/peak-alpine/src)

    target_link_libraries(ee_alpine_host
        PRIVATE PeakAlpine ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()
