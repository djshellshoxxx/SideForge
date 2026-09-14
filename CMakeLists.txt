cmake_minimum_required(VERSION 3.22)

project(SideForge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "SideForge targets Windows x64 only.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(JUCE)

# ---------------------------------------------------------------------------
# Core sources: DSP + model + parameters + utilities.
# Compiled into both the plugin and the test runner (cheap, and avoids the
# duplicate-symbol hazard of putting JUCE module sources in a static lib).
# ---------------------------------------------------------------------------
set(SIDEFORGE_CORE_SOURCES
    Source/Parameters/ParameterLayout.cpp
    Source/Model/CurveModel.cpp
    Source/Model/PresetState.cpp
)

set(SIDEFORGE_UI_SOURCES
)

juce_add_plugin(SideForge
    COMPANY_NAME            "SideForge Audio"
    BUNDLE_ID               com.sideforge.sideforge
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Sfrg
    PLUGIN_CODE             Sdfg
    FORMATS                 VST3 Standalone
    VST3_CATEGORIES         Fx Dynamics
    PRODUCT_NAME            "SideForge")

target_sources(SideForge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${SIDEFORGE_CORE_SOURCES}
    ${SIDEFORGE_UI_SOURCES})

target_compile_definitions(SideForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_include_directories(SideForge PRIVATE Source)

target_link_libraries(SideForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(SideForge PRIVATE /W4 /wd4244 /wd4267 /MP)
endif()

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------
juce_add_console_app(SideForgeTests PRODUCT_NAME "SideForgeTests")

file(GLOB_RECURSE SIDEFORGE_TEST_SOURCES CONFIGURE_DEPENDS Tests/*.cpp)

target_sources(SideForgeTests PRIVATE
    ${SIDEFORGE_TEST_SOURCES}
    ${SIDEFORGE_CORE_SOURCES}
    ${SIDEFORGE_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SideForgeTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_include_directories(SideForgeTests PRIVATE Source Tests)

target_link_libraries(SideForgeTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(SideForgeTests PRIVATE /W4 /wd4244 /wd4267 /MP)
endif()

enable_testing()
add_test(NAME SideForgeUnitTests COMMAND SideForgeTests)
