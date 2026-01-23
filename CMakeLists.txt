cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Krate Audio Monorepo
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
#
# Structure:
#   dsp/              - KrateDSP shared library (Layers 0-4)
#   plugins/iterum/   - Iterum VST3 plugin
#   extern/vst3sdk/   - VST3 SDK (shared by all plugins)
#   tools/            - Shared development tools
# ==============================================================================

project(KrateAudio
    VERSION 0.8.0
    DESCRIPTION "Krate Audio Plugin Suite"
    LANGUAGES CXX
)

# C++ Standard (Constitution Principle III: C++17 minimum, C++20 preferred)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Compiler Cache (ccache) Support
# ==============================================================================
option(USE_CCACHE "Use ccache for faster rebuilds" OFF)

if(USE_CCACHE)
    include(FetchContent)
    FetchContent_Declare(
        Ccache.cmake
        URL https://github.com/TheLartians/Ccache.cmake/archive/refs/tags/v1.2.5.tar.gz
        URL_HASH SHA256=5ecd99d8f7e2b8191b0481ffbe31822c96b1a0dbc55eda570b31228dbe39ae6f
    )
    FetchContent_MakeAvailable(Ccache.cmake)
endif()

# ==============================================================================
# MSVC PDB Workaround for sccache/ccache
# ==============================================================================
if(MSVC AND (CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER))
    message(STATUS "[KRATE] Using /Z7 instead of /Zi for sccache/ccache compatibility")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

# ==============================================================================
# AddressSanitizer (ASan) Support
# ==============================================================================
# Detects memory errors at runtime:
# - Use-after-free (heap and stack)
# - Heap buffer overflow
# - Stack buffer overflow
# - Memory leaks
#
# Usage:
#   cmake -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..
#   cmake --build . --config Debug
#   ctest  # ASan will report errors if memory issues are detected
#
# NOTE: ASan adds ~2x runtime overhead. Use for debugging, not release.
# ==============================================================================
option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)

if(ENABLE_ASAN)
    message(STATUS "[KRATE] AddressSanitizer ENABLED - memory errors will be detected at runtime")

    if(MSVC)
        # MSVC ASan support (Visual Studio 2019 16.9+)
        # Reference: https://learn.microsoft.com/en-us/cpp/sanitizers/asan
        add_compile_options(/fsanitize=address)

        # CRITICAL: ASan is INCOMPATIBLE with /RTC1 (Runtime Checks)
        # /RTC1 is enabled by default in Debug builds. We must remove it.
        # Reference: https://devblogs.microsoft.com/cppblog/addresssanitizer-asan-for-windows-with-msvc/
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        string(REPLACE "/RTCs" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        string(REPLACE "/RTCu" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

        # CRITICAL: ASan is INCOMPATIBLE with /ZI (Edit and Continue debug info)
        # Must use /Zi instead. Replace globally in Debug flags.
        string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        # Force these into the cache so they apply to subdirectories (VST SDK)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" CACHE STRING "" FORCE)
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" CACHE STRING "" FORCE)

        # Tell the VST SDK to use ASan mode (prevents it from adding /ZI)
        # The SDK uses SMTG_ENABLE_ADDRESS_SANITIZER
        set(SMTG_ENABLE_ADDRESS_SANITIZER ON CACHE BOOL "" FORCE)

        # Disable incremental linking (incompatible with ASan)
        add_link_options(/INCREMENTAL:NO)
        add_link_options(/DEBUG)

        message(STATUS "[KRATE] Using MSVC AddressSanitizer (/fsanitize=address)")
        message(STATUS "[KRATE] Disabled /RTC1 and /ZI (incompatible with ASan)")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # Clang/GCC ASan support
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address)
        message(STATUS "[KRATE] Using Clang/GCC AddressSanitizer (-fsanitize=address)")
    else()
        message(WARNING "[KRATE] AddressSanitizer not supported for this compiler")
    endif()
endif()

# ==============================================================================
# VST3 SDK Configuration (Shared by all plugins)
# ==============================================================================
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/vst3sdk")

if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at ${vst3sdk_SOURCE_DIR}\n"
        "Please run: git submodule add https://github.com/steinbergmedia/vst3sdk.git extern/vst3sdk\n"
        "Then run: git submodule update --init --recursive"
    )
endif()

# SDK Options
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_CXX_STANDARD "20" CACHE STRING "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# ==============================================================================
# Testing Configuration (Constitution Principle VIII)
# ==============================================================================
option(VSTWORK_BUILD_TESTS "Build unit tests" ON)

if(VSTWORK_BUILD_TESTS)
    enable_testing()

    # Fetch Catch2 for testing
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    # Fetch ApprovalTests for approval testing
    FetchContent_Declare(
        ApprovalTests
        GIT_REPOSITORY https://github.com/approvals/ApprovalTests.cpp.git
        GIT_TAG v.10.13.0
    )
    FetchContent_MakeAvailable(ApprovalTests)

    # Test helpers library (shared by all tests)
    add_subdirectory(tests/test_helpers)
endif()

# ==============================================================================
# KrateDSP Library (Shared by all plugins)
# ==============================================================================
add_subdirectory(dsp)

# ==============================================================================
# Plugins
# ==============================================================================
add_subdirectory(plugins/iterum)

# ==============================================================================
# Tools (Shared development tools)
# ==============================================================================
add_executable(preset_generator
    tools/preset_generator.cpp
)
target_compile_features(preset_generator PRIVATE cxx_std_20)
set_target_properties(preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ==============================================================================
# Control Testbench (UI Development Tool)
# ==============================================================================
# Standalone application for testing custom VSTGUI controls without loading
# the full plugin in a DAW. Provides fast iteration for UI development.
#
# Build with: cmake --build build --target control_testbench
# ==============================================================================
option(BUILD_CONTROL_TESTBENCH "Build the control testbench tool" ON)

if(BUILD_CONTROL_TESTBENCH)
    add_subdirectory(tools/control_testbench)
endif()
