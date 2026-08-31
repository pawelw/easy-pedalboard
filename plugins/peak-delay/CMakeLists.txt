juce_add_plugin(PeakDelay
    COMPANY_NAME            "Synth Peak"
    BUNDLE_ID               com.synthpeak.peakdelay
    PLUGIN_MANUFACTURER_CODE Peak
    PLUGIN_CODE             Pdly
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Peak Delay"
    VST3_CATEGORIES         Fx Delay
    AU_MAIN_TYPE            kAudioUnitType_Effect
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(PeakDelay PRIVATE
    src/PluginProcessor.cpp)

if(EE_TAPE_TUNER)
    target_sources(PeakDelay PRIVATE src/TapeTunerPanel.cpp)
    target_compile_definitions(PeakDelay PUBLIC EE_TAPE_TUNER=1)
    message(STATUS "Peak Delay: tape tuning panel enabled - development build")
endif()

target_compile_definitions(PeakDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PeakDelay
    PRIVATE
        ee_shared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
