cmake_minimum_required(VERSION 3.22)

project(ChanSampler VERSION 1.2.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically, so the plugin works when dropped
# onto any PC (no Visual C++ redistributable needed).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# macOS universal builds: configure with
#   -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# The sources: the engine alone (what the tests need) and everything.
set(CHANSAMPLER_ENGINE_SOURCES
    Source/Params.cpp
    Source/SampleData.cpp
    Source/Dsp.cpp
    Source/SamplerEngine.cpp
    Source/TimeStretch.cpp)
set(CHANSAMPLER_SOURCES
    ${CHANSAMPLER_ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/CutGroups.cpp
    Source/FlPreset.cpp
    Source/PresetBar.cpp
    Source/FactoryPresets.cpp
    Source/PluginEditor.cpp
    Source/FlStyle.cpp
    Source/FlParts.cpp
    Source/SampleExchange.cpp
    Source/TempoDetect.cpp
    Source/WaveformView.cpp)
if(APPLE)
    list(APPEND CHANSAMPLER_SOURCES Source/SampleExchangeMac.mm)   # the macOS clipboard is Objective-C++
endif()

# Two of JUCE's recommended GCC / Clang warnings flag patterns this code uses
# on purpose: switches over a few cases with a default, and exact float
# comparisons (a value that is set, not computed: "is this LFO off?", "did it
# change?"). Linked after JUCE's warning flags, so these come later and win.
add_library(chansampler_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(chansampler_warnings INTERFACE -Wno-switch-enum -Wno-float-equal)
endif()

# The developer tools build the plugin's code as a console app, so they
# define what juce_add_plugin would.
set(CHANSAMPLER_TOOL_DEFINITIONS
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1
    JucePlugin_Name="Chan Sampler" JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)

# VST3 + Standalone everywhere; AU is only buildable on macOS.
set(CHANSAMPLER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CHANSAMPLER_FORMATS AU)
endif()

juce_add_plugin(ChanSampler
    COMPANY_NAME "ChanSampler"
    PLUGIN_MANUFACTURER_CODE Chsm
    PLUGIN_CODE Csm1
    FORMATS ${CHANSAMPLER_FORMATS}
    PRODUCT_NAME "Chan Sampler"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler)

target_sources(ChanSampler PRIVATE ${CHANSAMPLER_SOURCES})

# CLAP, through clap-juce-extensions (MIT; it brings the CLAP SDK).
option(CHANSAMPLER_CLAP "Build the CLAP plugin" ON)
if(CHANSAMPLER_CLAP)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d   # main, 2026-10
        GIT_SUBMODULES_RECURSE ON)
    FetchContent_MakeAvailable(clap_juce_extensions)
    clap_juce_extensions_plugin(TARGET ChanSampler
        CLAP_ID "com.chansampler.chansampler"
        CLAP_FEATURES instrument sampler stereo)
endif()

target_compile_definitions(ChanSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(ChanSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        chansampler_warnings)

# The developer tools (CONTRIBUTING.md). Offline engine tests: build target ChanSamplerTests and run it.
juce_add_console_app(ChanSamplerTests PRODUCT_NAME "ChanSamplerTests")
target_sources(ChanSamplerTests PRIVATE Tests/EngineTests.cpp ${CHANSAMPLER_ENGINE_SOURCES})
target_compile_definitions(ChanSamplerTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ChanSamplerTests PRIVATE
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    chansampler_warnings)

# Offline renderer used by nulltest/ to compare against FL Studio renders.
juce_add_console_app(ChanSamplerRender PRODUCT_NAME "ChanSamplerRender")
target_sources(ChanSamplerRender PRIVATE Tests/RenderTool.cpp ${CHANSAMPLER_SOURCES})
target_compile_definitions(ChanSamplerRender PRIVATE ${CHANSAMPLER_TOOL_DEFINITIONS})
target_link_libraries(ChanSamplerRender PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Editor snapshots for visual comparison with FL (Tests/SnapshotTool.cpp).
juce_add_console_app(ChanSamplerSnapshot PRODUCT_NAME "ChanSamplerSnapshot")
target_sources(ChanSamplerSnapshot PRIVATE Tests/SnapshotTool.cpp ${CHANSAMPLER_SOURCES})
target_compile_definitions(ChanSamplerSnapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 ${CHANSAMPLER_TOOL_DEFINITIONS})
target_link_libraries(ChanSamplerSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Hosts the built VST3 like a DAW: automation, MIDI, save / reload, editor size,
# and projects saved by earlier releases (Tests/data).
juce_add_console_app(ChanSamplerHostTest PRODUCT_NAME "ChanSamplerHostTest")
target_sources(ChanSamplerHostTest PRIVATE Tests/HostTool.cpp)
target_compile_definitions(ChanSamplerHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ChanSamplerHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    chansampler_warnings)
