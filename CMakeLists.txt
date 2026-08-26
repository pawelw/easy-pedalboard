cmake_minimum_required(VERSION 3.22)

project(EasyEffects VERSION 0.2.0 LANGUAGES C CXX)

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

add_subdirectory(shared)
add_subdirectory(plugins/easy-reverb)

option(EE_BUILD_TESTS "Build the offline DSP test harness" ON)
if(EE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
