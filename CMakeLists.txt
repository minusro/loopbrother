cmake_minimum_required(VERSION 3.22)

# Build universal binaries on macOS unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(junglemassive VERSION 0.8.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(JM_BUILD_PLUGIN "Build the JUCE plugin targets" ON)
option(JM_BUILD_TESTS  "Build the plain-C++ engine tests" ON)
set(JM_JUCE_DIR "" CACHE PATH "Local JUCE git checkout to clone 8.0.4 from (empty = GitHub); never modified")

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
    # JUCE 8.0.4 with two window-resize patches, both handing the decision to
    # the editor's constrainer (jmui::WindowConstrainer):
    # - cmake/juce-vst3-resize.patch: VST3 hosts ask it, told which sides
    #   changed, instead of JUCE fitting the ratio to the shorter side (which
    #   made edges shrink-only).
    # - cmake/juce-standalone-resize.patch: the standalone's window keeps the
    #   placement it chooses instead of pinning the top-left corner.
    # The clone lives in the build tree, so a local JM_JUCE_DIR is never
    # touched; the patch step resets it first, so it can always run again.
    include(FetchContent)
    if(JM_JUCE_DIR)
        set(JM_JUCE_GIT ${JM_JUCE_DIR})
    else()
        set(JM_JUCE_GIT https://github.com/juce-framework/JUCE.git)
    endif()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY      ${JM_JUCE_GIT}
        GIT_TAG             8.0.4
        GIT_SHALLOW         TRUE
        UPDATE_DISCONNECTED TRUE
        PATCH_COMMAND       git checkout -- .
                    COMMAND git apply --whitespace=nowarn
                            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/juce-vst3-resize.patch
                            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/juce-standalone-resize.patch)
    FetchContent_MakeAvailable(JUCE)

    # The breaks and their pad layout are compiled into the plugin.
    file(GLOB JM_BREAK_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/breaks/*.wav)
    juce_add_binary_data(jm_breaks
        HEADER_NAME BreakData.h
        NAMESPACE   BreakData
        SOURCES     ${JM_BREAK_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/breaks/layout.json
                    ${CMAKE_CURRENT_SOURCE_DIR}/breaks/colours.json)

    set(JM_FORMATS VST3)
    if(APPLE)
        list(APPEND JM_FORMATS AU Standalone)
    endif()

    # LÖÖP BRÖTHER (formerly Jungle Massive). The name hosts and files see
    # is ASCII, LOOP BROTHER: the VST3 SDK's moduleinfo.json writer garbles
    # non-ASCII names (each UTF-8 byte sign-extended into its own
    # character), and hosts list plugins from that file. The umlauts are in
    # the editor's logo. The codes and bundle ID are unchanged, so saved
    # sessions still find it.
    set(JM_PLUGIN_NAME "LOOP BROTHER")
    juce_add_plugin(junglemassive
        COMPANY_NAME                "minus"
        PLUGIN_MANUFACTURER_CODE    Mnro
        PLUGIN_CODE                 Jngl
        PRODUCT_NAME                "${JM_PLUGIN_NAME}"
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
        src/PluginEditor.cpp
        src/Cereal.cpp)

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
            JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(jm_host_check PRIVATE
            juce::juce_audio_processors juce::juce_audio_formats
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endif()

    # Renders the editor to a PNG (no window, no audio device) for UI checks.
    option(JM_BUILD_SNAPSHOT "Build the editor snapshot tool (tests/snapshot.cpp)" OFF)
    if(JM_BUILD_SNAPSHOT)
        juce_add_console_app(jm_snapshot PRODUCT_NAME "jm_snapshot")
        target_sources(jm_snapshot PRIVATE
            tests/snapshot.cpp src/LoopLibrary.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Cereal.cpp)
        target_compile_definitions(jm_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
            JucePlugin_Name="${JM_PLUGIN_NAME}")
        target_link_libraries(jm_snapshot PRIVATE
            jm_engine jm_breaks juce::juce_audio_utils
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endif()
endif()
