cmake_minimum_required(VERSION 3.22)

# Chan Sampler: one CMake project for the plug-in (plugins/ChanSampler/plugin.cmake). The version is the suite's:
# a new feature raises the middle number, fixes and small updates the last. CHANGELOG.md has a section per version.
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

# The Python that writes the test signal fixture (plugins/ChanSampler/nulltest/make_base_signals.py, standard library
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

enable_testing()

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

# The plug-in. It is included (not added as a subdirectory), so its build output stays at build/<Target>_artefacts.
# packaging/plugins.txt lists it for the installers and the release workflow. plugin.cmake adds its VST3, its tests,
# its renderer and, on Windows, its snapshot tool to ci_targets, so ci.yml builds one target on every system.
# Nothing is shared between plug-ins yet, so there is no shared/ folder.
add_custom_target(ci_targets)
include(plugins/ChanSampler/plugin.cmake)
