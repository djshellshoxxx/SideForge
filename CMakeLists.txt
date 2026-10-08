cmake_minimum_required(VERSION 3.22)

project(SideForge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# CLAP is built through clap-juce-extensions. Point this at a checkout (with its
# submodules) to add the CLAP format; leave it empty for VST3 + Standalone only.
set(SIDEFORGE_CLAP_EXTENSIONS_DIR "" CACHE PATH "Path to a clap-juce-extensions checkout")

# Link-time optimisation buys a few percent on this code but costs roughly a
# quarter of an hour per link, which is too slow to iterate against. It is off by
# default and turned on for the artefacts that actually ship:
#   cmake -S . -B build -DSIDEFORGE_ENABLE_LTO=ON ...
option(SIDEFORGE_ENABLE_LTO "Enable link-time optimisation for Release builds" OFF)

add_subdirectory(JUCE)

if(SIDEFORGE_CLAP_EXTENSIONS_DIR)
    add_subdirectory(${SIDEFORGE_CLAP_EXTENSIONS_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# Core sources: DSP + model + parameters + utilities.
# Compiled into both the plugin and the test runner (cheap, and avoids the
# duplicate-symbol hazard of putting JUCE module sources in a static lib).
# ---------------------------------------------------------------------------
set(SIDEFORGE_CORE_SOURCES
    Source/Parameters/ParameterLayout.cpp
    Source/Parameters/MacroMapping.cpp
    Source/Parameters/BandMatrixParameters.cpp
    Source/Model/CurveModel.cpp
    Source/Model/PresetState.cpp
    Source/Model/FactoryPresets.cpp
    Source/DSP/SidechainDetector.cpp
    Source/DSP/CompressorEngine.cpp
    Source/DSP/CurveEngine.cpp
    Source/DSP/TriggerEngine.cpp
    Source/DSP/MultibandProcessor.cpp
    Source/DSP/SpectralDucker.cpp
    Source/DSP/BandProcessor.cpp
    Source/DSP/BandMatrix.cpp
    Source/Parameters/MidiLearn.cpp
)

# UI sources. Linked into the test runner too, so the tests can open the real
# editor rather than a stand-in.
set(SIDEFORGE_UI_SOURCES
    Source/UI/LookAndFeel.cpp
    Source/UI/ParameterContextMenu.cpp
    Source/UI/Controls.cpp
    Source/UI/MacroControls.cpp
    Source/UI/MeterComponent.cpp
    Source/UI/Visualizer.cpp
    Source/UI/SpectrumView.cpp
    Source/UI/CurveEditor.cpp
    Source/UI/Panels.cpp
    Source/UI/BandMatrixView.cpp
    Source/UI/BandMatrixPanel.cpp
    Source/UI/MainView.cpp
)

juce_add_plugin(SideForge
    ICON_BIG                "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_512.png"
    ICON_SMALL              "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_64.png"
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
        juce::juce_recommended_warning_flags)

if(SIDEFORGE_CLAP_EXTENSIONS_DIR)
    clap_juce_extensions_plugin(TARGET SideForge
        CLAP_ID "com.sideforge.sideforge"
        CLAP_FEATURES audio-effect dynamics-processor)
endif()

if(SIDEFORGE_ENABLE_LTO)
    target_link_libraries(SideForge PUBLIC juce::juce_recommended_lto_flags)
endif()

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

# ---------------------------------------------------------------------------
# VST3 validation host
#
# pluginval is not available here, so this small host loads the built .vst3
# through JUCE's own VST3 hosting code - the same path a DAW takes - and checks
# scanning, instantiation, bus layouts, processing, state and the editor.
# ---------------------------------------------------------------------------
juce_add_console_app(SideForgeValidate PRODUCT_NAME "SideForgeValidate")

target_sources(SideForgeValidate PRIVATE Tools/ValidateVst3.cpp)

target_compile_definitions(SideForgeValidate PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_PLUGINHOST_VST3=1)

target_link_libraries(SideForgeValidate
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(SideForgeValidate PRIVATE /W4 /wd4244 /wd4267 /MP)
endif()

add_dependencies(SideForgeValidate SideForge_All)

enable_testing()
add_test(NAME SideForgeUnitTests COMMAND SideForgeTests)
add_test(NAME SideForgeVst3Validation COMMAND SideForgeValidate)
