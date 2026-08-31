juce_add_plugin(PeakOverdrive
    COMPANY_NAME            "Synth Peak"
    BUNDLE_ID               com.synthpeak.peakoverdrive
    PLUGIN_MANUFACTURER_CODE Peak
    PLUGIN_CODE             Povd
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Peak Overdrive"
    VST3_CATEGORIES         Fx Distortion
    AU_MAIN_TYPE            kAudioUnitType_Effect
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(PeakOverdrive PRIVATE
    src/PluginProcessor.cpp)

target_compile_definitions(PeakOverdrive PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PeakOverdrive
    PRIVATE
        ee_shared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
