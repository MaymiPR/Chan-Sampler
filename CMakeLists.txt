cmake_minimum_required(VERSION 3.22)

# Chan Sampler: one plug-in, so this file is the project and its plugin.cmake in one (the flat layout is a recorded
# deviation from the suite layout; CONTRIBUTING.md). It defines ci_targets, which the CI workflow builds, and
# registers every test with ctest, the only test command.
project(ChanSampler VERSION 1.7.1)

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
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)
# A tag can be moved: build only from the commit it pointed to when chosen
# (unless FETCHCONTENT_SOURCE_DIR_JUCE points at a JUCE of your own).
set(CHANSAMPLER_JUCE_COMMIT d60b7a78030661574a615b586d9325ba7a7fa7dd)
if(NOT FETCHCONTENT_SOURCE_DIR_JUCE)
    find_package(Git REQUIRED)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD WORKING_DIRECTORY ${juce_SOURCE_DIR}
        OUTPUT_VARIABLE juce_commit OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT juce_commit STREQUAL CHANSAMPLER_JUCE_COMMIT)
        message(FATAL_ERROR "JUCE 9.0.5 is at ${juce_commit}, expected ${CHANSAMPLER_JUCE_COMMIT}")
    endif()
endif()
# JUCE's VST3 editor ignores the keys a host offers it (FL Studio's Ctrl+Z then never reaches the plugin): patch it
# at configure time (idempotent; see the script).
execute_process(COMMAND ${CMAKE_COMMAND} -DJUCE_DIR=${juce_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchJuceVst3Keys.cmake
    RESULT_VARIABLE juce_patch_result)
if(NOT juce_patch_result EQUAL 0)
    message(FATAL_ERROR "cmake/PatchJuceVst3Keys.cmake failed")
endif()

# CLAP, through clap-juce-extensions (MIT; it brings the CLAP SDK).
option(CHANSAMPLER_CLAP "Build the CLAP plugin" ON)
if(CHANSAMPLER_CLAP)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d   # main
        GIT_SUBMODULES_RECURSE ON)
    FetchContent_MakeAvailable(clap_juce_extensions)
endif()

# The developer tools (the offline renderer and the editor snapshots); not shipped.
option(CHANSAMPLER_TOOLS "Build the developer tools (ChanSamplerRender, ChanSamplerSnapshot)" ON)

# The Python that writes the test signal fixture (nulltest/make_base_signals.py, standard library only); set
# CHANSAMPLER_PYTHON to use another interpreter than the one find_package finds.
if(NOT CHANSAMPLER_PYTHON)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(CHANSAMPLER_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH "Python for the test fixtures")
    else()
        # A status line, not a warning (builds count warnings): without Python the fixture test fails in ctest.
        set(CHANSAMPLER_PYTHON "python3" CACHE FILEPATH "Python for the test fixtures")
        message(STATUS "No Python 3 found: the ctest fixture runs 'python3' from PATH (set CHANSAMPLER_PYTHON)")
    endif()
endif()

enable_testing()

# The sources: the engine alone (what the tests need) and everything.
set(CHANSAMPLER_ENGINE_SOURCES
    Source/Params.cpp
    Source/History.cpp
    Source/SampleData.cpp
    Source/Dsp.cpp
    Source/SamplerEngine.cpp
    Source/TimeStretch.cpp)
