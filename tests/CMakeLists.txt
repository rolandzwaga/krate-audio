# ==============================================================================
# Test Configuration
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
# - DSP algorithms MUST be pure functions testable without VST infrastructure
# - Unit tests MUST cover all DSP algorithms
# - Tests MUST run in CI/CD pipeline
# ==============================================================================

cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Fetch Testing Framework
# ==============================================================================
# Using Catch2 v3 for modern C++ testing
# Alternative: GoogleTest, doctest

include(FetchContent)

# Ensure consistent runtime library with Catch2
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
)
FetchContent_MakeAvailable(Catch2)

# ==============================================================================
# DSP Unit Tests
# ==============================================================================
# Constitution Principle VIII: DSP algorithms must be independently testable

add_executable(dsp_tests
    unit/test_dsp_utils.cpp
)

target_include_directories(dsp_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dsp_tests
    PRIVATE
        Catch2::Catch2
)

# MSVC needs main defined - we'll add it in the test file

target_compile_features(dsp_tests PRIVATE cxx_std_20)

# Enable Catch2's CMake integration
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(dsp_tests)

# ==============================================================================
# Integration Tests (Optional - requires built plugin)
# ==============================================================================
# These tests verify the plugin loads correctly in a test host
# Uncomment when ready to add integration tests

# add_executable(integration_tests
#     integration/test_plugin_load.cpp
# )
# target_link_libraries(integration_tests PRIVATE Catch2::Catch2WithMain sdk)
