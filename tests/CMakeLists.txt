include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# ---------------------------------------------------------------------------
# Plugin-level tests (link the JUCE shared code of the plugin)
# ---------------------------------------------------------------------------
add_executable(PluginTests
    PluginTests.cpp)

# The plugin's shared-code static library already contains the compiled JUCE
# modules; the tests only need the module headers on the include path.
target_include_directories(PluginTests PRIVATE ${JUCE_SOURCE_DIR}/modules)

target_link_libraries(PluginTests PRIVATE
    AuroraBloom
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

catch_discover_tests(PluginTests DISCOVERY_MODE PRE_TEST)
