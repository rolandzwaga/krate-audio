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
    LANGUAGES C CXX
)

# C++ Standard (Constitution Principle III: C++17 minimum, C++20 preferred)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for VS Code IntelliSense and clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Clang-Tidy Integration (Optional)
# ==============================================================================
# Static analysis for code quality. Not enabled by default to avoid slowing builds.
#
# Usage:
#   - Standalone (recommended): ./tools/run-clang-tidy.ps1 or ./tools/run-clang-tidy.sh
#   - CMake integration: cmake -DENABLE_CLANG_TIDY=ON ... (runs on every compile)
#
# NOTE: CMake integration works best with Ninja generator, not Visual Studio.
# For MSVC/Visual Studio generator, use the standalone scripts instead.
# ==============================================================================
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis during build" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy" DOC "Path to clang-tidy executable")
    if(CLANG_TIDY_EXE)
        message(STATUS "[KRATE] Clang-Tidy ENABLED: ${CLANG_TIDY_EXE}")

        # Configure clang-tidy command
        # --config-file: Use project .clang-tidy file
        # --header-filter: Only check our headers, not external deps
        set(CLANG_TIDY_COMMAND
            "${CLANG_TIDY_EXE}"
            "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
        )

        # MSVC-specific workarounds
        if(MSVC)
            list(APPEND CLANG_TIDY_COMMAND
                "--extra-arg=/EHsc"
                "--extra-arg=/std:c++20"
            )
        endif()

        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
    else()
        message(WARNING "[KRATE] ENABLE_CLANG_TIDY is ON but clang-tidy not found in PATH")
    endif()
endif()

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

# UndefinedBehaviorSanitizer (GCC / Clang): signed overflow, out-of-range shifts, misaligned access, bad casts, ...
# Recoverable by default; the CI sanitizer lane sets UBSAN_OPTIONS=halt_on_error=1 so any report fails the run.
# Usage: cmake -DENABLE_ASAN=ON -DENABLE_UBSAN=ON ...   (sanitizers-nightly.yml, Linux)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
if(ENABLE_UBSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=undefined)
        message(STATUS "[KRATE] UndefinedBehaviorSanitizer ENABLED (-fsanitize=undefined)")
    else()
        message(WARNING "[KRATE] UndefinedBehaviorSanitizer not supported for this compiler")
    endif()
endif()

# ThreadSanitizer (GCC / Clang): data races and lock-order inversions in code that runs on more than one thread.
# Cannot share a build with ASan. -Wno-tsan: GCC warns that std::atomic_thread_fence is not modelled under TSan; the
# fences in shipped code are seqlocks whose payload words are themselves atomics (Disrumpo shared_display.h, Profundum
# processor.cpp), and TSan never reports a race between two atomic accesses, so the warning is about modelling only.
# Usage: cmake -DENABLE_TSAN=ON ...   (tsan-nightly.yml, Linux; scope: tools/tsan-scope.js)
option(ENABLE_TSAN "Enable ThreadSanitizer (GCC/Clang)" OFF)
if(ENABLE_TSAN)
    if(ENABLE_ASAN)
        message(FATAL_ERROR "[KRATE] ENABLE_TSAN and ENABLE_ASAN cannot share a build")
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-Wno-tsan)
        endif()
        add_link_options(-fsanitize=thread)
        message(STATUS "[KRATE] ThreadSanitizer ENABLED (-fsanitize=thread)")
    else()
        message(WARNING "[KRATE] ThreadSanitizer not supported for this compiler")
    endif()
endif()

# ==============================================================================
# Build-Time Instrumentation (opt-in, OFF by default)
# ==============================================================================
# Measure WHERE compile time goes so agents stop guessing header blast radius by
# grepping include counts. Enable via a timing preset (see CMakePresets.json) or
# -DKRATE_BUILD_TIMING=ON. Analyze the results with:
#     node tools/aggregate-build-times.js --build <build-dir>
#
#   Clang/GCC (Ninja): -ftime-trace writes a Chrome-trace <obj>.json next to every
#     object file; the aggregator ranks the worst headers across all TUs. Pair with
#     ClangBuildAnalyzer for a deeper per-symbol view.
#   MSVC (windows-ninja): /Bt+ prints front-end + back-end timing per TU and
#     /d2cgsummary prints a codegen summary; capture the build stdout to a log and
#     pass it with --msvc-log.
#   Any Ninja build: the aggregator also reads .ninja_log for a zero-config
#     slowest-target baseline even without these flags.
# ==============================================================================
option(KRATE_BUILD_TIMING "Emit compiler build-time instrumentation (-ftime-trace / MSVC /Bt+)" OFF)

