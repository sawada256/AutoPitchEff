cmake_minimum_required(VERSION 3.22)
project(AutoPitchEff VERSION 0.7.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(APE_BUILD_PLUGIN "Build VST3 and audio tools (downloads JUCE)" ON)
option(APE_SANITIZE "Enable address and undefined behaviour sanitizers" OFF)
if(APE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
add_library(ape_dsp STATIC Source/dsp/PitchEngine.cpp Source/dsp/Formant.cpp)
add_library(ape_key_dsp STATIC Source/key/KeyAnalyser.cpp Source/key/ScaleLink.cpp)
target_include_directories(ape_key_dsp PUBLIC Source)
target_link_libraries(ape_key_dsp PUBLIC ape_dsp)
set_target_properties(ape_key_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(ape_key_dsp PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(ape_dsp PUBLIC Source)
set_target_properties(ape_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(ape_dsp PRIVATE -Wall -Wextra -Wpedantic)
include(CTest)
if(BUILD_TESTING)
    add_executable(ape_dsp_tests Tests/DspTests.cpp)
    target_link_libraries(ape_dsp_tests PRIVATE ape_dsp)
    add_test(NAME dsp COMMAND ape_dsp_tests)
    add_executable(ape_key_tests Tests/KeyTests.cpp)
    target_link_libraries(ape_key_tests PRIVATE ape_key_dsp)
    add_test(NAME key-analysis COMMAND ape_key_tests)
    add_executable(ape_scale_link_tests Tests/ScaleLinkTests.cpp)
    target_link_libraries(ape_scale_link_tests PRIVATE ape_key_dsp)
    add_test(NAME scale-link COMMAND ape_scale_link_tests)
endif()
if(APE_BUILD_PLUGIN)
    include(FetchContent)
    if(APE_JUCE_PATH)
        add_subdirectory("${APE_JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce")
    else()
        FetchContent_Declare(JUCE
            URL https://codeload.github.com/juce-framework/JUCE/tar.gz/29396c22c93392d6738e021b83196283d6e4d850
            URL_HASH SHA256=97cc01e0fe15278f6edadc895a3cf0aac2fd98bcd023823384d5ca7025beb91c)
        FetchContent_MakeAvailable(JUCE)
    endif()
    juce_add_plugin(AutoPitchEff
        COMPANY_NAME "AutoPitchEff"
        BUNDLE_ID "io.github.sawada256.autopitcheff"
        PLUGIN_MANUFACTURER_CODE Apef
        PLUGIN_CODE Aptf
        FORMATS VST3
        PRODUCT_NAME "AutoPitchEff"
        DESCRIPTION "Low latency live vocal hard tuning"
        IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx PitchShift)
    target_sources(AutoPitchEff PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    if(APPLE)
        target_sources(AutoPitchEff PRIVATE Source/NativeVisibility.mm)
    endif()
    target_link_libraries(AutoPitchEff PRIVATE ape_dsp ape_key_dsp juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    target_compile_definitions(AutoPitchEff PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    juce_add_plugin(AutoScaleLink
        COMPANY_NAME "AutoPitchEff" BUNDLE_ID "io.github.sawada256.autoscalelink"
        PLUGIN_MANUFACTURER_CODE Apef PLUGIN_CODE Aslk
        FORMATS VST3 PRODUCT_NAME "AutoScaleLink"
        DESCRIPTION "Live musical key analysis and scale link"
        IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Analyzer)
    target_sources(AutoScaleLink PRIVATE Source/key/KeyProcessor.cpp Source/key/KeyEditor.cpp)
    if(APPLE)
        target_sources(AutoScaleLink PRIVATE Source/NativeVisibility.mm)
    endif()
    target_include_directories(AutoScaleLink PRIVATE Source)
    target_link_libraries(AutoScaleLink PRIVATE ape_key_dsp juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    target_compile_definitions(AutoScaleLink PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    add_executable(ape_editor_preview Tools/EditorPreview.cpp)
    target_link_libraries(ape_editor_preview PRIVATE AutoPitchEff)
    get_target_property(APE_JUCE_INCLUDES juce::juce_core INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(ape_editor_preview PRIVATE Source ${APE_JUCE_INCLUDES})
    target_compile_definitions(ape_editor_preview PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    add_executable(ape_processor_state_check Tools/ProcessorStateCheck.cpp)
    target_link_libraries(ape_processor_state_check PRIVATE AutoPitchEff)
    target_include_directories(ape_processor_state_check PRIVATE Source ${APE_JUCE_INCLUDES})
    target_compile_definitions(ape_processor_state_check PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    add_executable(ape_key_processor_check Tools/KeyProcessorCheck.cpp)
    target_link_libraries(ape_key_processor_check PRIVATE AutoScaleLink)
    target_include_directories(ape_key_processor_check PRIVATE Source ${APE_JUCE_INCLUDES})
    target_compile_definitions(ape_key_processor_check PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    if(BUILD_TESTING)
        add_test(NAME processor-state COMMAND ape_processor_state_check)
    endif()
    juce_add_console_app(ape_render PRODUCT_NAME "ape-render")
    target_sources(ape_render PRIVATE Tools/Render.cpp)
    target_link_libraries(ape_render PRIVATE ape_dsp juce::juce_audio_formats)
    target_compile_definitions(ape_render PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    juce_add_console_app(ape_plugin_check PRODUCT_NAME "ape-plugin-check")
    target_sources(ape_plugin_check PRIVATE Tools/PluginCheck.cpp)
    if(APPLE)
        target_sources(ape_plugin_check PRIVATE Tools/NativeBench.mm)
    endif()
    target_link_libraries(ape_plugin_check PRIVATE juce::juce_audio_utils)
    target_compile_definitions(ape_plugin_check PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    juce_add_console_app(ape_scale_pair_check PRODUCT_NAME "ape-scale-pair-check")
    target_sources(ape_scale_pair_check PRIVATE Tools/ScalePairCheck.cpp)
    if(APPLE)
        target_sources(ape_scale_pair_check PRIVATE Tools/NativeBench.mm)
    endif()
    target_link_libraries(ape_scale_pair_check PRIVATE ape_key_dsp juce::juce_audio_utils)
    target_compile_definitions(ape_scale_pair_check PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(APPLE)
        # JUCE generates moduleinfo.json after its initial signing step.
        # Seal the final resources so install/restore verification is reliable.
        foreach(plugin_target AutoPitchEff_VST3 AutoScaleLink_VST3)
            add_custom_command(TARGET ${plugin_target} POST_BUILD
                COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${plugin_target}>"
                COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${plugin_target}>"
                VERBATIM)
        endforeach()
    endif()
endif()
