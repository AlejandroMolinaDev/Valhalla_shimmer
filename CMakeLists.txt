cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# Platform settings that must be defined before project()
# ---------------------------------------------------------------------------

# Windows: static MSVC runtime (/MT, /MTd) so the .vst3 never depends on the
# Visual C++ redistributable being installed on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# macOS: universal binary (Apple Silicon + Intel), minimum macOS 10.13.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

include(cmake/Version.cmake)
include(cmake/CompilerWarnings.cmake)

project(AuroraBloom VERSION ${AB_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AB_BUILD_TESTS "Build the Catch2 test suites" ON)
option(AB_COPY_AFTER_BUILD "Copy the plugin to the system plugin folder after building" ON)

# Keep JUCE helper targets out of IDE root folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# DSP core: plain C++20, no JUCE dependency (fast, deterministic unit tests)
# ---------------------------------------------------------------------------
add_library(AuroraBloomDSP STATIC
    source/dsp/DiffuserNetwork.cpp
    source/dsp/GrainPitchShifter.cpp
    source/dsp/LoopFilters.cpp
    source/dsp/ShimmerEngine.cpp)

target_include_directories(AuroraBloomDSP PUBLIC source)
target_compile_features(AuroraBloomDSP PUBLIC cxx_std_20)
ab_set_strict_warnings(AuroraBloomDSP)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(AuroraBloom
    PRODUCT_NAME "Aurora Bloom"
    COMPANY_NAME "Aurora Bloom Audio"
    BUNDLE_ID dev.aurorabloom.plugin
    VERSION ${AB_VERSION}
    DESCRIPTION "Stereo algorithmic shimmer reverb"
    PLUGIN_MANUFACTURER_CODE Abau
    PLUGIN_CODE Ablm
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${AB_COPY_AFTER_BUILD})

target_sources(AuroraBloom PRIVATE
    source/Parameters.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)

target_include_directories(AuroraBloom PUBLIC source)

target_compile_definitions(AuroraBloom PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(AuroraBloom
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        AuroraBloomDSP
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
