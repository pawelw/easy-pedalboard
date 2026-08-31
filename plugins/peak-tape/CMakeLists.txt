juce_add_plugin(PeakTape
    COMPANY_NAME            "Synth Peak"
    BUNDLE_ID               com.synthpeak.peaktape
    PLUGIN_MANUFACTURER_CODE Peak
    PLUGIN_CODE             Ptap
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Peak Tape"
    VST3_CATEGORIES         Fx Distortion
    AU_MAIN_TYPE            kAudioUnitType_Effect
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(PeakTape PRIVATE
    src/PluginProcessor.cpp)

target_compile_definitions(PeakTape PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PeakTape
    PRIVATE
        ee_shared
        ee_tape_assets
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
