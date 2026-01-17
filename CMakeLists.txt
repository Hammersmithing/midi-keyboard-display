cmake_minimum_required(VERSION 3.22)
project(MidiKeyboardDisplay VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory($ENV{HOME}/JUCE JUCE)

juce_add_plugin(MidiKeyboardDisplay
    COMPANY_NAME "ALDENHammersmith"
    BUNDLE_ID "com.ALDENHammersmith.MidiKeyboardDisplay"
    PLUGIN_MANUFACTURER_CODE Alde
    PLUGIN_CODE Mkbd
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "MIDI Keyboard Display"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

target_sources(MidiKeyboardDisplay PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_compile_definitions(MidiKeyboardDisplay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MidiKeyboardDisplay PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
