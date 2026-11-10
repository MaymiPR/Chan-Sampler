# Code and resources every Chan Sampler plug-in uses: the editor's theme, the small controls, the host-keys hand-back,
# the deterministic math, later the suite's font and the measured knob. Included from the top-level CMakeLists.txt
# before the plug-ins. Built once as the static library ChanSamplerShared, which every plug-in, test, snapshot and
# renderer links (it brings the JUCE modules and the include path of the repository root: #include "shared/ui/...").
set(SUITE_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_library(ChanSamplerShared STATIC
    ${SUITE_SHARED_DIR}/ui/Widgets.cpp
    ${SUITE_SHARED_DIR}/ui/HostFocus.cpp)
target_include_directories(ChanSamplerShared PUBLIC ${CMAKE_SOURCE_DIR})
set_target_properties(ChanSamplerShared PROPERTIES POSITION_INDEPENDENT_CODE ON)   # linked into shared plug-ins (Linux)
# The JUCE configuration of every target that links it: the plug-ins' objects and this library's must agree on it.
target_compile_definitions(ChanSamplerShared PUBLIC ${SUITE_DEFS})
target_link_libraries(ChanSamplerShared
    PUBLIC juce::juce_audio_utils juce::juce_dsp
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags suite_warnings)

# The suite's font, once chosen (the editor work: the closest free match to the reference's typeface, with its licence
# in packaging/THIRD-PARTY-NOTICES.txt). Put the .ttf in shared/resources/fonts/, then:
#   juce_add_binary_data(ChanSamplerUiData NAMESPACE ChanSamplerUiData HEADER_NAME ChanSamplerUiData.h
#       SOURCES ${SUITE_SHARED_DIR}/resources/fonts/<Font>-Regular.ttf)
#   set_target_properties(ChanSamplerUiData PROPERTIES POSITION_INDEPENDENT_CODE ON)
#   target_link_libraries(ChanSamplerShared PUBLIC ChanSamplerUiData)
# shared/ui/Theme.h says where the font is loaded.
