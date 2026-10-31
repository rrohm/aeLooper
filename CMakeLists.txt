cmake_minimum_required(VERSION 3.22)

project(aeLooper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if present, otherwise fetch the pinned release.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "aeLooper: using local JUCE checkout")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Sources shared by the standalone app and the VST3 plugin
# ---------------------------------------------------------------------------
set(AELOOPER_COMMON_SOURCES
    src/ColorScheme.h
    src/LooperChannel.h
    src/LooperChannel.cpp
    src/LooperEngine.h
    src/LooperEngine.cpp
    src/MidiController.h
    src/MidiController.cpp
    src/PluginProcessor.h
    src/PluginProcessor.cpp
    src/PluginEditor.h
    src/PluginEditor.cpp
    src/UiPanel.h
    src/UiPanel.cpp
)

# ---------------------------------------------------------------------------
# Standalone application (ALSA/JACK audio, MIDI input via JUCE)
# ---------------------------------------------------------------------------
juce_add_gui_app(aeLooper
    PRODUCT_NAME "aeLooper"
    COMPANY_NAME "aeLooper project"
    BUNDLE_ID "com.aelooper.standalone")

target_sources(aeLooper PRIVATE
    src/Main.cpp
    ${AELOOPER_COMMON_SOURCES})

target_link_libraries(aeLooper PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_basics)

# juce_audio_utils -> juce_gui_extra pulls in GTK/WebKit headers when
# JUCE_WEB_BROWSER is auto-detected. We do not use the WebBrowserComponent,
# so switch it off to avoid the libgtk-3-dev dependency. Likewise, curl
# networking is auto-detected and would need -lcurl; a looper has no use
# for it, so it is switched off as well.
target_compile_definitions(aeLooper PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# ---------------------------------------------------------------------------
# VST3 plugin target (same engine code)
# ---------------------------------------------------------------------------
juce_add_plugin(aeLooperPlugin
    PRODUCT_NAME "aeLooper"
    COMPANY_NAME "aeLooper project"
    BUNDLE_ID "com.aelooper.plugin"
    PLUGIN_MANUFACTURER_CODE "Aelo"
    PLUGIN_CODE "AeLp"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    DESCRIPTION "4-channel MIDI-controllable audio looper")

target_sources(aeLooperPlugin PRIVATE
    ${AELOOPER_COMMON_SOURCES})

target_link_libraries(aeLooperPlugin PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics)
target_compile_definitions(aeLooperPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # New plugin, no VST2 release: use plain VST3 parameter IDs.
    JUCE_VST3_CAN_REPLACE_VST2=0)