set(CHANSAMPLER_SOURCES
    ${CHANSAMPLER_ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/HostFocus.cpp
    Source/CutGroups.cpp
    Source/PresetImport.cpp
    Source/PresetBar.cpp
    Source/FactoryPresets.cpp
    Source/PluginEditor.cpp
    Source/Style.cpp
    Source/Parts.cpp
    Source/SampleExchange.cpp
    Source/TempoDetect.cpp
    Source/WaveformView.cpp)
if(APPLE)
    list(APPEND CHANSAMPLER_SOURCES Source/SampleExchangeMac.mm)   # the macOS clipboard is Objective-C++
endif()

# Two of JUCE's recommended GCC / Clang warnings flag patterns this code uses on purpose: switches over a few cases
# with a default, and exact float comparisons (a value that is set, not computed: "is this LFO off?", "did it
# change?"). Linked after JUCE's warning flags, so these come later and win. Our own code builds with 0 warnings
# on every platform; keep it so.
# GCC 13's -Wdangling-reference (in -Wall) fires twice inside JUCE 9.0.5's LookAndFeel_V2 on a reference a
# function returns into a temporary that outlives it; a false positive GCC 14 no longer reports and not our code,
# so GCC alone drops that one warning. The flag exists only for C++, and JUCE's modules carry C sources, so the
# language is part of the condition (44 cc1 lines otherwise).
add_library(chansampler_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(chansampler_warnings INTERFACE -Wno-switch-enum -Wno-float-equal
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<COMPILE_LANGUAGE:CXX>>:-Wno-dangling-reference>)
endif()
# GCC links JUCE's -flto serially and warns about it; -flto=auto, later on the
# link line, runs the same link in parallel.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(chansampler_warnings INTERFACE $<$<CONFIG:Release>:-flto=auto>)
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
# regression) and, on Windows, the snapshot tool (the workflow's screenshot artifact). The workflow builds this one
# target and nothing by name.
# The VST3 wrapper compiles the SDK's vst2persistence.cpp, whose four-character constants GCC reports as -Wmultichar;
# the option has to sit on the wrapper target, not the shared code.
target_compile_options(ChanSampler_VST3 PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-multichar>)
add_custom_target(ci_targets)
add_dependencies(ci_targets ChanSampler_VST3)

# The test signal the UI and host tests load: a 2-second sine that nulltest/make_base_signals.py writes (audio is
# never committed). A fixture, so ctest writes it before the tests that need it.
set(CHANSAMPLER_SINE ${CMAKE_CURRENT_SOURCE_DIR}/nulltest/signals/sine.wav)
add_test(NAME ChanSamplerSineFixture
    COMMAND ${CHANSAMPLER_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/nulltest/make_base_signals.py sine.wav)
set_tests_properties(ChanSamplerSineFixture PROPERTIES FIXTURES_SETUP sine)

# The developer tools (CONTRIBUTING.md). Offline engine tests (tests/EngineTests.cpp): no arguments.
juce_add_console_app(ChanSamplerTests PRODUCT_NAME "ChanSamplerTests")
target_sources(ChanSamplerTests PRIVATE tests/EngineTests.cpp ${CHANSAMPLER_ENGINE_SOURCES})
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
target_sources(ChanSamplerUiTests PRIVATE tests/UiTests.cpp ${CHANSAMPLER_SOURCES})
target_compile_definitions(ChanSamplerUiTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 ${CHANSAMPLER_TOOL_DEFINITIONS})
target_link_libraries(ChanSamplerUiTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
add_test(NAME ChanSamplerUiTests COMMAND ChanSamplerUiTests ${CHANSAMPLER_SINE})
set_tests_properties(ChanSamplerUiTests PROPERTIES FIXTURES_REQUIRED sine)
add_dependencies(ci_targets ChanSamplerUiTests)

# Hosts the built VST3 like a DAW: automation, MIDI, save / reload, editor size,
# and projects saved by earlier releases (tests/data).
juce_add_console_app(ChanSamplerHostTest PRODUCT_NAME "ChanSamplerHostTest")
target_sources(ChanSamplerHostTest PRIVATE tests/HostTool.cpp)
target_compile_definitions(ChanSamplerHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ChanSamplerHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    chansampler_warnings)
add_dependencies(ChanSamplerHostTest ChanSampler_VST3)   # the test loads the VST3 bundle
file(GLOB CHANSAMPLER_STATE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/state-*.bin)
# JUCE's property names the .vst3 bundle on every platform; its value is itself a generator expression, so it is
# evaluated twice (GENEX_EVAL).
add_test(NAME ChanSamplerHostTest
    COMMAND ChanSamplerHostTest $<GENEX_EVAL:$<TARGET_PROPERTY:ChanSampler_VST3,JUCE_PLUGIN_ARTEFACT_FILE>> ${CHANSAMPLER_SINE} ${CHANSAMPLER_STATE_FILES})
set_tests_properties(ChanSamplerHostTest PROPERTIES FIXTURES_REQUIRED sine)
add_dependencies(ci_targets ChanSamplerHostTest)

if(CHANSAMPLER_TOOLS)
    # Offline renderer used by nulltest/ to compare against FL Studio renders, and by the render regression
    # (scripts/render_regress.py against the golden renders in tests/regress).
    juce_add_console_app(ChanSamplerRender PRODUCT_NAME "ChanSamplerRender")
    target_sources(ChanSamplerRender PRIVATE tests/RenderTool.cpp ${CHANSAMPLER_SOURCES})
    target_compile_definitions(ChanSamplerRender PRIVATE ${CHANSAMPLER_TOOL_DEFINITIONS})
    target_link_libraries(ChanSamplerRender PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    add_dependencies(ci_targets ChanSamplerRender)

    # Editor snapshots for visual comparison with FL (tests/SnapshotTool.cpp); the standard call is
    # ChanSamplerSnapshot <out.png> [--scale=2].
    juce_add_console_app(ChanSamplerSnapshot PRODUCT_NAME "ChanSamplerSnapshot")
    target_sources(ChanSamplerSnapshot PRIVATE tests/SnapshotTool.cpp ${CHANSAMPLER_SOURCES})
    target_compile_definitions(ChanSamplerSnapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 ${CHANSAMPLER_TOOL_DEFINITIONS})
    target_link_libraries(ChanSamplerSnapshot PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    if(WIN32)
        add_dependencies(ci_targets ChanSamplerSnapshot)   # the CI workflow's screenshot artifact is made on Windows
    endif()
endif()
