cmake_minimum_required(VERSION 3.22)

# Build universal binaries on macOS unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(junglemassive VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(JM_BUILD_PLUGIN "Build the JUCE plugin targets" ON)
option(JM_BUILD_TESTS  "Build the plain-C++ engine tests" ON)
set(JM_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (empty = fetch JUCE 8.0.4)")

# ---------------------------------------------------------------------------
# Pure C++ loop engine: no JUCE dependency, so playback timing can be unit
# tested with bare g++.
# ---------------------------------------------------------------------------
add_library(jm_engine INTERFACE)
target_include_directories(jm_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(JM_BUILD_TESTS)
    enable_testing()
    add_executable(jm_tests tests/engine_tests.cpp)
    target_link_libraries(jm_tests PRIVATE jm_engine)
    target_compile_options(jm_tests PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -O2>)
    add_test(NAME engine_tests COMMAND jm_tests)
endif()

# ---------------------------------------------------------------------------
# JUCE plugin
# ---------------------------------------------------------------------------
if(JM_BUILD_PLUGIN)
    if(JM_JUCE_DIR)
        add_subdirectory(${JM_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.4
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    # The breaks and their pad layout are compiled into the plugin.
    file(GLOB JM_BREAK_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/breaks/*.wav)
    juce_add_binary_data(jm_breaks
        HEADER_NAME BreakData.h
        NAMESPACE   BreakData
        SOURCES     ${JM_BREAK_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/breaks/layout.json)

    set(JM_FORMATS VST3)
    if(APPLE)
        list(APPEND JM_FORMATS AU Standalone)
    endif()

    juce_add_plugin(junglemassive
        COMPANY_NAME                "minusro"
        PLUGIN_MANUFACTURER_CODE    Mnro
        PLUGIN_CODE                 Jngl
        PRODUCT_NAME                "Jungle Massive"
        BUNDLE_ID                   "com.minusro.junglemassive"
        FORMATS                     ${JM_FORMATS}
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           FALSE
        VST3_CATEGORIES             Instrument Sampler
        AU_MAIN_TYPE                kAudioUnitType_MusicDevice
        COPY_PLUGIN_AFTER_BUILD     TRUE)

    target_sources(junglemassive PRIVATE
        src/LoopLibrary.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

    target_compile_definitions(junglemassive PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if(UNIX AND NOT APPLE)
        target_compile_definitions(junglemassive PUBLIC
            JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0
            JUCE_ALSA=0 JUCE_JACK=0)
    endif()

    target_link_libraries(junglemassive
        PRIVATE
            jm_engine
            jm_breaks
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # End-to-end check: hosts the built VST3 with a scripted transport.
    option(JM_BUILD_HOST_CHECK "Build the plugin host check (tests/host_check.cpp)" OFF)
    if(JM_BUILD_HOST_CHECK)
        juce_add_console_app(jm_host_check PRODUCT_NAME "jm_host_check")
        target_sources(jm_host_check PRIVATE tests/host_check.cpp)
        target_compile_definitions(jm_host_check PRIVATE
            JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(jm_host_check PRIVATE
            juce::juce_audio_processors juce::juce_audio_formats
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endif()

    # Renders the editor to a PNG (no window, no audio device) for UI checks.
    option(JM_BUILD_SNAPSHOT "Build the editor snapshot tool (tests/snapshot.cpp)" OFF)
    if(JM_BUILD_SNAPSHOT)
        juce_add_console_app(jm_snapshot PRODUCT_NAME "jm_snapshot")
        target_sources(jm_snapshot PRIVATE
            tests/snapshot.cpp src/LoopLibrary.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
        target_compile_definitions(jm_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(jm_snapshot PRIVATE
            jm_engine jm_breaks juce::juce_audio_utils
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endif()
endif()
