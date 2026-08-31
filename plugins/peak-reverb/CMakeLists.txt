peak_add_plugin(PeakReverb
    CODE       Prvb
    PRODUCT    "Peak Reverb"
    BUNDLE     com.synthpeak.peakreverb
    CATEGORIES "Fx Reverb"
)
if(EE_SHIMMER_TUNER)
    target_sources(PeakReverb PRIVATE src/ShimmerTunerPanel.cpp)
    target_compile_definitions(PeakReverb PUBLIC EE_SHIMMER_TUNER=1)
    message(STATUS "Peak Reverb: shimmer tuning panel enabled - development build")
endif()
