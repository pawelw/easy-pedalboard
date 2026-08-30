juce_add_plugin(EasyReverb
    COMPANY_NAME            "Easy Effects"
    BUNDLE_ID               com.easyeffects.easyreverb
    PLUGIN_MANUFACTURER_CODE Eefx
    PLUGIN_CODE             Ervb
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Easy Reverb"
    VST3_CATEGORIES         Fx Reverb
    AU_MAIN_TYPE            kAudioUnitType_Effect
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(EasyReverb PRIVATE
    src/PluginProcessor.cpp)

if(EE_SHIMMER_TUNER)
    target_sources(EasyReverb PRIVATE src/ShimmerTunerPanel.cpp)
    target_compile_definitions(EasyReverb PUBLIC EE_SHIMMER_TUNER=1)
    message(STATUS "Easy Reverb: shimmer tuning panel enabled - development build")
endif()

target_compile_definitions(EasyReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(EasyReverb
    PRIVATE
        ee_shared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
