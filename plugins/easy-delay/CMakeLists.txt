juce_add_plugin(EasyDelay
    COMPANY_NAME            "Easy Effects"
    BUNDLE_ID               com.easyeffects.easydelay
    PLUGIN_MANUFACTURER_CODE Eefx
    PLUGIN_CODE             Edly
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Easy Delay"
    VST3_CATEGORIES         Fx Delay
    AU_MAIN_TYPE            kAudioUnitType_Effect
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(EasyDelay PRIVATE
    src/PluginProcessor.cpp)

target_compile_definitions(EasyDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(EasyDelay
    PRIVATE
        ee_shared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
