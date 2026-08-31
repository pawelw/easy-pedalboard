juce_add_plugin(PeakWah
    COMPANY_NAME            "Synth Peak"
    BUNDLE_ID               com.synthpeak.peakwah
    PLUGIN_MANUFACTURER_CODE Peak
    PLUGIN_CODE             Pwah
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Peak Wah"
    VST3_CATEGORIES         Fx Filter
    AU_MAIN_TYPE            kAudioUnitType_Effect
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(PeakWah PRIVATE
    src/PluginProcessor.cpp)

target_compile_definitions(PeakWah PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PeakWah
    PRIVATE
        ee_shared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
