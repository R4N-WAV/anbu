cmake_minimum_required(VERSION 3.22)

project(Anbu VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static MSVC runtime: no Visual C++ Redistributable needed on the target machine.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE: local folder (ANBU_JUCE_DIR) or fetched automatically from GitHub
if(ANBU_JUCE_DIR)
    add_subdirectory(${ANBU_JUCE_DIR} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.8
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ANBU_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(UNIX AND NOT APPLE)   # only for test builds in the Linux build environment
    list(APPEND ANBU_COMMON_DEFS
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
        JUCE_USE_XRENDER=0 JUCE_USE_XSHM=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

# Panel artwork (Preview B master and the live layers cut from it), compiled into the plugin
file(GLOB ANBU_GUI_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/gui/*)
juce_add_binary_data(AnbuGui HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES ${ANBU_GUI_FILES})
set_target_properties(AnbuGui PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(Anbu
    PRODUCT_NAME                "Anbu"
    COMPANY_NAME                "R4N"
    BUNDLE_ID                   "com.r4n.anbu"
    PLUGIN_MANUFACTURER_CODE    R4Nx
    PLUGIN_CODE                 Anb1
    FORMATS                     VST3
    VST3_CATEGORIES             Fx Dynamics
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VERSION                     ${PROJECT_VERSION})

target_sources(Anbu PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(Anbu PRIVATE Source)
target_compile_definitions(Anbu PUBLIC ${ANBU_COMMON_DEFS})
target_link_libraries(Anbu
    PRIVATE AnbuGui juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Offline renderer: runs the real plugin processor over a raw float32 stereo file (for measurements)
option(ANBU_BUILD_TOOLS "Build the offline render tool" OFF)
if(ANBU_BUILD_TOOLS)
    juce_add_console_app(AnbuRender PRODUCT_NAME "AnbuRender")
    target_sources(AnbuRender PRIVATE Tests/AnbuRender.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(AnbuRender PRIVATE Source)
    target_compile_definitions(AnbuRender PRIVATE ${ANBU_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(AnbuRender PRIVATE AnbuGui juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()

option(ANBU_BUILD_SNAPSHOT "Build the headless editor snapshot tool" OFF)
if(ANBU_BUILD_SNAPSHOT)
    juce_add_console_app(AnbuSnapshot PRODUCT_NAME "AnbuSnapshot")
    target_sources(AnbuSnapshot PRIVATE Tests/AnbuSnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(AnbuSnapshot PRIVATE Source)
    target_compile_definitions(AnbuSnapshot PRIVATE ${ANBU_COMMON_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AnbuSnapshot PRIVATE AnbuGui juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
