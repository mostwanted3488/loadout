cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
project(LOADOUT VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(LOADOUT_FORMATS VST3 AU)
else()
    set(LOADOUT_FORMATS VST3)
endif()

juce_add_plugin(LOADOUT
    COMPANY_NAME                "BUYORBEALOSER"
    COMPANY_WEBSITE             "https://github.com/mostwanted3488/loadout"
    BUNDLE_ID                   com.buyorbealoser.loadout
    PLUGIN_MANUFACTURER_CODE    Bobl
    PLUGIN_CODE                 Ldot
    PRODUCT_NAME                "LOADOUT"
    FORMATS                     ${LOADOUT_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Tools
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect)

juce_generate_juce_header(LOADOUT)

target_sources(LOADOUT PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginLibrary.cpp
    Source/WheelOverlay.cpp
    Source/UISounds.cpp)

if(APPLE)
    target_sources(LOADOUT PRIVATE Source/Platform_mac.mm)
    set_source_files_properties(Source/Platform_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(LOADOUT PRIVATE "-weak_framework ScreenCaptureKit" "-framework AppKit" "-framework CoreGraphics")
elseif(WIN32)
    target_sources(LOADOUT PRIVATE Source/Platform_win.cpp)
    target_link_libraries(LOADOUT PRIVATE gdi32 winmm)
else()
    target_sources(LOADOUT PRIVATE Source/Platform_other.cpp)
endif()

target_compile_definitions(LOADOUT PUBLIC
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(LOADOUT PRIVATE /utf-8 /bigobj)
endif()

target_link_libraries(LOADOUT
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_opengl
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
