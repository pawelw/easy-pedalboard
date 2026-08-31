# peak_add_plugin(<Target>
#     CODE        <four-letter plugin code>
#     PRODUCT     <"Peak Something">
#     BUNDLE      <com.synthpeak.something>
#     CATEGORIES  <VST3 category words>
#     [SOURCES    <extra .cpp beyond src/PluginProcessor.cpp>]
#     [LIBS       <extra link targets beyond ee_shared>])
#
# Every pedal is the same plugin with a different processor, so the twenty-odd
# lines of juce_add_plugin boilerplate they used to each carry live here once.
# Anything genuinely per-pedal is an argument.
function(peak_add_plugin TARGET)
    cmake_parse_arguments(ARG "" "CODE;PRODUCT;BUNDLE;CATEGORIES" "SOURCES;LIBS" ${ARGN})

    foreach(required CODE PRODUCT BUNDLE CATEGORIES)
        if(NOT ARG_${required})
            message(FATAL_ERROR "peak_add_plugin(${TARGET}): ${required} is required")
        endif()
    endforeach()

    # Dev builds make Standalone only. It is the format you can launch and hear
    # without a host, and it skips two extra link steps per pedal - which on this
    # tree, with LTO on, is most of the wall time. Release builds make all three.
    if(EE_DEV_FORMATS)
        set(formats Standalone)
    else()
        set(formats VST3 AU Standalone)
    endif()

    separate_arguments(categories UNIX_COMMAND "${ARG_CATEGORIES}")

    juce_add_plugin(${TARGET}
        COMPANY_NAME            "Synth Peak"
        BUNDLE_ID               ${ARG_BUNDLE}
        PLUGIN_MANUFACTURER_CODE Peak
        PLUGIN_CODE             ${ARG_CODE}
        FORMATS                 ${formats}
        PRODUCT_NAME            ${ARG_PRODUCT}
        VST3_CATEGORIES         ${categories}
        AU_MAIN_TYPE            kAudioUnitType_Effect
        IS_SYNTH                FALSE
        NEEDS_MIDI_INPUT        FALSE
        NEEDS_MIDI_OUTPUT       FALSE
        IS_MIDI_EFFECT          FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD ${EE_INSTALL_PLUGINS})

    target_sources(${TARGET} PRIVATE src/PluginProcessor.cpp ${ARG_SOURCES})

    target_compile_definitions(${TARGET} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    # LTO costs minutes per link and buys nothing while iterating, so it is only
    # applied to the builds that get handed to someone.
    set(link_flags juce::juce_recommended_config_flags)
    if(EE_LTO)
        list(APPEND link_flags juce::juce_recommended_lto_flags)
    endif()

    target_link_libraries(${TARGET}
        PRIVATE ee_shared ${ARG_LIBS}
        PUBLIC ${link_flags})
endfunction()
