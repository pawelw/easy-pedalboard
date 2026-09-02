peak_add_plugin(PeakDelay
    CODE       Pdly
    PRODUCT    "Peak Delay"
    BUNDLE     com.synthpeak.peakdelay
    CATEGORIES "Fx Delay"
)
if(EE_TAPE_TUNER)
    target_sources(PeakDelay PRIVATE src/TapeTunerPanel.cpp)
    target_compile_definitions(PeakDelay PUBLIC EE_TAPE_TUNER=1)
    message(STATUS "Peak Delay: tape tuning panel enabled - development build")
endif()
