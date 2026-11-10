cmake_minimum_required(VERSION 3.22)

# Chan Sampler: one CMake project for every plug-in (plugins/<Name>/plugin.cmake) and the measurement tools.
# The suite's version, shared by every plug-in: the first release is 1.0.0, a new plug-in raises the middle number
# (1.1.0, 1.2.0 ...), fixes and small updates the last (1.1.1, 1.1.2 ...). CHANGELOG.md has a section per version.
project(ChanSampler VERSION 1.7.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: static C++ runtime, so the plug-ins load on any PC without a redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# No fused multiply-add: GCC and Clang contract a * b + c into one rounding by default, which makes the same source
# give different last bits on different platforms; with this off and shared/dsp/Math.h for exp and log, a render is
# bit-identical on Windows, Linux and macOS, so the render regression's goldens hold on all three.
add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)
# JUCE's helper-tool bootstrap sets the environment variable CMAKE_GENERATOR_PLATFORM inside this CMake process for
# its nested configure. When `cmake --build` re-runs the configure itself (after a CMake file changed), the variable
# stays in that process and reaches MSBuild and every CMake call MSBuild makes, which print "Environment variable
# CMAKE_GENERATOR_PLATFORM will be ignored" as a build warning. Cleared once JUCE is set up.
unset(ENV{CMAKE_GENERATOR_PLATFORM})
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
# JUCE's VST3 editor ignores the keys a host offers it (FL Studio's Ctrl+Z then never reaches the plug-in): patch it
# at configure time (idempotent; see the script).
execute_process(COMMAND ${CMAKE_COMMAND} -DJUCE_DIR=${juce_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchJuceVst3Keys.cmake
    RESULT_VARIABLE juce_patch_result)
if(NOT juce_patch_result EQUAL 0)
    message(FATAL_ERROR "cmake/PatchJuceVst3Keys.cmake failed")
endif()

# CLAP through clap-juce-extensions (MIT; brings the CLAP SDK).
option(CHANSAMPLER_CLAP "Build the CLAP plugins" ON)
if(CHANSAMPLER_CLAP)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE ON)
    FetchContent_MakeAvailable(clap_juce_extensions)
endif()

# Measurement tools (hosts for the reference plug-ins, our offline renderers, snapshots); not shipped.
option(CHANSAMPLER_TOOLS "Build the measurement tools" ON)
# A copy of every VST3 at build/plugins/<Product name>.vst3 after each build, the folder a DAW's plug-in junction points
# at in every suite.
option(CHANSAMPLER_DEV_COPY "Copy each VST3 to build/plugins after a build" ON)

# The Python that writes the test signal fixture (plugins/ChanSampler/nulltest/render_signals.py, standard library
# only); set CHANSAMPLER_PYTHON to use another interpreter than the one find_package finds.
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

# MP3 decoding is on for every target: the sampler loads MP3 files, and the shared library and the plug-ins must agree
# on the JUCE configuration.
set(SUITE_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_MP3AUDIOFORMAT=1)
set(SUITE_REPOSITORY "https://github.com/MaymiPR/Chan-Sampler")
enable_testing()

# Two of JUCE's recommended GCC / Clang warnings flag patterns this code uses on purpose: switches over a few cases
# with a default, and exact float comparisons of values that are set, not computed. Linked after JUCE's warning
# flags, so these come later and win. Our own code builds with 0 warnings on every platform; keep it so.
# GCC 13's -Wdangling-reference (in -Wall) fires twice inside JUCE 9.0.5's LookAndFeel_V2 on a reference a
# function returns into a temporary that outlives it; a false positive GCC 14 no longer reports and not our code,
# so GCC alone drops that one warning. The flag exists only for C++, and JUCE's modules carry C sources, so the
# language is part of the condition (44 cc1 lines otherwise).
add_library(suite_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(suite_warnings INTERFACE -Wno-switch-enum -Wno-float-equal
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<COMPILE_LANGUAGE:CXX>>:-Wno-dangling-reference>)
endif()
# GCC links JUCE's plain -flto serially and warns about it (lto-wrapper: serial compilation of N LTRANS jobs);
# -flto=auto later on the link line runs the same link in parallel. A link item on purpose: link options land
# before the libraries and the later -flto would win.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(suite_warnings INTERFACE $<$<CONFIG:Release>:-flto=auto>)
endif()

# The plug-ins. Each is included (not added as a subdirectory), so its build output stays at
# build/<Target>_artefacts. packaging/plugins.txt lists them for the installers and the release workflow.
# Every plugin.cmake adds its VST3, its tests (the host test included), its renderer (and on Windows its snapshot
# tool) to ci_targets, so ci.yml builds one target on every system and a new plug-in needs no workflow edit. The
# suite's code in shared/ is the static library ChanSamplerShared that each of them links.
add_custom_target(ci_targets)
include(shared/shared.cmake)
include(plugins/ChanSampler/plugin.cmake)

if(CHANSAMPLER_TOOLS)
    # Everything a host sees without audio: buses, latency, programs, every parameter's text across its range.
    juce_add_console_app(ParamDump PRODUCT_NAME "ParamDump")
    target_sources(ParamDump PRIVATE tools/ParamDump.cpp)
    target_compile_definitions(ParamDump PRIVATE JUCE_PLUGINHOST_VST3=1 ${SUITE_DEFS})
    target_link_libraries(ParamDump PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
    # Offline renders through any VST3 (the measurements and null tests; tools/RefRender.cpp).
    juce_add_console_app(RefRender PRODUCT_NAME "RefRender")
    target_sources(RefRender PRIVATE tools/RefRender.cpp)
    target_compile_definitions(RefRender PRIVATE JUCE_PLUGINHOST_VST3=1 ${SUITE_DEFS})
    target_link_libraries(RefRender PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    if(WIN32)
        # Films / screenshots a plug-in's editor while audio plays (tools/RefCapture.cpp).
        juce_add_gui_app(RefCapture PRODUCT_NAME "RefCapture")
        target_sources(RefCapture PRIVATE tools/RefCapture.cpp)
        target_compile_definitions(RefCapture PRIVATE JUCE_PLUGINHOST_VST3=1 ${SUITE_DEFS})
        target_link_libraries(RefCapture PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
        # PASS when the space bar still reaches the host after clicks in an editor (tools/FocusCheck.cpp).
        juce_add_console_app(FocusCheck PRODUCT_NAME "FocusCheck")
        target_sources(FocusCheck PRIVATE tools/FocusCheck.cpp)
        target_compile_definitions(FocusCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 ${SUITE_DEFS})
        target_link_libraries(FocusCheck PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
    endif()
endif()
