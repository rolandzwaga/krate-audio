# ==============================================================================
# Shared Plugin Tests
# ==============================================================================
# Tests for shared UI components (ColorUtils, StepPatternEditor, etc.)
# ==============================================================================

add_executable(shared_tests
    test_main.cpp
    test_color_utils.cpp
    test_step_pattern_editor.cpp
    test_xy_morph_pad.cpp
    test_adsr_display.cpp
    test_oscillator_type_selector.cpp
)

target_link_libraries(shared_tests
    PRIVATE
        KratePluginsShared
        KrateDSP
        Catch2::Catch2
        sdk
        vstgui_support
)

target_include_directories(shared_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
)

target_compile_features(shared_tests PRIVATE cxx_std_20)

# Compiler warnings
if(MSVC)
    target_compile_options(shared_tests PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
else()
    target_compile_options(shared_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# IEEE 754 compliance: test_oscillator_type_selector.cpp uses std::isnan/std::isinf
# which are broken by -ffast-math (VST3 SDK enables it globally)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        test_oscillator_type_selector.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(shared_tests REPORTER console)
