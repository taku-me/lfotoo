cmake_minimum_required(VERSION 3.22)
project(TurntableLFO VERSION 0.1.0 LANGUAGES C CXX)

# JUCE is fetched at configure time. Pin a tag for reproducibility; bump as needed.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TurntableLFO
    COMPANY_NAME            "atoTerminal"
    PLUGIN_MANUFACTURER_CODE Atot
    PLUGIN_CODE              Tlfo
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Turntable LFO"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(TurntableLFO PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(TurntableLFO PRIVATE cxx_std_17)

target_compile_definitions(TurntableLFO PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TurntableLFO
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
