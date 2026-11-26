cmake_minimum_required(VERSION 3.22)
project(TITSTRIPPER VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# La imagen de fondo se incrusta en el plugin (BinaryData::background_jpg), la misma que TIT COMP y TIT Q
juce_add_binary_data(TITSTRIPPERAssets SOURCES background.jpg)

# Formatos: VST3 (Windows y macOS) y AU (solo macOS). Sin AAX.
set(TITSTRIPPER_FORMATS VST3)
if(APPLE)
    list(APPEND TITSTRIPPER_FORMATS AU)
endif()

juce_add_plugin(TITSTRIPPER
    COMPANY_NAME            "TIT Audio"
    PLUGIN_MANUFACTURER_CODE Titc
    PLUGIN_CODE             Tstr
    FORMATS                 ${TITSTRIPPER_FORMATS}
    AU_MAIN_TYPE            kAudioUnitType_Effect
    PRODUCT_NAME            "TIT STRIPPER"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Fx Dynamics EQ)

target_sources(TITSTRIPPER PRIVATE
    strip/StripProcessor.cpp
    strip/StripEditor.cpp
    gate/GateProcessor.cpp
    gate/GateEditor.cpp
    comp/Parameters.cpp
    comp/PluginProcessor.cpp
    comp/PluginEditor.cpp
    q/Parameters.cpp
    q/PluginProcessor.cpp
    q/PluginEditor.cpp)

# SpectrumAnalyzer.h, Biquad.h, EqBand.h y CutTable.h son comunes a los modulos
target_include_directories(TITSTRIPPER PRIVATE common)

target_compile_definitions(TITSTRIPPER PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TITSTRIPPER
    PRIVATE
        TITSTRIPPERAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
