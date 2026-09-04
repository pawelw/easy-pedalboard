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
    elseif(NOT CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL EE_ARCH_AUTOSET)
        # Build for the machine, not for whatever architecture cmake happens to
        # be running as.
        #
        # An x86_64 cmake on Apple Silicon - easy to end up with, since an Intel
        # Homebrew in /usr/local takes precedence over /opt/homebrew on a default
        # PATH - runs under Rosetta and defaults every target to x86_64. The
        # build succeeds, the plugins install, and then a natively-running DAW
        # cannot load a single one of them: Logic and Live report only that the
        # component could not be opened, with nothing about why.
        #
        # sysctl reports the hardware whether or not this process is translated,
        # so it is the one thing here that cannot be fooled by the toolchain.
        execute_process(COMMAND sysctl -n hw.optional.arm64
                        OUTPUT_VARIABLE ee_host_is_arm64
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)

        if(ee_host_is_arm64 STREQUAL "1" AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
            # Both, not just arm64. When the toolchain and the hardware disagree
            # there is no way to tell from here which architecture the host
            # application is running as - a DAW on Apple Silicon may be native
            # or may be launched under Rosetta, and it can only load a plugin
            # that matches. A thin bundle in either direction is invisible to
            # half the possibilities, and the failure looks the same both ways:
            # "this Audio Unit could not be opened", with nothing about why.
            #
            # This costs a second compile pass. Installing an arm64 cmake
            # (`arch -arm64 /opt/homebrew/bin/brew install cmake`) removes the
            # mismatch and the penalty with it.
            set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

            # Remembered so a later configure can revise this choice. Without it
            # the value we forced last time is indistinguishable from one the
            # user asked for, and would be left alone for ever.
            set(EE_ARCH_AUTOSET "arm64;x86_64" CACHE INTERNAL "Architectures this project chose by itself")

            message(STATUS
                "Apple Silicon host with an ${CMAKE_SYSTEM_PROCESSOR} cmake: building universal, "
                "so the plugins load whether the host runs native or under Rosetta")
        endif()
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

# melatonin_blur: real gaussian drop / inner shadows for the UI (proper
# box-shadow model - radius, spread, layered, cached). Linked into ee_ui.
# The checkout dir must be named `melatonin_blur` (not the default `-src`
# suffix), or its `juce_add_module` cannot find the module header.
FetchContent_Declare(melatonin_blur
    GIT_REPOSITORY https://github.com/sudara/melatonin_blur.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/melatonin_blur)
FetchContent_MakeAvailable(melatonin_blur)

# Fonts are compiled in rather than installed, so the pedal looks the same on a
# machine that has never seen them.
juce_add_binary_data(ee_assets SOURCES
    assets/fonts/Birthstone-Regular.ttf
    assets/fonts/Lato-Light.ttf
    assets/images/peak-logo.png
    assets/images/knob.png
    assets/images/plate.png
    assets/images/silver-knob.png
    assets/images/silver-knob-base-v1.png
    assets/images/reverb-bg.jpeg
    assets/images/random.png
    assets/images/grain-icon.png
    assets/images/tape-icon.png
    assets/images/reverb-icon.png
    assets/images/pitch-icon.png
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
    peak-phase peak-overdrive peak-wah peak-tape peak-grain)

set(EE_PLUGINS "all" CACHE STRING
    "Semicolon-separated pedals to build, or \"all\". e.g. -DEE_PLUGINS=peak-wah;peak-tape")
option(EE_INSTALL_PLUGINS "Copy built plugins into ~/Library/Audio/Plug-Ins" ON)
option(EE_DEV_FORMATS "Build Standalone only, skipping VST3 and AU" OFF)
option(EE_LTO "Link with link-time optimisation (slow; for release builds)" ON)

# Bolt a live tuning panel onto a pedal. Never turn these on for a build you hand
# to anyone.
option(EE_SHIMMER_TUNER "Add the shimmer tuning panel to Peak Reverb (development only)" OFF)
option(EE_TAPE_TUNER "Add the tape tuning panel to Peak Delay (development only)" OFF)
option(EE_GRAIN_TUNER "Add the grain tuning panel to Peak Grain (development only)" OFF)
option(EE_GRAIN_TRACE "Log Peak Grain's in/out levels when its output runs away (development only)" OFF)

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