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

    # Unit tests
    unit/crossover_network_test.cpp
    unit/band_processing_test.cpp
    unit/distortion_adapter_test.cpp

    # DSP sources (needed for tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp/distortion_adapter.cpp

    # Integration tests
    integration/band_management_integration_test.cpp
)

target_link_libraries(disrumpo_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
        sdk
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
        integration/band_management_integration_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(disrumpo_tests REPORTER console)
