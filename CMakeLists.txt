cmake_minimum_required(VERSION 3.22)
project(RedTune VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(RedTune
    COMPANY_NAME "DIY"
    PLUGIN_MANUFACTURER_CODE Diyy
    PLUGIN_CODE Rdtn
    FORMATS VST3 Standalone
    PRODUCT_NAME "RedTune"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(RedTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.h)

target_compile_definitions(RedTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(RedTune PRIVATE
    juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)
