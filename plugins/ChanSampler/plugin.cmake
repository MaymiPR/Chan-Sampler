# Chan Sampler (recreation of FL Studio's Channel Sampler). Included from the top-level CMakeLists.txt.
set(CHANSAMPLER_DIR ${CMAKE_CURRENT_LIST_DIR})

# The sources: the engine alone (what the tests need) and everything.
set(CHANSAMPLER_ENGINE_SOURCES
    ${CHANSAMPLER_DIR}/Source/Params.cpp
    ${CHANSAMPLER_DIR}/Source/History.cpp
    ${CHANSAMPLER_DIR}/Source/dsp/SampleData.cpp
    ${CHANSAMPLER_DIR}/Source/dsp/Dsp.cpp
    ${CHANSAMPLER_DIR}/Source/dsp/SamplerEngine.cpp
    ${CHANSAMPLER_DIR}/Source/dsp/TimeStretch.cpp)
set(CHANSAMPLER_SOURCES
    ${CHANSAMPLER_ENGINE_SOURCES}
    ${CHANSAMPLER_DIR}/Source/PluginProcessor.cpp
    ${CHANSAMPLER_DIR}/Source/HostFocus.cpp
    ${CHANSAMPLER_DIR}/Source/CutGroups.cpp
    ${CHANSAMPLER_DIR}/Source/PresetImport.cpp
    ${CHANSAMPLER_DIR}/Source/ui/PresetBar.cpp
    ${CHANSAMPLER_DIR}/Source/Presets.cpp
    ${CHANSAMPLER_DIR}/Source/PluginEditor.cpp
    ${CHANSAMPLER_DIR}/Source/ui/Style.cpp
    ${CHANSAMPLER_DIR}/Source/ui/Parts.cpp
    ${CHANSAMPLER_DIR}/Source/SampleExchange.cpp
    ${CHANSAMPLER_DIR}/Source/dsp/TempoDetect.cpp
    ${CHANSAMPLER_DIR}/Source/ui/WaveformView.cpp)
if(APPLE)
    list(APPEND CHANSAMPLER_SOURCES ${CHANSAMPLER_DIR}/Source/SampleExchangeMac.mm)   # the macOS clipboard is Objective-C++
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

# The manufacturer and plug-in codes make the plug-in's identity in hosts (saved projects find it by them): keep
# them. packaging/plugins.txt repeats them for the installers and the release workflow.
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
target_include_directories(ChanSampler PRIVATE ${CHANSAMPLER_DIR}/Source)