if(KRATE_BUILD_TIMING)
    if(MSVC)
        add_compile_options(/Bt+ /d2cgsummary)
        message(STATUS "[KRATE] Build timing ENABLED (MSVC /Bt+ /d2cgsummary) - capture build stdout to a log, then: node tools/aggregate-build-times.js --msvc-log <log>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-ftime-trace)
        message(STATUS "[KRATE] Build timing ENABLED (-ftime-trace) - per-TU <obj>.json traces; then: node tools/aggregate-build-times.js --build ${CMAKE_BINARY_DIR}")
    else()
        message(WARNING "[KRATE] KRATE_BUILD_TIMING not supported for this compiler")
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

# Disable clang-tidy for external SDK (restore after add_subdirectory)
set(_SAVED_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
set(CMAKE_CXX_CLANG_TIDY "")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)

# Build the AUv3 wrapper library (macOS + Xcode only).
# The SDK only includes this under SMTG_ENABLE_VST3_PLUGIN_EXAMPLES, but our
# plugins need auv3_wrapper_macos without pulling in all the example plugins.
if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV3_BUILDS)
    # The auv3wrapper CMakeLists.txt expects SDK_ROOT and public_sdk_SOURCE_DIR
    # (set internally by the SDK's top-level CMakeLists.txt).
    set(SDK_ROOT "${vst3sdk_SOURCE_DIR}")
    set(public_sdk_SOURCE_DIR "${vst3sdk_SOURCE_DIR}/public.sdk")
    list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
    add_subdirectory(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auv3wrapper
        ${PROJECT_BINARY_DIR}/vst3sdk_auv3wrapper
    )
    # The wrapper's CMakeLists never applies the SDK's universal-binary setting; the AUv3 app and
    # extension that link it are universal (cmake/KratePlugin.cmake).
    smtg_target_setup_universal_binary(auv3_wrapper_macos)
endif()

# Restore clang-tidy for our code
set(CMAKE_CXX_CLANG_TIDY "${_SAVED_CLANG_TIDY}")
smtg_enable_vst3_sdk()

# The SDK's toolset adds -Wsuggest-override with a plain add_compile_options (SMTG_PlatformToolset.cmake, GCC), so it
# reaches C sources too and GCC warns "valid for C++/ObjC++ but not for C" (pffft, VSTGUI's wayland protocols). Make
# it C++-only: in this directory (every target from here on) and on the SDK's one C target, made in its own directory.
function(krate_cxx_only_suggest_override)
    get_directory_property(_opts COMPILE_OPTIONS)
    list(TRANSFORM _opts REPLACE "^-Wsuggest-override$" "$<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>")
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_opts}")
    if(TARGET vstgui_wayland_protocols)
        get_target_property(_topts vstgui_wayland_protocols COMPILE_OPTIONS)
        if(_topts)
            list(TRANSFORM _topts REPLACE "^-Wsuggest-override$" "$<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>")
            set_target_properties(vstgui_wayland_protocols PROPERTIES COMPILE_OPTIONS "${_topts}")
        endif()
    endif()
endfunction()
krate_cxx_only_suggest_override()

