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
        ${CMAKE_SOURCE_DIR}/plugins/bitbit-delay/src/TapeTunerPanel.cpp)
    target_include_directories(ee_ui_snapshot
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-delay/src)
    target_compile_definitions(ee_ui_snapshot PUBLIC EE_TAPE_TUNER=1)
endif()

if(TARGET BitBitReverb)
    # Renders a dry file through the real processor so its output can be scored
    # against a reference recording.
    juce_add_console_app(ee_reverb_match PRODUCT_NAME "ee_reverb_match")

    target_sources(ee_reverb_match PRIVATE ReverbMatch.cpp)

    target_compile_definitions(ee_reverb_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_reverb_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-reverb/src)

    target_link_libraries(ee_reverb_match
        PRIVATE BitBitReverb ee_shared juce::juce_audio_formats
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

if(TARGET BitBitEq)
    # Renders audio through the whole EQ processor, band filters and bypass mix.
    juce_add_console_app(ee_eq_match PRODUCT_NAME "ee_eq_match")

    target_sources(ee_eq_match PRIVATE EqMatch.cpp)

    target_compile_definitions(ee_eq_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_eq_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-eq/src)

    target_link_libraries(ee_eq_match
        PRIVATE BitBitEq ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitDelay)
    # Renders a file through the whole delay processor, chain and all.
    juce_add_console_app(ee_delay_match PRODUCT_NAME "ee_delay_match")

    target_sources(ee_delay_match PRIVATE DelayMatch.cpp)

    target_compile_definitions(ee_delay_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_delay_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-delay/src)

    target_link_libraries(ee_delay_match
        PRIVATE BitBitDelay ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitDelay)
    # The A/B for a change to BitBit Delay that is meant to change nothing: a
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
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-delay/src)

    target_link_libraries(ee_delay_regress
        PRIVATE BitBitDelay ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitDelay)
    # ee::plugin::PresetStore against a real pedal - the factory bank compiled
    # in from plugins/bitbit-delay/presets, and a user bank in a folder of its
    # own. Shared machinery every pedal is meant to adopt, so it is worth a
    # suite rather than a manual click through one face.
    juce_add_console_app(ee_preset_tests PRODUCT_NAME "ee_preset_tests")

    target_sources(ee_preset_tests PRIVATE PresetTests.cpp)

    target_compile_definitions(ee_preset_tests PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_preset_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-delay/src)

    target_link_libraries(ee_preset_tests
        PRIVATE BitBitDelay ee_shared
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitTremPan)
    # Sweeps the Trem & Pan processor over every parameter combo and adverse input,
    # watching for a non-finite or runaway output.
    juce_add_console_app(ee_trempan_stress PRODUCT_NAME "ee_trempan_stress")

    target_sources(ee_trempan_stress PRIVATE TremPanStress.cpp)

    target_compile_definitions(ee_trempan_stress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_trempan_stress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-trem-pan/src)

    target_link_libraries(ee_trempan_stress
        PRIVATE BitBitTremPan ee_shared
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitTremPan)
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
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-trem-pan/src)

    target_link_libraries(ee_trempan_regress
        PRIVATE BitBitTremPan ee_shared juce::juce_audio_formats
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

# Renders a dry file through BitBit Sympathy's resonator engine directly (no
# processor), for A/B-ing the voicing itself by ear. The *_match family (see
# RegressHarness.h): "is this the sound we want", not "is this the same sound
# as before". See ee_sympathy_plugin_match below for the same thing through the
# whole processor - the two can disagree if the wiring around the engine has a
# bug the engine-only render cannot see.
juce_add_console_app(ee_sympathy_match PRODUCT_NAME "ee_sympathy_match")

target_sources(ee_sympathy_match PRIVATE SympathyMatch.cpp)

target_compile_definitions(ee_sympathy_match PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_sympathy_match
    PRIVATE ee_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags)

# Hunts non-finite output and runaway level in ee::dsp::ResonatorBank over long
# runs at maximum Coupling and Decay with Freeze on - the failure mode a bank of
# near-unity feedback loops plus a coupling matrix most invites.
juce_add_console_app(ee_sympathy_stress PRODUCT_NAME "ee_sympathy_stress")

target_sources(ee_sympathy_stress PRIVATE SympathyStress.cpp)

target_compile_definitions(ee_sympathy_stress PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_sympathy_stress
    PRIVATE ee_dsp
    PUBLIC juce::juce_recommended_config_flags)

if(TARGET BitBitSympathy)
    # The A/B for a change to BitBit Sympathy that is meant to change nothing: a
    # battery of settings through the whole processor, one checksum per pass.
    # New code, so nothing to diff yet - it is the baseline. See RegressHarness.h.
    juce_add_console_app(ee_sympathy_regress PRODUCT_NAME "ee_sympathy_regress")

    target_sources(ee_sympathy_regress PRIVATE SympathyRegress.cpp)

    target_compile_definitions(ee_sympathy_regress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_sympathy_regress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-sympathy/src)

    target_link_libraries(ee_sympathy_regress
        PRIVATE BitBitSympathy ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)

    # Renders a dry file through the *whole* BitBit Sympathy processor - Mix, the
    # dry duck, the bypass crossfade, the plugin's own parameter defaults - all
    # included. ee_sympathy_match above drives ee::dsp::ResonatorBank directly
    # and skips every bit of that; the two exist so a discrepancy between "the
    # engine sounds right" and "the finished pedal sounds right" shows up as a
    # difference between their outputs rather than staying invisible.
    juce_add_console_app(ee_sympathy_plugin_match PRODUCT_NAME "ee_sympathy_plugin_match")

    target_sources(ee_sympathy_plugin_match PRIVATE SympathyPluginMatch.cpp)

    target_compile_definitions(ee_sympathy_plugin_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_sympathy_plugin_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-sympathy/src)

    target_link_libraries(ee_sympathy_plugin_match
        PRIVATE BitBitSympathy ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitSpring)
    # Renders a dry file through the whole spring processor, for voicing it
    # against a reference recording.
    juce_add_console_app(ee_spring_match PRODUCT_NAME "ee_spring_match")

    target_sources(ee_spring_match PRIVATE SpringMatch.cpp)

    target_compile_definitions(ee_spring_match PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_spring_match
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-spring/src)

    target_link_libraries(ee_spring_match
        PRIVATE BitBitSpring ee_shared juce::juce_audio_formats
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

if(TARGET BitBitGrain)
    # Drives the real processor the way a host does, so the wiring around the
    # engine is under test and not just the engine.
    juce_add_console_app(ee_grain_host PRODUCT_NAME "ee_grain_host")

    target_sources(ee_grain_host PRIVATE GrainHost.cpp)

    target_compile_definitions(ee_grain_host PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_grain_host
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-grain/src)

    target_link_libraries(ee_grain_host
        PRIVATE BitBitGrain ee_shared juce::juce_graphics juce::juce_audio_formats
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

if(TARGET BitBitSpring)
    # The A/B for a change to the spring tank that is meant to leave BitBit Spring
    # exactly as it is - Tension and Low Cut are additive, and this is what says
    # so. See RegressHarness.h.
    juce_add_console_app(ee_spring_regress PRODUCT_NAME "ee_spring_regress")

    target_sources(ee_spring_regress PRIVATE SpringRegress.cpp)

    target_compile_definitions(ee_spring_regress PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_spring_regress
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-spring/src)

    target_link_libraries(ee_spring_regress
        PRIVATE BitBitSpring ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitArtifact)
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
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-artifact/src)

    target_link_libraries(ee_bitcrush_regress
        PRIVATE BitBitArtifact ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitModulation)
    # Drives the real BitBit Modulation processor like a host: every engine makes
    # sound and stays finite, the power toggle reaches the audio, the dry path
    # lands where the reported latency says, and the Init preset names every
    # parameter. A checksum per case, so it is also the A/B for the next change
    # that means to leave the pedal alone. See RegressHarness.h.
    juce_add_console_app(ee_modulation_host PRODUCT_NAME "ee_modulation_host")

    target_sources(ee_modulation_host PRIVATE ModulationHost.cpp)

    target_compile_definitions(ee_modulation_host PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_modulation_host
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-modulation/src)

    target_link_libraries(ee_modulation_host
        PRIVATE BitBitModulation ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(TARGET BitBitReverb)
    # The same for BitBit Reverb: both engines, the power toggle, bypass, and the
    # Init preset. A checksum per case - Shimmer stays at 0 so it reproduces.
    juce_add_console_app(ee_reverb_host PRODUCT_NAME "ee_reverb_host")

    target_sources(ee_reverb_host PRIVATE ReverbHost.cpp)

    target_compile_definitions(ee_reverb_host PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_include_directories(ee_reverb_host
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-reverb/src)

    target_link_libraries(ee_reverb_host
        PRIVATE BitBitReverb ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

# BitBit Alpine's switchable modules (Modulation, Reverb, Artifact). Not a
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

# Checks two specific claims about BitBit Artifact's Bit engine (see BitCheck.cpp):
# its Crush/Rate knobs at 100 % are bit-identical to Bit Crush's own engine at
# 50 %, and its Drive knob leaves the level where it was on real audio.
juce_add_console_app(ee_bit_check PRODUCT_NAME "ee_bit_check")

target_sources(ee_bit_check PRIVATE BitCheck.cpp)

target_compile_definitions(ee_bit_check PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ee_bit_check
    PRIVATE ee_dsp juce::juce_audio_processors juce::juce_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags)

if(TARGET BitBitAlpine)
    # Drives the real BitBit Alpine processor like a host: does it make sound, do
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
        PRIVATE ${CMAKE_SOURCE_DIR}/plugins/bitbit-alpine/src)

    target_link_libraries(ee_alpine_host
        PRIVATE BitBitAlpine ee_shared juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)
endif()

# The frozen parameter contract, one binary per shipping product. See
# ParamGolden.cpp for what it holds and why.
#
# One binary each rather than one that knows about them all: every pedal's
# PluginProcessor.cpp defines createPluginFilter(), so two of them in one link
# is a duplicate symbol, and the object that defines it is the same one that
# defines the processor. Generated from the list bitbit_add_plugin keeps, so a
# new product is frozen the day it is added instead of the day someone
# remembers.
get_property(eeReleaseTargets GLOBAL PROPERTY EE_RELEASE_PLUGIN_TARGETS)

foreach(pluginTarget IN LISTS eeReleaseTargets)
    set(goldenApp ee_param_golden_${pluginTarget})

    get_target_property(pluginDir ${pluginTarget} SOURCE_DIR)
    get_target_property(pluginCode ${pluginTarget} EE_PLUGIN_CODE)
    get_target_property(pluginProduct ${pluginTarget} EE_PLUGIN_PRODUCT)
    get_target_property(pluginBundle ${pluginTarget} EE_PLUGIN_BUNDLE)
    get_target_property(pluginCategories ${pluginTarget} EE_PLUGIN_CATEGORIES)

    juce_add_console_app(${goldenApp} PRODUCT_NAME "${goldenApp}")

    target_sources(${goldenApp} PRIVATE ParamGolden.cpp)

    # The processor class name is the target name plus "Processor" in every
    # pedal; bitbit_add_plugin is what makes that true.
    target_compile_definitions(${goldenApp} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        EE_GOLDEN_PROCESSOR=${pluginTarget}Processor
        EE_GOLDEN_CODE="${pluginCode}"
        EE_GOLDEN_PRODUCT="${pluginProduct}"
        EE_GOLDEN_BUNDLE="${pluginBundle}"
        EE_GOLDEN_CATEGORIES="${pluginCategories}"
        EE_GOLDEN_MANUFACTURER="${EE_MANUFACTURER_CODE}"
        EE_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/golden/${pluginTarget}.txt")

    target_include_directories(${goldenApp} PRIVATE ${pluginDir}/src)

    target_link_libraries(${goldenApp}
        PRIVATE ${pluginTarget} ee_shared
        PUBLIC juce::juce_recommended_config_flags)
endforeach()
