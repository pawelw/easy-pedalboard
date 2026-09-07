# Factory presets, if the pedal has any: every .xml in its own presets/ folder
# is compiled into the binary, so the bank a release ships with is pinned by
# the release rather than by whatever happens to be on the machine. Drop a file
# in the folder and it is in the next build - there is nothing per-pedal to
# register, which is the point.
#
# CONFIGURE_DEPENDS so adding or removing one re-globs on the next build
# instead of needing a manual cmake run. The usual objection to globbing is
# that it hides new source files from your collaborators' incremental builds;
# that is exactly what CONFIGURE_DEPENDS fixes, and preset files are data with
# no other build-system meaning.
function(peak_add_factory_presets TARGET)
    file(GLOB presetFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.xml")

    # PUBLIC throughout, not PRIVATE: a pedal's PluginProcessor.h includes the
    # generated header behind EE_HAS_FACTORY_PRESETS, so every target that
    # includes that header - the offline test harnesses in tests/ - needs the
    # same macro *and* the same include directory. Split them and a harness
    # compiles with the macro on and no header to find.
    if(NOT presetFiles)
        # Still tell the processor where the folder would be: with
        # EE_PRESET_AUTHOR on, the face's author button creates it.
        target_compile_definitions(${TARGET} PUBLIC
            EE_HAS_FACTORY_PRESETS=0
            EE_PRESET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")
        return()
    endif()

    # NAMESPACE is the same in every pedal on purpose - each target links only
    # its own binary-data library and sees only its own generated header, so
    # one name lets ee/plugin/PresetStore.h's EE_FACTORY_PRESETS macro be
    # written once instead of once per pedal.
    juce_add_binary_data(${TARGET}_presets
        NAMESPACE   FactoryPresets
        HEADER_NAME FactoryPresets.h
        SOURCES     ${presetFiles})

    set_target_properties(${TARGET}_presets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

    target_link_libraries(${TARGET} PUBLIC ${TARGET}_presets)
    target_compile_definitions(${TARGET} PUBLIC
        EE_HAS_FACTORY_PRESETS=1
        EE_FACTORY_PRESETS_HEADER="FactoryPresets.h"
        EE_PRESET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")

    list(LENGTH presetFiles presetCount)
    message(STATUS "${TARGET}: ${presetCount} factory preset(s)")
endfunction()

# peak_add_plugin(<Target>
#     CODE        <four-letter plugin code>
#     PRODUCT     <"Peak Something">
#     BUNDLE      <com.synthpeak.something>
#     CATEGORIES  <VST3 category words>
#     [SOURCES    <extra .cpp beyond src/PluginProcessor.cpp>]
#     [LIBS       <extra link targets beyond ee_shared>]
#     [WEBVIEW])
#
# Every pedal is the same plugin with a different processor, so the twenty-odd
# lines of juce_add_plugin boilerplate they used to each carry live here once.
# Anything genuinely per-pedal is an argument.
#
# WEBVIEW opts a pedal into juce::WebBrowserComponent instead of the ee::ui
# analog/digital face: NEEDS_WEBVIEW2 on Windows, JUCE_WEB_BROWSER=1 instead of
# the default 0, and the caller is responsible for linking juce::juce_gui_extra
# itself (it is not part of ee_shared).
function(peak_add_plugin TARGET)
    cmake_parse_arguments(ARG "WEBVIEW" "CODE;PRODUCT;BUNDLE;CATEGORIES" "SOURCES;LIBS" ${ARGN})

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
        NEEDS_WEBVIEW2          ${ARG_WEBVIEW}
        COPY_PLUGIN_AFTER_BUILD ${EE_INSTALL_PLUGINS})

    target_sources(${TARGET} PRIVATE src/PluginProcessor.cpp ${ARG_SOURCES})

    peak_add_factory_presets(${TARGET})

    if(ARG_WEBVIEW)
        set(webBrowserFlag 1)
    else()
        set(webBrowserFlag 0)
    endif()

    # A web face reads its page from the built jsui/dist by default, so a plugin
    # installed into ~/Library works in a DAW on its own. EE_JSUI_DEV_SERVER
    # points it at the Vite dev server instead, for the hot-reload loop - never
    # ship one: with no server running the editor has to fall back, and a face
    # that depends on a process the user has to remember to start is how you get
    # a blank plugin window in Ableton.
    if(ARG_WEBVIEW AND EE_JSUI_DEV_SERVER)
        set(devServerFlag 1)
    else()
        set(devServerFlag 0)
    endif()

    # The face's third save button, which writes a preset into the pedal's own
    # presets/ source folder for committing. An authoring tool, not a feature:
    # off unless asked for, like the tuning panels.
    if(EE_PRESET_AUTHOR)
        set(presetAuthorFlag 1)
    else()
        set(presetAuthorFlag 0)
    endif()

    target_compile_definitions(${TARGET} PUBLIC
        EE_PRESET_AUTHOR=${presetAuthorFlag}
        JUCE_WEB_BROWSER=${webBrowserFlag}
        EE_JSUI_DEV_SERVER=${devServerFlag}
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
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
