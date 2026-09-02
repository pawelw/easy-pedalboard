peak_add_plugin(PeakGrain
    CODE       Pgrn
    PRODUCT    "Peak Grain"
    BUNDLE     com.synthpeak.peakgrain
    CATEGORIES "Fx Delay"
)
if(EE_GRAIN_TUNER)
    target_sources(PeakGrain PRIVATE src/GrainTunerPanel.cpp)
    target_compile_definitions(PeakGrain PUBLIC EE_GRAIN_TUNER=1)
    message(STATUS "Peak Grain: grain tuning panel enabled - development build")
endif()
if(EE_GRAIN_TRACE)
    target_compile_definitions(PeakGrain PUBLIC EE_GRAIN_TRACE=1)
    message(STATUS "Peak Grain: runaway trace log enabled - development build")
endif()
