cmake_minimum_required(VERSION 3.22)

project(EasyEffects VERSION 0.10.0 LANGUAGES C CXX)

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

# DaisySP supplies the time-domain pitch shifter that feeds Easy Reverb's
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

# Fonts are compiled in rather than installed, so the pedal looks the same on a
# machine that has never seen them.
juce_add_binary_data(ee_assets SOURCES
    assets/fonts/Birthstone-Regular.ttf
    assets/images/rocket-logo.png)
set_target_properties(ee_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

add_subdirectory(shared)

# Bolts a live tuning panel onto Easy Reverb's shimmer path. Never turn this on
# for a build you hand to anyone.
option(EE_SHIMMER_TUNER "Add the shimmer tuning panel to Easy Reverb (development only)" OFF)
add_subdirectory(plugins/easy-reverb)

add_subdirectory(plugins/easy-eq)
add_subdirectory(plugins/easy-trem-pan)

# Bolts a live tuning panel onto Easy Delay. Never turn this on for a build you
# hand to anyone.
option(EE_TAPE_TUNER "Add the tape tuning panel to Easy Delay (development only)" OFF)
add_subdirectory(plugins/easy-delay)

option(EE_BUILD_TESTS "Build the offline DSP test harness" ON)
if(EE_BUILD_TESTS)
    add_subdirectory(tests)
endif()