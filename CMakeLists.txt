cmake_minimum_required(VERSION 3.22)

project(ChanSampler VERSION 0.1.0)

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

target_sources(ChanSampler PRIVATE
    Source/Params.cpp
    Source/SampleData.cpp
    Source/Dsp.cpp
    Source/SamplerEngine.cpp
    Source/TimeStretch.cpp
    Source/PluginProcessor.cpp
    Source/CutGroups.cpp
    Source/FlPreset.cpp
    Source/PresetBar.cpp
    Source/PluginEditor.cpp
    Source/FlStyle.cpp
    Source/FlParts.cpp
    Source/WaveformView.cpp
    Source/OsFile.cpp)

target_compile_definitions(ChanSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

# Offline engine tests: build target ChanSamplerTests and run it.
juce_add_console_app(ChanSamplerTests PRODUCT_NAME "ChanSamplerTests")
target_sources(ChanSamplerTests PRIVATE
    Tests/EngineTests.cpp
    Source/Params.cpp
    Source/SampleData.cpp
    Source/Dsp.cpp
    Source/SamplerEngine.cpp
    Source/TimeStretch.cpp)
target_compile_definitions(ChanSamplerTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ChanSamplerTests PRIVATE
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Offline renderer used by nulltest/ to compare against FL Studio renders.
juce_add_console_app(ChanSamplerRender PRODUCT_NAME "ChanSamplerRender")
target_sources(ChanSamplerRender PRIVATE
    Tests/RenderTool.cpp
    Source/Params.cpp
    Source/SampleData.cpp
    Source/Dsp.cpp
    Source/SamplerEngine.cpp
    Source/TimeStretch.cpp
    Source/PluginProcessor.cpp
    Source/CutGroups.cpp
    Source/FlPreset.cpp
    Source/PresetBar.cpp
    Source/PluginEditor.cpp
    Source/FlStyle.cpp
    Source/FlParts.cpp
    Source/WaveformView.cpp
    Source/OsFile.cpp)
target_compile_definitions(ChanSamplerRender PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1
    JucePlugin_Name="Chan Sampler" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
target_link_libraries(ChanSamplerRender PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Editor snapshots for visual comparison with FL (Tests/SnapshotTool.cpp).
juce_add_console_app(ChanSamplerSnapshot PRODUCT_NAME "ChanSamplerSnapshot")
target_sources(ChanSamplerSnapshot PRIVATE
    Tests/SnapshotTool.cpp
    Source/Params.cpp
    Source/SampleData.cpp
    Source/Dsp.cpp
    Source/SamplerEngine.cpp
    Source/TimeStretch.cpp
    Source/PluginProcessor.cpp
    Source/CutGroups.cpp
    Source/FlPreset.cpp
    Source/PresetBar.cpp
    Source/PluginEditor.cpp
    Source/FlStyle.cpp
    Source/FlParts.cpp
    Source/WaveformView.cpp
    Source/OsFile.cpp)
target_compile_definitions(ChanSamplerSnapshot PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1
    JucePlugin_Name="Chan Sampler" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
target_link_libraries(ChanSamplerSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

target_link_libraries(ChanSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Hosts the built VST3 like a DAW and checks automation, MIDI, save / reload and editor size.
juce_add_console_app(ChanSamplerHostTest PRODUCT_NAME "ChanSamplerHostTest")
target_sources(ChanSamplerHostTest PRIVATE Tests/HostTool.cpp)
target_compile_definitions(ChanSamplerHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ChanSamplerHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