if(CHANSAMPLER_CLAP)
    clap_juce_extensions_plugin(TARGET ChanSampler
        CLAP_ID "com.chansampler.chansampler"
        CLAP_FEATURES instrument sampler stereo)
    # Xcode wants the bundle identifier as a build setting too, or it warns that the Info.plist's differs from ''.
    set_target_properties(ChanSampler_CLAP PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.chansampler.chansampler")
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

# Everything the CI workflow builds, on every system: the VST3, the three test programs, the renderer (the render
# regression) and, on Windows, the snapshot tool (the workflow's screenshot artifact). The workflow builds the
# ci_targets aggregate and nothing by name.
# The VST3 wrapper compiles the SDK's vst2persistence.cpp, whose four-character constants GCC reports as -Wmultichar;
# the option has to sit on the wrapper target, not the shared code.
target_compile_options(ChanSampler_VST3 PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-multichar>)
add_dependencies(ci_targets ChanSampler_VST3)

# The test signal the UI and host tests load: a 2-second sine that nulltest/make_base_signals.py writes (audio is
# never committed). A fixture, so ctest writes it before the tests that need it.
set(CHANSAMPLER_SINE ${CHANSAMPLER_DIR}/nulltest/signals/sine.wav)
add_test(NAME ChanSamplerSineFixture
    COMMAND ${CHANSAMPLER_PYTHON} ${CHANSAMPLER_DIR}/nulltest/make_base_signals.py sine.wav)
set_tests_properties(ChanSamplerSineFixture PROPERTIES FIXTURES_SETUP sine)

# Offline engine tests (tests/ChanSamplerTests.cpp): no arguments.
juce_add_console_app(ChanSamplerTests PRODUCT_NAME "ChanSamplerTests")
target_sources(ChanSamplerTests PRIVATE ${CHANSAMPLER_DIR}/tests/ChanSamplerTests.cpp ${CHANSAMPLER_ENGINE_SOURCES})
target_include_directories(ChanSamplerTests PRIVATE ${CHANSAMPLER_DIR}/Source)
target_compile_definitions(ChanSamplerTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ChanSamplerTests PRIVATE
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    chansampler_warnings)
add_test(NAME ChanSamplerTests COMMAND ChanSamplerTests)
add_dependencies(ci_targets ChanSamplerTests)

# The editor's shared controls and the undo history, checked offline (tests/UiTests.cpp). Needs a display: the
# workflows run ctest under xvfb-run on Linux.
juce_add_console_app(ChanSamplerUiTests PRODUCT_NAME "ChanSamplerUiTests")
target_sources(ChanSamplerUiTests PRIVATE ${CHANSAMPLER_DIR}/tests/UiTests.cpp ${CHANSAMPLER_SOURCES})
target_include_directories(ChanSamplerUiTests PRIVATE ${CHANSAMPLER_DIR}/Source)
target_compile_definitions(ChanSamplerUiTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 ${CHANSAMPLER_TOOL_DEFINITIONS})
target_link_libraries(ChanSamplerUiTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
add_test(NAME ChanSamplerUiTests COMMAND ChanSamplerUiTests ${CHANSAMPLER_SINE})
set_tests_properties(ChanSamplerUiTests PROPERTIES FIXTURES_REQUIRED sine)
add_dependencies(ci_targets ChanSamplerUiTests)

# Hosts the built VST3 like a DAW: automation, MIDI, save / reload, editor size,
# and projects saved by earlier releases (tests/data). tools/HostTool.cpp.
juce_add_console_app(ChanSamplerHostTest PRODUCT_NAME "ChanSamplerHostTest")
target_sources(ChanSamplerHostTest PRIVATE ${CHANSAMPLER_DIR}/tools/HostTool.cpp)
target_compile_definitions(ChanSamplerHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ChanSamplerHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    chansampler_warnings)
add_dependencies(ChanSamplerHostTest ChanSampler_VST3)   # the test loads the VST3 bundle
file(GLOB CHANSAMPLER_STATE_FILES ${CHANSAMPLER_DIR}/tests/data/state-*.bin)
# JUCE's property names the .vst3 bundle on every platform; its value is itself a generator expression, so it is
# evaluated twice (GENEX_EVAL).
add_test(NAME ChanSamplerHostTest
    COMMAND ChanSamplerHostTest $<GENEX_EVAL:$<TARGET_PROPERTY:ChanSampler_VST3,JUCE_PLUGIN_ARTEFACT_FILE>> ${CHANSAMPLER_SINE} ${CHANSAMPLER_STATE_FILES})
set_tests_properties(ChanSamplerHostTest PROPERTIES FIXTURES_REQUIRED sine)
add_dependencies(ci_targets ChanSamplerHostTest)

if(CHANSAMPLER_TOOLS)
    # Offline renderer used by nulltest/ to compare against FL Studio renders, and by the render regression
    # (scripts/render_regress.py against the golden renders in tests/regress at the repository root).
    juce_add_console_app(ChanSamplerRender PRODUCT_NAME "ChanSamplerRender")
    target_sources(ChanSamplerRender PRIVATE ${CHANSAMPLER_DIR}/tools/ChanSamplerRender.cpp ${CHANSAMPLER_SOURCES})
    target_include_directories(ChanSamplerRender PRIVATE ${CHANSAMPLER_DIR}/Source)
    target_compile_definitions(ChanSamplerRender PRIVATE ${CHANSAMPLER_TOOL_DEFINITIONS})
    target_link_libraries(ChanSamplerRender PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    add_dependencies(ci_targets ChanSamplerRender)

    # Editor snapshots for the docs and the visual comparison with FL (tools/Snapshot.cpp); the standard call is
    # ChanSamplerSnapshot <out.png> [--scale=2].
    juce_add_console_app(ChanSamplerSnapshot PRODUCT_NAME "ChanSamplerSnapshot")
    target_sources(ChanSamplerSnapshot PRIVATE ${CHANSAMPLER_DIR}/tools/Snapshot.cpp ${CHANSAMPLER_SOURCES})
    target_include_directories(ChanSamplerSnapshot PRIVATE ${CHANSAMPLER_DIR}/Source)
    target_compile_definitions(ChanSamplerSnapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 ${CHANSAMPLER_TOOL_DEFINITIONS})
    target_link_libraries(ChanSamplerSnapshot PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    if(WIN32)
        add_dependencies(ci_targets ChanSamplerSnapshot)   # the CI workflow's screenshot artifact is made on Windows
    endif()
endif()
