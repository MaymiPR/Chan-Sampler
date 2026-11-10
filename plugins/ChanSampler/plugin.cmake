# Chan Sampler (recreation of FL Studio's Channel Sampler). Included from the top-level CMakeLists.txt after
# shared/shared.cmake.
set(CS_DIR ${CMAKE_CURRENT_LIST_DIR})

# The sources: the engine alone (what the engine tests need) and everything.
set(CS_ENGINE_SOURCES
    ${CS_DIR}/Source/Params.cpp
    ${CS_DIR}/Source/History.cpp
    ${CS_DIR}/Source/dsp/SampleData.cpp
    ${CS_DIR}/Source/dsp/Dsp.cpp
    ${CS_DIR}/Source/dsp/Engine.cpp
    ${CS_DIR}/Source/dsp/TimeStretch.cpp)
set(CS_SOURCES
    ${CS_ENGINE_SOURCES}
    ${CS_DIR}/Source/PluginProcessor.cpp
    ${CS_DIR}/Source/CutGroups.cpp
    ${CS_DIR}/Source/PresetImport.cpp
    ${CS_DIR}/Source/ui/PresetBar.cpp
    ${CS_DIR}/Source/Presets.cpp
    ${CS_DIR}/Source/PluginEditor.cpp
    ${CS_DIR}/Source/ui/Controls.cpp
    ${CS_DIR}/Source/SampleExchange.cpp
    ${CS_DIR}/Source/dsp/TempoDetect.cpp
    ${CS_DIR}/Source/ui/WaveformView.cpp)
if(APPLE)
    list(APPEND CS_SOURCES ${CS_DIR}/Source/SampleExchangeMac.mm)   # the macOS clipboard is Objective-C++
endif()

set(CS_DEFS ${SUITE_DEFS} SUITE_REPOSITORY="${SUITE_REPOSITORY}")
# the snapshot / test / render executables compile the plug-in sources outside the plug-in wrapper
set(CS_APP_DEFS ${CS_DEFS} JucePlugin_Name="Chan Sampler" JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
    JucePlugin_VersionString="${PROJECT_VERSION}")

set(formats VST3 Standalone)
if(APPLE)
    list(APPEND formats AU)
endif()
# The manufacturer and plug-in codes and the bundle ID make the plug-in's identity in hosts (saved projects find it by
# them): keep them (CONTRIBUTING.md "Frozen identity"). The bundle ID is the one every release has shipped with.
juce_add_plugin(ChanSampler
    COMPANY_NAME "Chan Sampler"
    PLUGIN_MANUFACTURER_CODE Chsm
    PLUGIN_CODE Csm1
    FORMATS ${formats}
    PRODUCT_NAME "Chan Sampler"
    IS_SYNTH TRUE                       # an instrument: no audio input, MIDI in (packaging/plugins.txt: AU type aumu)
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE   # the host keeps its transport shortcuts
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    BUNDLE_ID com.ChanSampler.ChanSampler)
# MSBuild cannot handle an apostrophe or other odd characters in the shared-code library's file name (both names:
# Xcode builds the library under XCODE_ATTRIBUTE_PRODUCT_NAME, the link lines use OUTPUT_NAME)
set_target_properties(ChanSampler PROPERTIES OUTPUT_NAME "ChanSampler_SharedCode" XCODE_ATTRIBUTE_PRODUCT_NAME "ChanSampler_SharedCode")
target_sources(ChanSampler PRIVATE ${CS_SOURCES})
target_include_directories(ChanSampler PRIVATE ${CS_DIR}/Source ${CMAKE_SOURCE_DIR})
target_compile_definitions(ChanSampler PUBLIC ${CS_DEFS})
target_link_libraries(ChanSampler PRIVATE ChanSamplerShared juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags suite_warnings)
# The VST3 wrapper compiles the SDK's vst2persistence.cpp, whose four-character constants GCC reports as -Wmultichar
# (48 lines per build); the option has to sit on the wrapper target, not the shared code.
target_compile_options(ChanSampler_VST3 PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-multichar>)
add_dependencies(ci_targets ChanSampler_VST3)
if(CHANSAMPLER_DEV_COPY)
    # Every rebuild also lands at build/plugins/<Product name>.vst3, the one folder a DAW's plug-in junction points at.
    add_custom_command(TARGET ChanSampler_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:ChanSampler_VST3>/../.."
            "${CMAKE_BINARY_DIR}/plugins/Chan Sampler.vst3"
        VERBATIM)
