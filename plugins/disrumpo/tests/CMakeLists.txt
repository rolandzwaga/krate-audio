# ==============================================================================
# Disrumpo Plugin Tests
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
# - Plugin tests verify crossover network, band processing, and integration
# - Unit tests and integration tests for the Disrumpo plugin
# ==============================================================================

# ==============================================================================
# Plugin Unit Tests
# ==============================================================================
add_executable(disrumpo_tests
    # Test main
    unit/test_main.cpp

    # Unit tests - Parameter encoding (spec 004)
    unit/parameter_encoding_test.cpp

    # Unit tests - UI/Display (spec 004)
    unit/spectrum_display_test.cpp
    unit/display_format_test.cpp
    unit/global_controls_test.cpp
    unit/band_strip_test.cpp
    unit/param_display_test.cpp
    unit/band_state_test.cpp
    unit/visibility_test.cpp
    unit/crossover_interaction_test.cpp

    # Unit tests - DSP
    unit/crossover_network_test.cpp
    unit/band_processing_test.cpp
    unit/distortion_adapter_test.cpp

    # Unit tests - Morph System (005-morph-system)
    unit/morph_weight_computation_test.cpp
    unit/morph_mode_test.cpp
    unit/morph_transition_test.cpp
    unit/morph_interpolation_test.cpp
    unit/morph_processor_cap_test.cpp
    unit/morph_band_integration_test.cpp

    # Approval tests - Morph artifact detection (005-morph-system SC-003)
    approval/morph_artifact_test.cpp

    # Unit tests - MorphPad UI (006-morph-ui)
    unit/morph_pad_test.cpp
    unit/morph_link_test.cpp

    # Unit tests - Sweep System (007-sweep-system)
    dsp/sweep_morph_link_tests.cpp
    dsp/custom_curve_tests.cpp
    dsp/sweep_intensity_tests.cpp
    dsp/sweep_processor_tests.cpp
    dsp/sweep_lfo_tests.cpp
    dsp/sweep_envelope_tests.cpp

    # DSP sources (needed for tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/distortion_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/morph_engine.cpp

    # UI sources (needed for MorphPad tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/views/morph_pad.cpp

    # Morph link sources (needed for morph link tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/morph_link.cpp

    # Integration tests
    integration/band_management_integration_test.cpp
    integration/expand_collapse_test.cpp
    integration/node_editor_test.cpp
    integration/sweep_band_intensity_test.cpp
)

target_link_libraries(disrumpo_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
        sdk
        vstgui_support
)

target_include_directories(disrumpo_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
)

target_compile_features(disrumpo_tests PRIVATE cxx_std_20)

# ==============================================================================
# Disable -ffast-math for files requiring IEEE 754 compliance
# ==============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/crossover_network_test.cpp
        unit/band_processing_test.cpp
        unit/distortion_adapter_test.cpp
        unit/morph_weight_computation_test.cpp
        unit/morph_mode_test.cpp
        unit/morph_transition_test.cpp
        unit/morph_interpolation_test.cpp
        unit/morph_processor_cap_test.cpp
        unit/morph_band_integration_test.cpp
        unit/morph_pad_test.cpp
        approval/morph_artifact_test.cpp
        integration/band_management_integration_test.cpp
        dsp/sweep_morph_link_tests.cpp
        dsp/custom_curve_tests.cpp
        dsp/sweep_intensity_tests.cpp
        dsp/sweep_processor_tests.cpp
        dsp/sweep_lfo_tests.cpp
        dsp/sweep_envelope_tests.cpp
        integration/sweep_band_intensity_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(disrumpo_tests REPORTER console)
