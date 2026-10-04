cmake_minimum_required(VERSION 3.24)
project(Doubler VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE)

FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "")

FetchContent_MakeAvailable(JUCE signalsmith_stretch)

set(copy_plugin_after_build TRUE)
if(DEFINED ENV{CI})
    set(copy_plugin_after_build FALSE)
endif()

juce_add_plugin(Doubler
    COMPANY_NAME Aeyoll
    BUNDLE_ID com.aeyoll.doubler
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${copy_plugin_after_build}
    PLUGIN_MANUFACTURER_CODE Aeyl
    PLUGIN_CODE Dblr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Doubler"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(Doubler
    PRIVATE
        src/PluginProcessor.cpp)

target_include_directories(Doubler PRIVATE src)

target_compile_definitions(Doubler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Doubler
    PRIVATE
        juce::juce_audio_utils
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_executable(check_doubler src/dsp/check_doubler.cpp)
target_include_directories(check_doubler PRIVATE src)
target_link_libraries(check_doubler PRIVATE signalsmith-stretch)
target_compile_features(check_doubler PRIVATE cxx_std_17)
if(NOT MSVC)
    target_compile_options(check_doubler PRIVATE -O2)
endif()