# Suppress compiler warnings from VST3 SDK and VSTGUI headers.
# The SDK sets PUBLIC (non-SYSTEM) include directories, which causes GCC/Clang
# to emit warnings for third-party code we can't modify. Re-adding them as
# SYSTEM tells the compiler to treat those headers like system headers.
function(smtg_mark_includes_as_system target)
    if(NOT TARGET ${target})
        return()
    endif()
    get_target_property(_inc ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_inc)
        set_target_properties(${target} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
        target_include_directories(${target} SYSTEM INTERFACE ${_inc})
    endif()
endfunction()

smtg_mark_includes_as_system(sdk_common)
smtg_mark_includes_as_system(sdk)
smtg_mark_includes_as_system(sdk_hosting)
smtg_mark_includes_as_system(base)
smtg_mark_includes_as_system(pluginterfaces)
smtg_mark_includes_as_system(vstgui)
smtg_mark_includes_as_system(vstgui_uidescription)
smtg_mark_includes_as_system(vstgui_support)

# Suppress warnings from VST3 SDK / VSTGUI source file compilations on GCC/Clang.
# These are third-party sources we cannot modify.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -Wformat: int64 format string mismatch (long int vs long long int) in fstring.cpp, ustring.cpp
    target_compile_options(base PRIVATE -Wno-format)
    target_compile_options(pluginterfaces PRIVATE -Wno-format)
    # -Wsuggest-override: missing override in cscrollview.cpp
    # -Wcpp: #warning TODO directives in cairobitmap.cpp, x11frame.cpp
    # -Wdeprecated-declarations: newer libc++ (Xcode 26.5 / macOS 26 SDK) marks
    #   std::wstring_convert / std::codecvt_utf8_utf16 (deprecated since C++17)
    #   as deprecated; VSTGUI's generictextedit.cpp still uses them and builds
    #   with -Werror, so the deprecation became a hard error. We cannot modify
    #   the SDK source, so don't treat the deprecation as an error here.
    if(TARGET vstgui)
        target_compile_options(vstgui PRIVATE
            -Wno-suggest-override -Wno-cpp -Wno-deprecated-declarations)
    endif()
    if(TARGET vstgui_uidescription)
        target_compile_options(vstgui_uidescription PRIVATE -Wno-deprecated-declarations)
    endif()
    if(TARGET vstgui_support)
        target_compile_options(vstgui_support PRIVATE -Wno-deprecated-declarations)
    endif()
    # -Wsuggest-override: OnDestroy::onDestroy in uidescription-scripting/detail/scriptobject.h
    if(TARGET vstgui_uiscripting)
        target_compile_options(vstgui_uiscripting PRIVATE -Wno-suggest-override)
    endif()
endif()
# SDK source we cannot modify, compiled into this directory's preset generators (seraphis / vorago / profundum) and
# gen_v2_fixtures: memcpy / memset over PClassInfo structs (GCC -Wclass-memaccess, which -Wall enables). The same
# suppression as plugins/gradus/tests and plugins/shared/tests (source properties are per directory).
set_source_files_properties(${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-class-memaccess>")
# MSVC: C4267 (size_t -> int) in ctexteditor.cpp; third-party source we cannot modify.
if(MSVC AND TARGET vstgui)
    target_compile_options(vstgui PRIVATE /wd4267)
endif()

# Clang warning groups that older compilers lack: probe each once, so a per-target -Wno-<x> below (and in the
# plugin / test CMakeLists) never trips -Wunknown-warning-option. GCC rejects an unknown positive -W, so it reads OFF.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wnan-infinity-disabled KRATE_CXX_HAS_WNAN_INFINITY_DISABLED)
check_cxx_compiler_flag(-Wvla-cxx-extension KRATE_CXX_HAS_WVLA_CXX_EXTENSION)
check_cxx_compiler_flag(-Wunnecessary-virtual-specifier KRATE_CXX_HAS_WUNNECESSARY_VIRTUAL_SPECIFIER)
check_cxx_compiler_flag("-W#pragma-messages" KRATE_CXX_HAS_WPRAGMA_MESSAGES)

# Third-party SDK sources we cannot modify (macOS CI, Xcode 26.6 / macOS 26.5 SDK):
# - sdk_hosting / sdk_common / sdk: stringconvert.cpp instantiates std::wstring_convert, which libc++ marks
#   deprecated (C++17).
# - auv3_wrapper_macos: AUv3Wrapper.mm uses C++ variable-length arrays and UTGetOSTypeFromString (deprecated in
#   macOS 12).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(_sdk_target IN ITEMS sdk_hosting sdk_common sdk)
        if(TARGET ${_sdk_target})
            target_compile_options(${_sdk_target} PRIVATE -Wno-deprecated-declarations)
        endif()
    endforeach()
    if(TARGET auv3_wrapper_macos)
        target_compile_options(auv3_wrapper_macos PRIVATE -Wno-deprecated-declarations)
        if(KRATE_CXX_HAS_WVLA_CXX_EXTENSION)
            target_compile_options(auv3_wrapper_macos PRIVATE -Wno-vla-cxx-extension)
        endif()
    endif()
endif()
# Third-party static libraries whose sources compile to empty objects on macOS: the other platforms' files
# (sdk_common: systemclipboard_win32 / _linux, threadchecker_win32 / _linux), VSTGUI's cpoint / crect (all inline)
# and vstgui_uidescription's editor files (VSTGUI_LIVE_EDITING off). Apple libtool warns "has no symbols" per object.
if(XCODE)
    foreach(_empty_obj_target IN ITEMS sdk_common vstgui vstgui_uidescription)
        if(TARGET ${_empty_obj_target})
            set_target_properties(${_empty_obj_target} PROPERTIES
                XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "-no_warning_for_no_symbols")
        endif()
    endforeach()
endif()

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
        GIT_TAG v3.16.1
    )
    FetchContent_MakeAvailable(Catch2)
    # Third-party source: catch_matchers_floating_point.cpp spells numeric_limits<>::infinity(), which clang flags
    # under the -ffast-math the SDK enables on macOS (-Wnan-infinity-disabled).
    if(KRATE_CXX_HAS_WNAN_INFINITY_DISABLED AND TARGET Catch2)
        target_compile_options(Catch2 PRIVATE -Wno-nan-infinity-disabled)
    endif()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    # krate_test_pch(): Catch2 + STL precompiled per test executable when
    # KRATE_TEST_PCH is ON. Included here, before dsp/ and the plugins add
    # their test directories.
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
    include(KrateTestPch)

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
# Google Highway (SIMD-accelerated math for spectral DSP)
# ==============================================================================
# Provides runtime dispatch: SSE2/AVX2/AVX-512 on x86, NEON on ARM.
# Single binary uses the best available ISA at load time.
include(FetchContent)
FetchContent_Declare(
    highway
    GIT_REPOSITORY https://github.com/google/highway.git
    GIT_TAG 1.4.0
)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_CONTRIB ON CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(highway)
# macOS universal binary (x86_64 + arm64) - must match KrateDSP/plugin targets
smtg_target_setup_universal_binary(hwy)
smtg_target_setup_universal_binary(hwy_contrib)
# Every consumer (Profundum's goertzel_bank_simd.cpp links hwy directly) sees Highway's headers as SYSTEM: GCC's
# -Wpedantic otherwise reports its __int128 use in hwy/base.h.
smtg_mark_includes_as_system(hwy)
smtg_mark_includes_as_system(hwy_contrib)

# ==============================================================================
# pffft (SIMD-optimized FFT, marton78 fork, BSD license)
# ==============================================================================
# Auto-detects SIMD: SSE on x86/x64, NEON on ARM, scalar fallback.
FetchContent_Declare(
    pffft
    GIT_REPOSITORY https://github.com/marton78/pffft.git
    GIT_TAG v1.2.0
)
set(PFFFT_USE_TYPE_DOUBLE OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# INSTALL_PFFFT must stay ON because the pffft CMakeLists.txt unconditionally
# references the pffft-targets export. Harmless — we never run cmake --install.
set(INSTALL_PFFFT ON CACHE BOOL "" FORCE)
set(INSTALL_PFDSP OFF CACHE BOOL "" FORCE)
set(INSTALL_PFFASTCONV OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pffft)
smtg_target_setup_universal_binary(PFFFT)
# Third-party source: unused parameter `l1` in src/pffft_priv_impl.h (GCC/Clang -Wunused-parameter).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(PFFFT PRIVATE -Wno-unused-parameter)
endif()
# Third-party source: src/simd/pf_sse1_float.h / pf_neon_float.h announce their SIMD path with #pragma message
# (clang -W#pragma-messages, macOS universal build).
if(KRATE_CXX_HAS_WPRAGMA_MESSAGES)
    target_compile_options(PFFFT PRIVATE "-Wno-#pragma-messages")
endif()
# pffft always defines PFDSP (mixer / carrier / CIC helpers) when float is on, with no option to skip it. Nothing
# here links it, and its pf_cic.cpp raises MSVC C4244 x4: keep it out of the default build.
if(TARGET PFDSP)
    set_target_properties(PFDSP PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# ==============================================================================
# cpp-httplib (header-only HTTP/HTTPS client, MIT license)
# ==============================================================================
# Test-only: shared_tests on Linux runs a loopback HTTP server with it (test_libcurl_fetch.cpp).
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.64.0
)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# ==============================================================================
# nlohmann/json (header-only JSON library, MIT license)
# ==============================================================================
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)
# Header-only third-party code compiled inside our targets: binary_reader.hpp spells numeric_limits<>::infinity(),
# which clang flags under the macOS -ffast-math (-Wnan-infinity-disabled). SYSTEM includes keep its diagnostics out
# of KratePluginsShared / shared_tests, as for the SDK headers above.
smtg_mark_includes_as_system(nlohmann_json)

# ==============================================================================
# Eigen 5 (linear algebra, MPL-2 license) -- used by tools/membrum-fit
# ==============================================================================
# Header-only. Imported as an INTERFACE target to skip Eigen's own CMake
# machinery (tests, docs, install rules). Provides Eigen3::Eigen.
option(VSTWORK_BUILD_MEMBRUM_FIT "Build the tools/membrum-fit offline drum-sample fitter" ON)

if(VSTWORK_BUILD_MEMBRUM_FIT)
    FetchContent_Declare(
        Eigen3
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 5.0.1
        GIT_SHALLOW TRUE
        # No CMakeLists.txt there, so MakeAvailable populates without add_subdirectory().
        SOURCE_SUBDIR _krate_no_cmake
    )
    FetchContent_MakeAvailable(Eigen3)
    if(NOT TARGET Eigen3::Eigen)
        add_library(Eigen3_headeronly INTERFACE)
        target_include_directories(Eigen3_headeronly INTERFACE ${eigen3_SOURCE_DIR})
        add_library(Eigen3::Eigen ALIAS Eigen3_headeronly)
    endif()

    # =====================
    # CLI11 (command-line parser, BSD-3) -- used by tools/membrum-fit
    # =====================
    FetchContent_Declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.7.2
        GIT_SHALLOW TRUE
    )
    set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(CLI11_SINGLE_FILE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(cli11)

    # =====================
    # NLopt BOBYQA (derivative-free refinement) -- tools/membrum-fit
    # =====================
    # Gated by a cache option so CI / minimal configurations can skip NLopt.
    option(MEMBRUM_FIT_ENABLE_NLOPT "Fetch and link NLopt for BOBYQA refinement" ON)
    if(MEMBRUM_FIT_ENABLE_NLOPT)
        FetchContent_Declare(
            nlopt
            GIT_REPOSITORY https://github.com/stevengj/nlopt.git
            GIT_TAG v2.11.0
            GIT_SHALLOW TRUE
        )
        set(NLOPT_PYTHON OFF CACHE BOOL "" FORCE)
        set(NLOPT_OCTAVE OFF CACHE BOOL "" FORCE)
        set(NLOPT_MATLAB OFF CACHE BOOL "" FORCE)
        set(NLOPT_GUILE OFF CACHE BOOL "" FORCE)
        set(NLOPT_SWIG OFF CACHE BOOL "" FORCE)
        set(NLOPT_TESTS OFF CACHE BOOL "" FORCE)
        set(NLOPT_FORTRAN OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(nlopt)

        # NLopt generates nlopt.f via a custom command attached as a
        # source to BOTH the `nlopt` library and the `generate-fortran` utility
        # target. Xcode's new build system rejects that unless one is a
        # dependency of the other ("attached to multiple targets" error).
        # Make `nlopt` depend on `generate-fortran` so Xcode sees a valid DAG.
        if(CMAKE_GENERATOR STREQUAL "Xcode" AND TARGET nlopt AND TARGET generate-fortran)
            add_dependencies(nlopt generate-fortran)
        endif()
        # Third-party: GCC -Wsuggest-override on ProblemInternal's virtuals in src/algs/ags/solver.cc.
        if(TARGET nlopt AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(nlopt PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-suggest-override>)
        endif()
    endif()
endif()

# ==============================================================================
# KrateDSP Library (Shared by all plugins)
# ==============================================================================
add_subdirectory(dsp)

# ==============================================================================
# Shared Plugin Infrastructure
# ==============================================================================
add_subdirectory(plugins/shared)

# ==============================================================================
# Plugins
# ==============================================================================
# Shared plugin CMake helpers (version parsing, AU targets, install, warnings).
# Defined here so they are visible to every plugin subdirectory added below.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(KratePlugin)

add_subdirectory(plugins/iterum)
add_subdirectory(plugins/disrumpo)
add_subdirectory(plugins/ruinae)
add_subdirectory(plugins/innexus)
add_subdirectory(plugins/gradus)
add_subdirectory(plugins/membrum)
add_subdirectory(plugins/seraphis)
add_subdirectory(plugins/vorago)
add_subdirectory(plugins/profundum)

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

add_executable(disrumpo_preset_generator
    tools/disrumpo_preset_generator.cpp
)
target_compile_features(disrumpo_preset_generator PRIVATE cxx_std_20)
set_target_properties(disrumpo_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Disrumpo factory presets on demand
# Usage: cmake --build build --target generate_disrumpo_presets
add_custom_target(generate_disrumpo_presets
    COMMAND disrumpo_preset_generator "${CMAKE_SOURCE_DIR}/plugins/disrumpo/resources/presets"
    DEPENDS disrumpo_preset_generator
    COMMENT "Generating Disrumpo factory presets (120 presets across 11 categories)"
    VERBATIM
)

add_executable(ruinae_preset_generator
    tools/ruinae_preset_generator.cpp
)
target_compile_features(ruinae_preset_generator PRIVATE cxx_std_20)
set_target_properties(ruinae_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Ruinae factory arp presets on demand
# Usage: cmake --build build --target generate_ruinae_presets
add_custom_target(generate_ruinae_presets
    COMMAND ruinae_preset_generator "${CMAKE_SOURCE_DIR}/plugins/ruinae/resources/presets"
    DEPENDS ruinae_preset_generator
    COMMENT "Generating Ruinae factory arp presets"
    VERBATIM
)

add_executable(innexus_preset_generator
    tools/innexus_preset_generator.cpp
)
target_compile_features(innexus_preset_generator PRIVATE cxx_std_20)
set_target_properties(innexus_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Innexus factory presets on demand
# Usage: cmake --build build --target generate_innexus_presets
add_custom_target(generate_innexus_presets
    COMMAND innexus_preset_generator "${CMAKE_SOURCE_DIR}/plugins/innexus/resources/presets"
    DEPENDS innexus_preset_generator
    COMMENT "Generating Innexus factory presets (35 presets across 7 categories)"
    VERBATIM
)

add_executable(gradus_preset_generator
    tools/gradus_preset_generator.cpp
)
target_compile_features(gradus_preset_generator PRIVATE cxx_std_20)
set_target_properties(gradus_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Gradus factory presets on demand
# Usage: cmake --build build --target generate_gradus_presets
add_custom_target(generate_gradus_presets
    COMMAND gradus_preset_generator "${CMAKE_SOURCE_DIR}/plugins/gradus/resources/presets"
    DEPENDS gradus_preset_generator
    COMMENT "Generating Gradus factory presets (25 presets across 8 categories)"
    VERBATIM
)

add_executable(membrum_preset_generator
    tools/membrum_preset_generator.cpp
)
target_compile_features(membrum_preset_generator PRIVATE cxx_std_20)
set_target_properties(membrum_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Membrum factory kit presets on demand
# Usage: cmake --build build --target generate_membrum_presets
add_custom_target(generate_membrum_presets
    COMMAND membrum_preset_generator "${CMAKE_SOURCE_DIR}/plugins/membrum/resources/presets/Kit Presets"
    DEPENDS membrum_preset_generator
    COMMENT "Generating Membrum factory kit presets (20 kits across 4 subcategories)"
    VERBATIM
)

# ==============================================================================
# seraphis_preset_generator — Seraphis Phase 12 factory-preset generator
# ==============================================================================
# The target NAME and the run-time output directory are FIXED by the release
# workflow, not free choices: .github/workflows/release.yml:149-166 maps every
# non-Iterum plugin to `${PLUGIN}_preset_generator` (the `*` case at :162),
# builds that target (:167-168), then runs
# `./build/bin/${PLUGIN}_preset_generator generated-presets` (:170-174) and
# uploads with `if-no-files-found: error` (:180). Renaming the target, or
# dropping the RUNTIME_OUTPUT_DIRECTORY below, breaks the release leg —
# release.yml itself needs NO edit.
add_executable(seraphis_preset_generator
    tools/seraphis_preset_generator.cpp
    # Seraphis has no static core library - plugins/seraphis/CMakeLists.txt:18-67 puts every .cpp inside
    # one smtg_add_vst3plugin MODULE - so the processor is compiled as an executable source, exactly as
    # gen_v2_fixtures does for Gradus/Ruinae (root CMakeLists.txt:689-720).
    plugins/seraphis/src/processor/processor.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
    # GetPluginFactory stub ONLY - this file is 12 lines and defines nothing else
    # (plugins/seraphis/tests/vstgui_test_stubs.cpp:12). moduleHandle is NOT here; it is defined in
    # tools/seraphis_preset_generator.cpp (T005).
    plugins/seraphis/tests/vstgui_test_stubs.cpp
)

# NO vstgui_support: FR-015 - the generator must not pull VSTGUI in. processor.cpp's only ui/ include is
# ui/edit_message.h (a POD plus three constexpr strings, no VSTGUI type; processor.cpp:18, :21).
target_link_libraries(seraphis_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)

target_include_directories(seraphis_preset_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/seraphis/src      # "plugin_ids.h", "processor/processor.h", "parameters/..."
    ${CMAKE_SOURCE_DIR}/plugins/seraphis/tests    # seraphis_test_fixture.h
    ${CMAKE_SOURCE_DIR}/tests/test_helpers        # <vst_event_list.h> - HEADER ONLY, not the
                                                  # test_helpers target (it links Catch2 INTERFACE)
    ${CMAKE_SOURCE_DIR}/tools                     # seraphis_preset_defs.h
    ${vst3sdk_SOURCE_DIR}
)

target_compile_features(seraphis_preset_generator PRIVATE cxx_std_20)

set_target_properties(seraphis_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Warning posture copies gen_v2_fixtures (:762-778). Seraphis additionally needs MSVC /wd4459 for the
# function-local `kPi` in dsp/include/krate/dsp/systems/timevar_comb_bank.h:915 shadowing the namespace
# -scope Krate::DSP::kPi - the same suppression plugins/seraphis/CMakeLists.txt:120-135 documents, applied
# HERE on this target only.
if(MSVC)
    target_compile_options(seraphis_preset_generator PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
        /wd4459
    )
else()
    target_compile_options(seraphis_preset_generator PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Custom target to regenerate Seraphis factory presets on demand
# Usage: cmake --build build --target generate_seraphis_presets
add_custom_target(generate_seraphis_presets
    COMMAND seraphis_preset_generator "${CMAKE_SOURCE_DIR}/plugins/seraphis/resources/presets"
    DEPENDS seraphis_preset_generator
    COMMENT "Generating Seraphis factory presets (42 presets across 7 categories)"
    VERBATIM
)

# ==============================================================================
# vorago_preset_generator — Vorago Phase 14 factory-preset generator
# ==============================================================================
# The target NAME and the run-time output directory are FIXED by the release
# workflow (.github/workflows/release.yml:150-182): every non-Iterum plugin maps
# to `${PLUGIN}_preset_generator`, built and then run as
# `./build/bin/${PLUGIN}_preset_generator generated-presets` with
# `if-no-files-found: error`. Copied from the Seraphis block above.
add_executable(vorago_preset_generator
    tools/vorago_preset_generator.cpp
    # Vorago's processor.cpp is compiled as an executable source, as for Seraphis.
    plugins/vorago/src/processor/processor.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
    # GetPluginFactory stub ONLY; moduleHandle is defined in tools/vorago_preset_generator.cpp.
    plugins/vorago/tests/vstgui_test_stubs.cpp
)

# NO vstgui_support and NO Catch2: the generator must not pull either in.
target_link_libraries(vorago_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)

target_include_directories(vorago_preset_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/vorago/src       # "plugin_ids.h", "processor/processor.h"
    ${CMAKE_SOURCE_DIR}/plugins/vorago/tests     # vorago_preset_host.h
    ${CMAKE_SOURCE_DIR}/tests/test_helpers       # <vst_event_list.h> - HEADER ONLY, not the
                                                 # test_helpers target (it links Catch2 INTERFACE)
    ${CMAKE_SOURCE_DIR}/tools                    # vorago_preset_defs.h
    ${vst3sdk_SOURCE_DIR}
)

target_compile_features(vorago_preset_generator PRIVATE cxx_std_20)

set_target_properties(vorago_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Warning posture copies seraphis_preset_generator above, including MSVC /wd4459.
if(MSVC)
    target_compile_options(vorago_preset_generator PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
        /wd4459
    )
else()
    target_compile_options(vorago_preset_generator PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Custom target to regenerate Vorago factory presets on demand
# Usage: cmake --build build --target generate_vorago_presets
add_custom_target(generate_vorago_presets
    COMMAND vorago_preset_generator "${CMAKE_SOURCE_DIR}/plugins/vorago/resources/presets"
    DEPENDS vorago_preset_generator
    COMMENT "Generating Vorago factory presets (the Init preset + 42 across 7 categories)"
    VERBATIM
)

# ==============================================================================
# profundum_preset_generator — Profundum Phase 13 factory-preset generator
# ==============================================================================
# The target NAME and the run-time output directory are FIXED by the release
# workflow (.github/workflows/release.yml:150-182): every non-Iterum plugin maps
# to `${PLUGIN}_preset_generator`, built and then run as
# `./build/bin/${PLUGIN}_preset_generator generated-presets` with
# `if-no-files-found: error`. Copied from the Vorago block above.
add_executable(profundum_preset_generator
    tools/profundum_preset_generator.cpp
    # Profundum's processor.cpp is compiled as an executable source, as for Vorago.
    plugins/profundum/src/processor/processor.cpp
    # The plugin-local Goertzel kernel (spectrum_producer.h needs it).
    plugins/profundum/src/processor/goertzel_bank_simd.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
    # GetPluginFactory stub ONLY; moduleHandle is defined in tools/profundum_preset_generator.cpp.
    plugins/profundum/tests/vstgui_test_stubs.cpp
)

# NO VSTGUI (neither vstgui nor vstgui_support) and NO Catch2 (SC-010): KratePluginsShared links
# vstgui PUBLIC (plugins/shared/CMakeLists.txt), so it is NOT linked here; the generator needs only the
# header-only plugins/shared/src/midi/sustain_latch.h (processor.h), added as an include directory below.
# hwy: goertzel_bank_simd.cpp uses Highway directly.
target_link_libraries(profundum_preset_generator PRIVATE KrateDSP hwy sdk)

target_include_directories(profundum_preset_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/profundum/src    # "plugin_ids.h", "processor/processor.h"
    ${CMAKE_SOURCE_DIR}/plugins/profundum/tests  # profundum_preset_host.h
    ${CMAKE_SOURCE_DIR}/tests/test_helpers       # HEADER ONLY, not the test_helpers
                                                 # target (it links Catch2 INTERFACE)
    ${CMAKE_SOURCE_DIR}/tools                    # profundum_preset_defs.h
    ${CMAKE_SOURCE_DIR}/plugins/shared/src       # "midi/sustain_latch.h" (header only)
    ${vst3sdk_SOURCE_DIR}
)

target_compile_features(profundum_preset_generator PRIVATE cxx_std_20)

set_target_properties(profundum_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Warning posture copies vorago_preset_generator above, including MSVC /wd4459.
if(MSVC)
    target_compile_options(profundum_preset_generator PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
        /wd4459
    )
else()
    target_compile_options(profundum_preset_generator PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Custom target to regenerate Profundum factory presets on demand
# Usage: cmake --build build --target generate_profundum_presets
add_custom_target(generate_profundum_presets
    COMMAND profundum_preset_generator "${CMAKE_SOURCE_DIR}/plugins/profundum/resources/presets"
    DEPENDS profundum_preset_generator
    COMMENT "Generating Profundum factory presets (73 presets across 7 categories)"
    VERBATIM
)

# ==============================================================================
# gen_v2_fixtures — Spec 142 migration regression fixture generator
# ==============================================================================
# Generates byte-exact v2 state fixtures (.bin) for the Gradus processor and
# golden MIDI captures (.txt) for both Gradus and Ruinae before the spec-142
# kCurrentStateVersion 2->3 / kNumLanes 9->10 changes land. Must be run on the
# pre-feature parent commit; the harness source is checked in so future
# maintainers can re-generate.
#
# Build: cmake --build build --target gen_v2_fixtures
# Run  : build/.../bin/Release/gen_v2_fixtures.exe
#
# Outputs:
#   plugins/gradus/tests/fixtures/gradus_v2_preset_{default,heavy_lanes,midi_delay}.bin
#   plugins/gradus/tests/fixtures/gradus_v2_golden_midi_{default,heavy_lanes,midi_delay}.txt
#   plugins/ruinae/tests/fixtures/ruinae_factory_{preset}_golden_midi.txt (per arp preset)
#
# Marked EXCLUDE_FROM_ALL: not built by default; only when explicitly requested.
# ==============================================================================
add_executable(gen_v2_fixtures EXCLUDE_FROM_ALL
    # Harness orchestration (plugin-agnostic)
    tools/gen_v2_fixtures/main.cpp
    tools/gen_v2_fixtures/common.cpp

    # Per-plugin generators (each lives in its own TU so the Gradus and Ruinae
    # plugin_ids.h enum namespaces — which contain conflicting symbols at the
    # same numeric IDs — never compile together in a single translation unit)
    tools/gen_v2_fixtures/gen_gradus.cpp
    tools/gen_v2_fixtures/gen_ruinae.cpp

    # Gradus processor (audio thread) + parameters
    plugins/gradus/src/processor/processor.cpp

    # Ruinae processor (audio thread)
    plugins/ruinae/src/processor/processor.cpp
    plugins/ruinae/src/processor/processor_state.cpp
    plugins/ruinae/src/processor/processor_params.cpp
    plugins/ruinae/src/processor/processor_midi.cpp
    plugins/ruinae/src/processor/processor_messaging.cpp

    # VST3 SDK helpers (PresetFile + MemoryStream + module init)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstpresetfile.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp

    # GetPluginFactory stub (resolves entry-point symbol referenced by SDK)
    plugins/gradus/tests/vstgui_test_stubs.cpp
)

target_link_libraries(gen_v2_fixtures
    PRIVATE
        KrateDSP
        KratePluginsShared
        sdk
)

# Target-level include directories: common ones used by every TU.
target_include_directories(gen_v2_fixtures
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tools/gen_v2_fixtures
        ${vst3sdk_SOURCE_DIR}
)

# Per-source include dirs: each plugin's src/ is added ONLY for the TU that
# includes its headers, so the `#include "plugin_ids.h"` in each TU resolves
# to the correct plugin's plugin_ids.h.
set_source_files_properties(
    tools/gen_v2_fixtures/gen_gradus.cpp
    plugins/gradus/src/processor/processor.cpp
    PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/plugins/gradus/src"
)
set_source_files_properties(
    tools/gen_v2_fixtures/gen_ruinae.cpp
    plugins/ruinae/src/processor/processor.cpp
    plugins/ruinae/src/processor/processor_state.cpp
    plugins/ruinae/src/processor/processor_params.cpp
    plugins/ruinae/src/processor/processor_midi.cpp
    plugins/ruinae/src/processor/processor_messaging.cpp
    PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/plugins/ruinae/src"
)

target_compile_features(gen_v2_fixtures PRIVATE cxx_std_20)

set_target_properties(gen_v2_fixtures PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Match gradus_tests warning posture (silence VST SDK headers' /W4 chatter)
if(MSVC)
    target_compile_options(gen_v2_fixtures PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
else()
    target_compile_options(gen_v2_fixtures PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

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

# ==============================================================================
# tools/membrum-fit -- offline drum-sample to Membrum-preset fitter
# ==============================================================================
# Analysis-by-synthesis CLI that ingests WAV drum samples, renders through the
# production Membrum voice (via membrum_dsp), and emits .vstpreset files.
# See specs/membrum-fit-tool.md for the execution plan.
if(VSTWORK_BUILD_MEMBRUM_FIT)
    add_subdirectory(tools/membrum-fit)
endif()

# ==============================================================================
# tools/krate-render -- offline audio render + feature-extraction CLI
# ==============================================================================
# Renders a plugin Processor to WAV + a JSON feature summary; the executor for
# the "render is ground truth" A/B loop. Currently Membrum-only (links membrum_core).
option(VSTWORK_BUILD_KRATE_RENDER "Build the tools/krate-render offline render CLI" ON)

if(VSTWORK_BUILD_KRATE_RENDER)
    add_subdirectory(tools/krate-render)
endif()

# ==============================================================================
# Copy compile_commands.json to project root (for clangd / Serena)
# ==============================================================================
if(EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    configure_file(
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COPYONLY
    )
    message(STATUS "[KRATE] Copied compile_commands.json to project root")
endif()