endif()
if(CHANSAMPLER_CLAP)
    clap_juce_extensions_plugin(TARGET ChanSampler
        CLAP_ID "com.chansampler.chansampler" CLAP_FEATURES instrument sampler stereo)
    # Xcode wants the bundle identifier as a build setting too, or it warns that the Info.plist's differs from ''.
    set_target_properties(ChanSampler_CLAP PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.chansampler.chansampler")
endif()

# The test signal the UI and host tests load: a 2-second sine that nulltest/make_base_signals.py writes (audio is
# never committed). A fixture, so ctest writes it before the tests that need it.
set(CS_SINE ${CS_DIR}/nulltest/signals/sine.wav)
add_test(NAME ChanSamplerSineFixture
    COMMAND ${CHANSAMPLER_PYTHON} ${CS_DIR}/nulltest/make_base_signals.py sine.wav)
set_tests_properties(ChanSamplerSineFixture PROPERTIES FIXTURES_SETUP sine)

# Offline engine tests (tests/ChanSamplerTests.cpp): no arguments.
#   cmake --build build --target ChanSamplerTests, then ctest --test-dir build -C Release
juce_add_console_app(ChanSamplerTests PRODUCT_NAME "ChanSamplerTests")
target_sources(ChanSamplerTests PRIVATE ${CS_DIR}/tests/ChanSamplerTests.cpp ${CS_ENGINE_SOURCES})
target_include_directories(ChanSamplerTests PRIVATE ${CS_DIR}/Source ${CMAKE_SOURCE_DIR})
target_compile_definitions(ChanSamplerTests PRIVATE ${CS_DEFS})
target_link_libraries(ChanSamplerTests PRIVATE ChanSamplerShared juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags suite_warnings)
add_test(NAME ChanSamplerTests COMMAND ChanSamplerTests)
add_dependencies(ci_targets ChanSamplerTests)

# The editor's controls and the undo history, checked offline (tests/ChanSamplerUiTests.cpp). Needs a display: the
# workflows run ctest under xvfb-run on Linux.
juce_add_console_app(ChanSamplerUiTests PRODUCT_NAME "ChanSamplerUiTests")
target_sources(ChanSamplerUiTests PRIVATE ${CS_DIR}/tests/ChanSamplerUiTests.cpp ${CS_SOURCES})
target_include_directories(ChanSamplerUiTests PRIVATE ${CS_DIR}/Source ${CMAKE_SOURCE_DIR})
target_compile_definitions(ChanSamplerUiTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 ${CS_APP_DEFS})
target_link_libraries(ChanSamplerUiTests PRIVATE ChanSamplerShared juce::juce_recommended_config_flags)
add_test(NAME ChanSamplerUiTests COMMAND ChanSamplerUiTests ${CS_SINE})
set_tests_properties(ChanSamplerUiTests PROPERTIES FIXTURES_REQUIRED sine)
add_dependencies(ci_targets ChanSamplerUiTests)

# The built VST3 hosted as a DAW hosts it (tools/HostTest.cpp): automation, MIDI, save and reload, the editor, rate
# changes, offline rendering and every saved state of an earlier release in tests/data (state-<x.y.z>.bin with its
# .txt: ChanSamplerHostTest <vst3> --save-state plugins/ChanSampler/tests/data/state-<x.y.z>.bin). A sampler: the
# test sine is its sample.
juce_add_console_app(ChanSamplerHostTest PRODUCT_NAME "ChanSamplerHostTest")
target_sources(ChanSamplerHostTest PRIVATE ${CMAKE_SOURCE_DIR}/tools/HostTest.cpp)
target_compile_definitions(ChanSamplerHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 ${SUITE_DEFS})
target_link_libraries(ChanSamplerHostTest PRIVATE juce::juce_audio_processors juce::juce_gui_basics
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags suite_warnings)
add_dependencies(ChanSamplerHostTest ChanSampler_VST3)          # the test loads the VST3 bundle
file(GLOB CS_STATES CONFIGURE_DEPENDS ${CS_DIR}/tests/data/state-*.bin)
# JUCE's property names the .vst3 bundle on every platform; its value is itself a generator expression, so it is
# evaluated twice (GENEX_EVAL).
add_test(NAME ChanSamplerHostTest
    COMMAND ChanSamplerHostTest $<GENEX_EVAL:$<TARGET_PROPERTY:ChanSampler_VST3,JUCE_PLUGIN_ARTEFACT_FILE>> ${CS_SINE} ${CS_STATES})
set_tests_properties(ChanSamplerHostTest PROPERTIES FIXTURES_REQUIRED sine)
add_dependencies(ci_targets ChanSamplerHostTest)

if(CHANSAMPLER_TOOLS)
    # Editor snapshot (PNG) for the docs and the layout comparison with the reference (docs/screenshots/README.md).
    # Chan Sampler keeps its own tools/Snapshot.cpp: the standard call plus a sample, the sheets, the sample view's
    # zoom and the menus and dialogs, which the suite's tools/Snapshot.cpp does not offer.
    juce_add_console_app(ChanSamplerSnapshot PRODUCT_NAME "ChanSamplerSnapshot")
    target_sources(ChanSamplerSnapshot PRIVATE ${CS_DIR}/tools/Snapshot.cpp ${CS_SOURCES})
    target_include_directories(ChanSamplerSnapshot PRIVATE ${CS_DIR}/Source ${CMAKE_SOURCE_DIR})
    target_compile_definitions(ChanSamplerSnapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 ${CS_APP_DEFS})
    target_link_libraries(ChanSamplerSnapshot PRIVATE ChanSamplerShared juce::juce_recommended_config_flags)
    if(WIN32)
        add_dependencies(ci_targets ChanSamplerSnapshot)      # ci.yml's screenshot artefact is made on Windows
    endif()

    # Offline renders through our own processor (the null tests, the render regression) with the fitted constants
    # open to change (dsp/Tuning.h), and a second copy for trying DSP changes while a fit is running with the first.
    foreach(t CsRender CsRenderDev)
        juce_add_console_app(${t} PRODUCT_NAME "${t}")
        target_sources(${t} PRIVATE ${CS_DIR}/tools/CsRender.cpp ${CS_SOURCES})
        target_include_directories(${t} PRIVATE ${CS_DIR}/Source ${CMAKE_SOURCE_DIR})
        target_compile_definitions(${t} PRIVATE ${CS_APP_DEFS})
        target_link_libraries(${t} PRIVATE ChanSamplerShared juce::juce_recommended_config_flags)
    endforeach()
    add_dependencies(ci_targets CsRender)          # the render regression
endif()
