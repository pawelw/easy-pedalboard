cmake_minimum_required(VERSION 3.22)

project(SynthPeak VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

    # Universal binaries double build time, so keep them opt-in for local iteration
    # and switch them on for anything you hand to someone else.
    option(EE_UNIVERSAL_BINARY "Build arm64 + x86_64 universal binaries" OFF)
    if(EE_UNIVERSAL_BINARY)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(JUCE)

# DaisySP supplies the time-domain pitch shifter that feeds Peak Reverb's
# shimmer path. Pinned like JUCE; only the `DaisySP` static library target is
# defined by its CMake, so nothing else leaks into the build.
FetchContent_Declare(DaisySP
    GIT_REPOSITORY https://github.com/electro-smith/DaisySP.git
    GIT_TAG V1.0.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(DaisySP)

# Its own CMake never applies a language standard to the target (a stray PUBLIC
# keyword swallows it), so it would otherwise inherit our C++20 - which is fine,
# but pin it explicitly so a compiler default can't change it underneath us.
set_target_properties(DaisySP PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# chowdsp_wdf is the header-only Wave Digital Filter library behind Peak
# Overdrive's diode-clipper stage. It is pure headers, so we only fetch the
# sources and wrap them in our own INTERFACE target - its bundled CMake declares
# `cmake_minimum_required(VERSION 3.1)`, which current CMake rejects, and it is
# not needed for a header-only include. SOURCE_SUBDIR points at a directory with
# no CMakeLists.txt so FetchContent skips add_subdirectory entirely.
FetchContent_Declare(chowdsp_wdf
    GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_wdf.git
    GIT_TAG v1.0.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    SOURCE_SUBDIR include)
FetchContent_MakeAvailable(chowdsp_wdf)

add_library(chowdsp_wdf INTERFACE)
target_include_directories(chowdsp_wdf INTERFACE ${chowdsp_wdf_SOURCE_DIR}/include)

# Fonts are compiled in rather than installed, so the pedal looks the same on a
# machine that has never seen them.
juce_add_binary_data(ee_assets SOURCES
    assets/fonts/Birthstone-Regular.ttf
    assets/images/peak-logo.png
    assets/images/knob.png
    assets/images/plate.png
    assets/images/spoon.png)
set_target_properties(ee_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Peak Tape's noise floor is a recording played in a loop rather than
# synthesised hiss, and it is several megabytes - so it gets its own binary-data
# target, linked by that pedal alone instead of riding along in every plugin.
# Its own namespace and header, or it would collide with ee_assets' BinaryData.
juce_add_binary_data(ee_tape_noise
    NAMESPACE TapeNoiseData
    HEADER_NAME TapeNoiseData.h
    SOURCES assets/audio/tape-noise.wav)
set_target_properties(ee_tape_noise PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

add_subdirectory(shared)

# Bolts a live tuning panel onto Peak Reverb's shimmer path. Never turn this on
# for a build you hand to anyone.
option(EE_SHIMMER_TUNER "Add the shimmer tuning panel to Peak Reverb (development only)" OFF)
add_subdirectory(plugins/peak-reverb)

add_subdirectory(plugins/peak-eq)
add_subdirectory(plugins/peak-trem-pan)
add_subdirectory(plugins/peak-chorus)
add_subdirectory(plugins/peak-phase)
add_subdirectory(plugins/peak-overdrive)
add_subdirectory(plugins/peak-wah)
add_subdirectory(plugins/peak-tape)

# Bolts a live tuning panel onto Peak Delay. Never turn this on for a build you
# hand to anyone.
option(EE_TAPE_TUNER "Add the tape tuning panel to Peak Delay (development only)" OFF)
add_subdirectory(plugins/peak-delay)

option(EE_BUILD_TESTS "Build the offline DSP test harness" ON)
if(EE_BUILD_TESTS)
    add_subdirectory(tests)
endif()