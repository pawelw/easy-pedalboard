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

# Shared code compiles into every plugin target (see shared/CMakeLists.txt), so the
# same translation units are built a dozen-odd times. A compiler cache turns the
# repeats into hits and is the single biggest win on an incremental build. Used
# only if it is installed - `brew install ccache`.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
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

# Peak Tape's own assets: the noise floor it loops (several megabytes of
# recording) and the emblem on its face. Their own binary-data target, linked by
# that pedal alone instead of riding along in every plugin, and their own
# namespace and header, or they would collide with ee_assets' BinaryData.
juce_add_binary_data(ee_tape_assets
    NAMESPACE TapeAssets
    HEADER_NAME TapeAssets.h
    SOURCES
        assets/audio/tape-noise.wav
        assets/images/tape.png)
set_target_properties(ee_tape_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

add_subdirectory(shared)

# ---------------------------------------------------------------------------
# Which pedals get built, in which formats, and what happens to them afterwards.
# The defaults reproduce a full release build; the "fast" preset turns them down
# for iteration. See CLAUDE.md.
# ---------------------------------------------------------------------------
set(EE_ALL_PLUGINS
    peak-reverb peak-spring peak-delay peak-eq peak-trem-pan peak-chorus
    peak-phase peak-overdrive peak-wah peak-tape)

set(EE_PLUGINS "all" CACHE STRING
    "Semicolon-separated pedals to build, or \"all\". e.g. -DEE_PLUGINS=peak-wah;peak-tape")
option(EE_INSTALL_PLUGINS "Copy built plugins into ~/Library/Audio/Plug-Ins" ON)
option(EE_DEV_FORMATS "Build Standalone only, skipping VST3 and AU" OFF)
option(EE_LTO "Link with link-time optimisation (slow; for release builds)" ON)

# Bolt a live tuning panel onto a pedal. Never turn these on for a build you hand
# to anyone.
option(EE_SHIMMER_TUNER "Add the shimmer tuning panel to Peak Reverb (development only)" OFF)
option(EE_TAPE_TUNER "Add the tape tuning panel to Peak Delay (development only)" OFF)

include(cmake/AddPeakPlugin.cmake)

if(EE_PLUGINS STREQUAL "all")
    set(EE_PLUGINS_TO_BUILD ${EE_ALL_PLUGINS})
else()
    set(EE_PLUGINS_TO_BUILD ${EE_PLUGINS})
    foreach(plugin ${EE_PLUGINS_TO_BUILD})
        if(NOT plugin IN_LIST EE_ALL_PLUGINS)
            message(FATAL_ERROR
                "EE_PLUGINS: unknown pedal \"${plugin}\". Known: ${EE_ALL_PLUGINS}")
        endif()
    endforeach()
    message(STATUS "Building a subset of the pedals: ${EE_PLUGINS_TO_BUILD}")
endif()

foreach(plugin ${EE_PLUGINS_TO_BUILD})
    add_subdirectory(plugins/${plugin})
endforeach()

option(EE_BUILD_TESTS "Build the offline DSP test harness" ON)
if(EE_BUILD_TESTS)
    add_subdirectory(tests)
endif()